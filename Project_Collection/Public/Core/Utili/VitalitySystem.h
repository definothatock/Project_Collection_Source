// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Engine/DataTable.h"

#include "VitalitySystem.generated.h"


// ==================== Declares ==================== 

/**
 * How an effect contributes to the meter cap.
 *  - Flat: applies a constant cap modifier (e.g. -40 from wound, +20 from stim)
 *  - RampOverTime: accumulates at Magnitude/sec (e.g. -3 from poison)
 */
UENUM(BlueprintType)
enum class EVitalityEffectMode : uint8
{
    Flat,
    RampOverTime
};


/**
 * A single unit of named status effect that caps or raises the meter.
 * This struct is both the input payload and the stored runtime state.
 */
USTRUCT(BlueprintType)
struct FVitalityEffect
{
    GENERATED_BODY()

    // Unique name of effect type; NOT unique instance id, effect of the same type can stack.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    FName EffectId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    EVitalityEffectMode Mode = EVitalityEffectMode::Flat;

    /* Flat: the change value.
     * Ramp: the per-second change applied to the modifier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float Magnitude = 0.f;

	// <=0: indefinite unless removed. >0: starts expiry/removal behavior after this time.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float SustainingDuration = 0.f;

	// How long does the ramp up last? (SustainingDuration only start counting after RampDuration)
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
	// float RampingDuration = 0.f;
	
	// If true and SustainingDuration elapsed, instance enters removal phase and ramps toward 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
	bool bRampRemovalAfterDuration = false;

	// Units/sec toward 0 during removal phase.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
	float RemovalRampRate = 0.f;
	
    // Ramp only: absolute clamp on the accumulated modifier (0 => unclamped).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float MaxMagnitude = 0.f;

    // Is this effect permanent before reset
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    bool bPermanent = false;

    // Runtime: the current cap contribution of this effect.
    UPROPERTY(BlueprintReadOnly, Category = "Vitality")
    float CurrentContribution = 0.f;

    // Runtime: time elapsed since application or refreshed.
    UPROPERTY(BlueprintReadOnly, Category = "Vitality")
    float Elapsed = 0.f;

	// Runtime: state flag for ramp-mode effects
	UPROPERTY(BlueprintReadOnly, Category = "Vitality")
	bool bInRemovalPhase = false;
};

/**
 * DataTable row for initializing effects.
 * Authored in a CSV / data asset.
 */
USTRUCT(BlueprintType)
struct FVitalityEffectDef : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    EVitalityEffectMode Mode = EVitalityEffectMode::Flat;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float Magnitude = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float SustainingDuration = 0.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
	bool bRampRemovalAfterDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
	float RemovalRampRate = 0.f;
	
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    float MaxMagnitude = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality")
    bool bPermanent = false;
};

/**
 * A continuous stamina cost (e.g. sprinting, channeling).
 * Server-side only.
 */
USTRUCT()
struct FStaminaDrain
{
    GENERATED_BODY()

    UPROPERTY()
    FName DrainId = NAME_None;

    UPROPERTY()
    float RatePerSecond = 0.f;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVitalitySignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVitalityMeterSignature, int32, Available, int32, Cap);



/**
 * VitalitySystem manages a character's stamina pool and dynamic meter cap.
 *
 * Main Function:
 * - Tracks available stamina and a configurable meter cap.
 * - Applies named effects that raise or lower the cap over time or instantly.
 * - Handles bulk stamina consumption, continuous drains, and controlled regeneration.
 *
 * Main Rules:
 * - The meter cap can be modified by effects and may become negative.
 * - Available stamina is clamped to the current cap and reported as an integer.
 * - A cap of 0 or below means the character is fainted, but not necessarily dead.
 *
 * State:
 * - BaseCap is the undamaged cap when no effects are active.
 * - Active effects are stored as runtime instances and may expire or ramp down.
 * - Stamina drains and regeneration are resolved every tick.
 *
 * Boundary:
 * - Fainted = cap <= 0; this component does not itself resolve the actor's death behavior.
 * - Concluded death is a terminal state until ResetVitals is called.
 *
 * Networking:
 * - Authority-only APIs are prefixed with Auth_ and must be invoked on the server.
 * - Clients may request consumption via Request_TryConsumeStamina and receive results through replication.
 *
 * Notice:
 * - "Fainted" is not "concluded death".
 * - Internally uses floats for simulation, but exposed meter values are integer.
 *
 * Future Updates:
 * - Effects are Delta-time update. Will switch to timer  update (each instance handle itself).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_COLLECTION_API UVitalitySystem : public UActorComponent
{
	GENERATED_BODY()

public:
	UVitalitySystem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;


	// ==================== APIs ====================
    /**
     * State mutation.
     * Server-authoritative. Calls without authority or RPC: do nothing
     */

public:
	// ----- Effects ----- //
	
    // Apply one effect instance (stacks in array if same EffectId instance existed).
    UFUNCTION(BlueprintCallable, Category = "Vitality|Effects")
    void Auth_ApplyEffect(FVitalityEffect Effect);
	
    // Apply an effect using a row from EffectDatabase. Returns false if the row is missing.
    // ANCHOR: This might be changed.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Effects")
    bool Auth_ApplyEffectById(FName EffectId);

    // Remove all active instances for this EffectId.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Effects")
    void Auth_RemoveEffect(FName EffectId);
	
    // Cleanse. Remove every effect currently lowering the cap.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Effects")
    void Auth_RemoveAllNegativeEffects();
	
    // Remove all effects.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Effects")
    void Auth_ClearAllEffects();

	// ----- Stamina ----- //
	
    // Bulk consume. Returns true only if the full amount was available (authority only).
	// ANCHOR: move to private and under internal since no extern should be using.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    bool Auth_TryConsumeStamina(float Amount);
	
    // Client-safe consume request; result arrives via replication.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Request_TryConsumeStamina(float Amount);

    // Register/refresh a continuous drain (gradual consumption).
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_AddOrUpdateStaminaDrain(FName DrainId, float RatePerSecond);
	//
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_RemoveStaminaDrain(FName DrainId);
	
    // External system gates regeneration.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_SetRegenAllowed(bool bAllowed);
	
    // Temporary regen rate scaling (1.0 = standard).
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_SetRegenRateMultiplier(float Multiplier);
	
	//
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_ResetRegenRate();
	
    // Lock prevents consumption (bulk + drains). Does NOT stop regeneration.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Stamina")
    void Auth_SetStaminaLocked(bool bLocked);

	// ----- Lifecycle ----- //
	
    // Definitive death. Persists state but halts simulation until ResetVitals.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Lifecycle")
    void Auth_ConcludeDeath();
	
    // Full reset. Only valid path back from concluded death.
    UFUNCTION(BlueprintCallable, Category = "Vitality|Lifecycle")
    void Auth_ResetVitals();

	
	// ==================== Configs ==================== 

public:
	// The undamaged maximum the cap returns to when all effects are cleared.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|Config")
	float BaseCap = 100.f;

	// Abs of How far below 0 AND above BaseCap the cap be. Clamp excessive effects
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|Config")
	// float CapMeterMargin = 20.f;

	// Standard regeneration rate (units/second) when regen is allowed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|Config")
	float BaseRegenRate = 15.f;

	// Optional CSV / DataTable of FVitalityEffectDef rows for ApplyEffectById.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|Config")
	TObjectPtr<UDataTable> EffectDatabase = nullptr;


	// ==================== Queries ====================

public:
	// Available stamina as an integer in [0, max(Cap,0)].
	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	int32 GetAvailableStamina() const;

	// Current meter cap as an integer. May be negative.
	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	int32 GetMeterCap() const;

	// Unconditioned base cap (integer).
	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	int32 GetBaseCap() const
	{ return FMath::FloorToInt(BaseCap); }

	// How far below 0 the cap is (0 if non-negative). Larger => harder to revive.
	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	int32 GetCapDeficit() const;

	// 0~1 ratio of available stamina against the base cap, for UI usage.
	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	float GetAvailableRatio() const;

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool IsFainted() const
	{ return CachedCap <= 0.f; }

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool HasConcludedDeath() const
	{ return bConcludedDeath; }

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool IsStaminaLocked() const
	{ return bStaminaLocked; }

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool IsRegenAllowed() const
	{ return bRegenAllowed; }

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	const TArray<FVitalityEffect>& GetActiveEffects() const
	{ return ActiveEffectArray; }

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool HasEffect(FName EffectId) const;

	UFUNCTION(BlueprintPure, Category = "Vitality|Query")
	bool GetEffect(FName EffectId, FVitalityEffect& OutEffect) const;

	
	// ===================== Delegates =====================

public:
	// Fires when the meter cap drops to 0. Actor should faint (does not handle here).
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalitySignature OnFainted;

	// Fires when the cap rises back above 0. regains consciousness (does not handle here).
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalitySignature OnRegainedConsciousness;

	// Fires when AVAILABLE stamina is used up to 0 while still conscious (cap > 0).
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalitySignature OnStaminaDepleted;

	// Fires on a definitive death conclusion (only ResetVitals() re-enables this System).
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalitySignature OnConcludedDeath;

	// Fires whenever the available stamina (integer) changes.
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalityMeterSignature OnStaminaChanged;

	// Fires whenever the meter cap (integer) changes.
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalityMeterSignature OnCapChanged;

	// Fires when the active-effect set changes (add/remove). for UI refresh.
	UPROPERTY(BlueprintAssignable, Category = "Vitality|Events")
	FVitalitySignature OnEffectsChanged;

	
	// ==================== Internal Variables ====================

protected:
	// ---- Replicated state (display + authority result) ----
	
	UPROPERTY(ReplicatedUsing = OnRep_CurrentStamina)
	float CurrentStamina = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_CachedCap)
	float CachedCap = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveEffects)
	TArray<FVitalityEffect> ActiveEffectArray;

	UPROPERTY(ReplicatedUsing  = OnRep_ConcludedDeath)
	bool bConcludedDeath = false;

	UPROPERTY(Replicated)
	bool bStaminaLocked = false;

	UPROPERTY(Replicated)
	bool bRegenAllowed = true;

	// ---- Server runtime ----
	
	UPROPERTY(Transient)
	TArray<FStaminaDrain> ActiveDrains;



	float RegenRateMultiplier = 1.f;

	// ---- Local transition tracking (server & client) ----
	
	int32 LastAvailStamina_Int = 0;
	int32 LastCap_Int = 0;
	bool  bLastFainted = false;


	// ==================== Internal functions ==================== 

protected:
	UFUNCTION()
	void OnRep_CurrentStamina();
	
	UFUNCTION()
	void OnRep_CachedCap();
	
	UFUNCTION()
	void OnRep_ActiveEffects();

	UFUNCTION()
	void OnRep_ConcludedDeath();
	
	UFUNCTION(Server, Reliable)
	void RpcServer_TryConsumeStamina(float Amount);

private:
	bool HasAuthority() const;

	float ComputeCap() const;
	
	// true on structural change
	bool UpdateEffects(float DeltaTime);
	
	// recompute cap, clamp stamina, broadcast
	void RecalculateState();
	
	// diff against Last update and fire delegates
	void EvaluateStateAndBroadcast();
	
	// seed Last update without spurious events
	void InitialiseBaselines();
};
