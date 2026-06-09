// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Systems/Vitality/Component/VitalityComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"


UVitalityComponent::UVitalityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}


void UVitalityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVitalityComponent, CurrentStamina);
	DOREPLIFETIME(UVitalityComponent, CachedCap);
	DOREPLIFETIME(UVitalityComponent, ActiveEffectArray);
	DOREPLIFETIME(UVitalityComponent, bConcludedDeath);
	DOREPLIFETIME(UVitalityComponent, bStaminaLocked);
	DOREPLIFETIME(UVitalityComponent, bRegenAllowed);
}


void UVitalityComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CachedCap = ComputeCap();
		CurrentStamina = FMath::Max(CachedCap, 0.f);
	}
	InitialiseBaselines();

}


void UVitalityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Server authoritative: clients only display replicated values.
	if (!HasAuthority() || bConcludedDeath || DeltaTime <= 0.f)
		{return;}

	const bool bStructural = UpdateEffects(DeltaTime);
	CachedCap = ComputeCap();

	// Gradual consumption (skipped while locked).
	if (!bStaminaLocked && ActiveDrains.Num() > 0)
	{
		float DrainTotal = 0.f;
		for (const FStaminaDrain& Drain : ActiveDrains)
		{
			DrainTotal += Drain.RatePerSecond;
		}
		CurrentStamina -= DrainTotal * DeltaTime;
	}

	// Regeneration is gated only by bRegenAllowed (lock does not block it).
	if (bRegenAllowed)
	{
		CurrentStamina += BaseRegenRate * RegenRateMultiplier * DeltaTime;
	}

	CurrentStamina = FMath::Clamp(CurrentStamina, 0.f, FMath::Max(CachedCap, 0.f));

	EvaluateStateAndBroadcast();
	if (bStructural)
	{
		OnEffectsChanged.Broadcast();
	}
}


// ==================== APIs ====================


// ----- Effects ----- //

void UVitalityComponent::Auth_ApplyEffect(FVitalityEffect Effect)
{
	if (!HasAuthority() || bConcludedDeath || Effect.EffectId.IsNone())
		{return;}

	Effect.Elapsed = 0.f;
	Effect.bInRemovalPhase = false;
	Effect.CurrentContribution = (Effect.Mode == EVitalityEffectMode::Flat) ? Effect.Magnitude : 0.f;

	// Stacking: same EffectId can have multiple instances.
	ActiveEffectArray.Add(Effect);

	RecalculateState();
	OnEffectsChanged.Broadcast();
}


bool UVitalityComponent::Auth_ApplyEffectById(FName EffectId)
{
	if (!HasAuthority() || bConcludedDeath || !EffectDatabase)
		{return false;}

	const FVitalityEffectDef* Row =
		EffectDatabase->FindRow<FVitalityEffectDef>(EffectId, TEXT("ApplyEffectById"), false);
	if (!Row)
		{ return false; }

	FVitalityEffect Effect;
    Effect.EffectId						= EffectId;
    Effect.Mode							= Row->Mode;
    Effect.Magnitude					= Row->Magnitude;
    Effect.SustainingDuration			= Row->SustainingDuration;
    Effect.bRampRemovalAfterDuration	= Row->bRampRemovalAfterDuration;
    Effect.RemovalRampRate				= Row->RemovalRampRate;
    Effect.MaxMagnitude					= Row->MaxMagnitude;
    Effect.bPermanent					= Row->bPermanent;

	Auth_ApplyEffect(Effect);
	return true;
}


void UVitalityComponent::Auth_RemoveEffect(FName EffectId)
{
	if (!HasAuthority())
		{return;}

	const int32 Removed = ActiveEffectArray.RemoveAll(
		[&](const FVitalityEffect& E) { return E.EffectId == EffectId; });

	// Should only have one entry per EffectId. assume no need to gate for now.
	if (Removed > 0)
	{
		RecalculateState();
		OnEffectsChanged.Broadcast();
	}
}


void UVitalityComponent::Auth_RemoveAllNegativeEffects()
{
	if (!HasAuthority()) { return; }

	const int32 Removed = ActiveEffectArray.RemoveAll(
		[](const FVitalityEffect& E) { return E.CurrentContribution < 0.f; });

	if (Removed > 0)
	{
		RecalculateState();
		OnEffectsChanged.Broadcast();
	}
}


void UVitalityComponent::Auth_ClearAllEffects()
{
	if (!HasAuthority() || ActiveEffectArray.Num() == 0)
		{return;}

	ActiveEffectArray.Reset();
	RecalculateState();
	OnEffectsChanged.Broadcast();
}

// ----- Stamina ----- //

bool UVitalityComponent::Auth_TryConsumeStamina(float Amount)
{
	if (!HasAuthority() || bConcludedDeath || bStaminaLocked || IsFainted() || Amount <= 0.f)
		{return false;}
	if (CurrentStamina < Amount)
		{return false;}

	CurrentStamina -= Amount;
	EvaluateStateAndBroadcast();
	
	if (GetOwner())
		{ GetOwner()->ForceNetUpdate(); }
	
	return true;
}


void UVitalityComponent::Request_TryConsumeStamina(float Amount)
{
	if (HasAuthority())
		{ Auth_TryConsumeStamina(Amount); }
	else
		{ RpcServer_TryConsumeStamina(Amount); }
}


void UVitalityComponent::Auth_AddOrUpdateStaminaDrain(FName DrainId, float RatePerSecond)
{
	if (!HasAuthority() || bConcludedDeath || DrainId.IsNone())
		{ return; }

	if (FStaminaDrain* Existing = ActiveDrains.FindByPredicate(
			[&](const FStaminaDrain& D) { return D.DrainId == DrainId; }))
	{
		Existing->RatePerSecond = RatePerSecond;
	}
	else
	{
		FStaminaDrain Drain;
		Drain.DrainId = DrainId;
		Drain.RatePerSecond = RatePerSecond;
		ActiveDrains.Add(Drain);
	}
}

void UVitalityComponent::Auth_RemoveStaminaDrain(FName DrainId)
{
	if (!HasAuthority()) {return;}
	
	ActiveDrains.RemoveAll([&](const FStaminaDrain& D) { return D.DrainId == DrainId; });
}


void UVitalityComponent::Auth_SetRegenAllowed(bool bAllowed)
{
	if (!HasAuthority()) {return;}
	bRegenAllowed = bAllowed;
}


void UVitalityComponent::Auth_SetRegenRateMultiplier(float Multiplier)
{
	if (!HasAuthority()) { return; }
	
	RegenRateMultiplier = FMath::Max(0.f, Multiplier);
}

void UVitalityComponent::Auth_ResetRegenRate()
{
	if (!HasAuthority()) { return; }
	
	RegenRateMultiplier = 1.f;
}

void UVitalityComponent::Auth_SetStaminaLocked(bool bLocked)
{
	if (!HasAuthority()) { return; }
	
	bStaminaLocked = bLocked;
}

void UVitalityComponent::Auth_ConcludeDeath()
{
	if (!HasAuthority() || bConcludedDeath) { return; }
	
	bConcludedDeath = true;
	OnConcludedDeath.Broadcast();
	
	if (GetOwner())
		{ GetOwner()->ForceNetUpdate(); }
}

void UVitalityComponent::Auth_ResetVitals()
{
	if (!HasAuthority()) { return; }

	ActiveEffectArray.Reset();
	ActiveDrains.Reset();
	bConcludedDeath = false;
	bStaminaLocked = false;
	bRegenAllowed = true;
	RegenRateMultiplier = 1.f;

	CachedCap = ComputeCap();
	CurrentStamina = FMath::Max(CachedCap, 0.f);

	InitialiseBaselines();
	EvaluateStateAndBroadcast();
	OnEffectsChanged.Broadcast();
	if (GetOwner()) { GetOwner()->ForceNetUpdate(); }
}


// ==================== Queries ====================


int32 UVitalityComponent::GetAvailableStamina() const
{
	const int32 CapInt = FMath::Max(FMath::FloorToInt(CachedCap), 0);
	return FMath::Clamp(FMath::FloorToInt(CurrentStamina), 0, CapInt);
}


int32 UVitalityComponent::GetMeterCap() const
{
	return FMath::FloorToInt(CachedCap); // may be negative
}


int32 UVitalityComponent::GetCapDeficit() const
{
	return CachedCap < 0.f ? -FMath::FloorToInt(CachedCap) : 0;
}


float UVitalityComponent::GetAvailableRatio() const
{
	return BaseCap > 0.f ? FMath::Clamp(static_cast<float>(GetAvailableStamina()) / BaseCap, 0.f, 1.f) : 0.f;
}


bool UVitalityComponent::HasEffect(FName EffectId) const
{
	return ActiveEffectArray.ContainsByPredicate(
		[&](const FVitalityEffect& E) { return E.EffectId == EffectId; });
}


bool UVitalityComponent::GetEffect(FName EffectId, FVitalityEffect& OutEffect) const
{
	if (const FVitalityEffect* Found = ActiveEffectArray.FindByPredicate(
			[&](const FVitalityEffect& E) { return E.EffectId == EffectId; }))
	{
		OutEffect = *Found;
		return true;
	}
	return false;
}


// ===================== Internal functions =====================


void UVitalityComponent::OnRep_CurrentStamina()
{
	EvaluateStateAndBroadcast();
}


void UVitalityComponent::OnRep_CachedCap()
{
	EvaluateStateAndBroadcast();
}


void UVitalityComponent::OnRep_ActiveEffects()
{
	OnEffectsChanged.Broadcast();
	EvaluateStateAndBroadcast();
}

void UVitalityComponent::OnRep_ConcludedDeath()
{
}


void UVitalityComponent::RpcServer_TryConsumeStamina_Implementation(float Amount)
{
	Auth_TryConsumeStamina(Amount);
}


bool UVitalityComponent::HasAuthority() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}


float UVitalityComponent::ComputeCap() const
{
	float Cap = BaseCap;
	for (const FVitalityEffect& Effect : ActiveEffectArray)
	{
		Cap += Effect.CurrentContribution;
	}
	return Cap;
}


bool UVitalityComponent::UpdateEffects(float DeltaTime)
{
	bool bRemovedEntry = false;

	for (int32 i = ActiveEffectArray.Num() - 1; i >= 0; --i)
	{
		FVitalityEffect& Effect = ActiveEffectArray[i];

		// Lifetime tracking 
		if (!Effect.bPermanent && Effect.SustainingDuration > 0.f)
		{ // is temporal effects
			Effect.Elapsed += DeltaTime;
			
			if (!Effect.bInRemovalPhase && Effect.Elapsed >= Effect.SustainingDuration)
			{ // state change: active -> removal 
				if (Effect.bRampRemovalAfterDuration && Effect.RemovalRampRate > 0.f)
				{ // ramp-mode effect
					Effect.bInRemovalPhase = true;
				}
				else
				{ // directly remove flat-mode effect
					ActiveEffectArray.RemoveAt(i);
					bRemovedEntry = true;
					continue;
				}
			}
		}
		
		// removal handling
		if (Effect.bInRemovalPhase)
		{
			Effect.CurrentContribution = FMath::FInterpConstantTo(
				Effect.CurrentContribution,
				0.f, DeltaTime,
				FMath::Max(0.f, Effect.RemovalRampRate)
				);

			if (FMath::IsNearlyZero(Effect.CurrentContribution, KINDA_SMALL_NUMBER))
			{
				ActiveEffectArray.RemoveAt(i);
				bRemovedEntry = true;
				continue;
			}
		}
		else // standard update
		{
			if (Effect.Mode == EVitalityEffectMode::RampOverTime)
			{
				Effect.CurrentContribution += Effect.Magnitude * DeltaTime; //ANCHOR: switch to discrete update later 
			}
			else
			{
				Effect.CurrentContribution = Effect.Magnitude; //ANCHOR: no need to update everytime if flat
			}

			if (Effect.MaxMagnitude > 0.f)
			{
				Effect.CurrentContribution = FMath::Clamp(
					Effect.CurrentContribution, -Effect.MaxMagnitude, Effect.MaxMagnitude);
			}
		}
	}

	return bRemovedEntry;
}


void UVitalityComponent::RecalculateState()
{
    CachedCap = ComputeCap();
    CurrentStamina = FMath::Clamp(CurrentStamina, 0.f, FMath::Max(CachedCap, 0.f));
    EvaluateStateAndBroadcast();
    if (GetOwner()) { GetOwner()->ForceNetUpdate(); }
}


void UVitalityComponent::EvaluateStateAndBroadcast()
{
	const int32 Avail = GetAvailableStamina();
	const int32 Cap   = GetMeterCap();
	const bool  bNowFainted = IsFainted();

	if (Cap != LastCap_Int)
	{
		OnCapChanged.Broadcast(Avail, Cap);
		LastCap_Int = Cap;
	}

	if (bNowFainted != bLastFainted)
	{
		if (bNowFainted)
			{ OnFainted.Broadcast(); }
		else
			{ OnRegainedConsciousness.Broadcast(); }
		
		bLastFainted = bNowFainted;
	}

	if (Avail != LastAvailStamina_Int)
	{
		OnStaminaChanged.Broadcast(Avail, Cap);
		// "Used up to 0" is distinct from fainting (cap == 0).
		if (Avail == 0 && !bNowFainted && LastAvailStamina_Int > 0)
		{
			OnStaminaDepleted.Broadcast();
		}
		LastAvailStamina_Int = Avail;
	}
}


void UVitalityComponent::InitialiseBaselines()
{
	LastAvailStamina_Int = GetAvailableStamina();
	LastCap_Int       = GetMeterCap();
	bLastFainted     = IsFainted();
}


