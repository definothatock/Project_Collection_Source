// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HoloRopeBindingComponent.generated.h"

// ==================== Declares ==================== 

class UPrimitiveComponent;
class UCurveFloat;

UENUM(BlueprintType)
enum class EHoloRopeUnbindReason : uint8
{
	Manual,
	ExceededMaxDistance,
	TargetInvalid,
	BinderInvalid,
	NoPhysicBody
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHoloRopeUnbound, AActor*, UnboundTarget, EHoloRopeUnbindReason, Reason);


/**
 * On-demand "holo-rope" binding.s
 * 
 * Main Function:
 * Pulls a bound target toward this component's owner (the binder) with a distance-scaled force, unbinds once the gap
 * exceeds SlackDistance.
 * Prefers applying force on named socket if provided.
 *
 * State:
 * unbound <-> bound
 * cache target and socket
 *
 * Boundary:
 * Does not refresh input data once cached.
 * No hard dependency on the owner's class.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECT_COLLECTION_API UHoloRopeBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UHoloRopeBindingComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	
	// ==================== APIs ====================
	
public:	
	/**
	 * Bind a target actor to this binder.
	 * @param InTarget        The actor to be pulled.
	 * @param InTargetSocket  Optional socket on the target to anchor to.
	 * @param InBinderSocket  Optional socket on the binder to anchor from.
	 * @return true if binding succeeded (valid target with a simulating body).
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloRope")
	bool HoloRope_Bind(AActor* InTarget,
			  FName InTargetSocket = NAME_None,
			  FName InBinderSocket = NAME_None);

	/** Release the current binding. Safe to call when not bound. */
	UFUNCTION(BlueprintCallable, Category = "HoloRope")
	void HoloRope_Unbind(EHoloRopeUnbindReason Reason = EHoloRopeUnbindReason::Manual);

	
	// ==================== Configs ==================== 

	
	/** Below this distance the rope is "slack": no force is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope", meta = (ClampMin = "0.0"))
	float SlackDistance = 300.f;

	/** Beyond this distance the binding is automatically released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope", meta = (ClampMin = "0.0"))
	float MaxDistance = 500.f;

	/** Peak pull force (reached at MaxDistance). Interpretation depends on bForceIsAcceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope", meta = (ClampMin = "0.0"))
	float MaxPullForce = 10000.f;

	/**
	 * Optional shaping curve mapping normalized overshoot [0..1] -> force scale.
	 * If null, a linear ramp is used. Use this to make the pull soft near the
	 * slack edge and firmer near max range.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope")
	TObjectPtr<UCurveFloat> ForceCurve = nullptr;

	// If true, force is mass-independent (treated as acceleration).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope")
	bool bForceIsAcceleration = false;

	// If true, auto-unbind when distance exceeds MaxDistance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope")
	bool bAutoUnbindOnMaxDistance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HoloRope|Debug")
	bool bDebugDraw = false;
	
	// ==================== Queries ==================== 

	
public:	
	UFUNCTION(BlueprintPure, Category = "HoloRope")
	bool IsBound() const;

	UFUNCTION(BlueprintPure, Category = "HoloRope")
	AActor* GetBoundTarget() const;

	/** Current live distance between the two anchor points. -1 if not bound. */
	UFUNCTION(BlueprintPure, Category = "HoloRope")
	float GetCurrentDistance() const;
	
	/** Fired whenever a binding ends, for any reason. */
	UPROPERTY(BlueprintAssignable, Category = "HoloRope")
	FOnHoloRopeUnbound OnHoloRopeUnbound;

	
	// ==================== Internal functions ==================== 

	
protected:
	// Per-frame pull. Reads only the current distance
	void ApplyPullForce(const FVector& BinderLoc, const FVector& TargetLoc, float Distance);

	FVector GetBinderAnchorLocation() const;
	FVector GetTargetAnchorLocation() const;
	// Search for first scene component that owns the named socket.
	USceneComponent* FindComponentWithSocket(const AActor* Actor, FName SocketName) const;
	void CacheAnchorComponents();

	// Find a primitive component on the target that is simulating physics.
	UPrimitiveComponent* ResolveTargetPhyBody() const;

	void SetBoundTickEnabled(bool bEnabled);

	
	// ==================== Internal Variables ====================

	
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> BoundTarget;

	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> BoundTargetPhyBody;

	// Cache
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> BinderAnchorComp;
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> TargetAnchorComp;
	FName TargetSocketName = NAME_None;
	FName BinderSocketName = NAME_None;
};
