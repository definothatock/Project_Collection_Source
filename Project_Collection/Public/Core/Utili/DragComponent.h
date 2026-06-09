// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DragComponent.generated.h"

// ==================== Declares ==================== 

// delta time ~ fps
constexpr float DT_LOWERBOUND = (1.f / 240.f);
constexpr float DT_UPPERBOUND = (1.f / 20.f);

UENUM(BlueprintType)
enum class EDragState : uint8
{
	Undrag,
	Dragging
};

/*
UENUM(BlueprintType)
enum class EUndragReason : uint8
{
	Manual,
	ExceededMaxDistance,
	TargetInvalid,
	NoPhysicBody
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnHoloRopeUnbound, AActor*, UndraggedTarget, EHoloRopeUnbindReason, Reason);
*/

/**
 * Drag and pull for physical objects.
 *
 * Main Function:
 * - Attempts to grab a valid physics body under the player's view and applies forces to move its grab point toward a desired point.
 * - Uses a spring or stable PD-style force based on player strength, target mass, stiffness, damping, and grab distance.
 *
 * State:
 * - Maintains a simple drag state: Undrag or Dragging.
 * - Tracks the grabbed component, bone, local grab point, and desired point for substep physics.
 *
 * Boundary:
 * - Uses the physics substep callback for force application. This yields better stability, but adds some physics latency.
 * - Designed for a minimum stable frame rate around 30 FPS with typical physics substepping.
 * - Releases the drag if the grab point falls too far behind the desired point.
 *
 * Networking:
 * - Drag requests are initiated locally, but drag detection and force application are server-authoritative.
 * - Uses RPCs to start and stop dragging on the server.
 *
 * Reference:
 * - https://www.youtube.com/watch?v=_jRLlTDqoGI
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_COLLECTION_API UDragComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UDragComponent();
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== APIs ====================

	// Request Drag action.
	UFUNCTION(BlueprintCallable, Category = "Drag")
	void Request_StartDrag();

	// Request to Stop Dragging.
	UFUNCTION(BlueprintCallable, Category = "Drag")
	void Request_StopDrag();
	
	// ==================== Configs ==================== 

	// Max force the player can exert.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Strength")
	float Strength = 50000.f;

	// Spring stiffness. Higher = snappier
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float Stiffness = 50.f;

	// 1.0 == critically damped (no overshoot). >1 sluggish, <1 springy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float DampingRatio = 1.0f;
	
	// Stable PD natural frequency (Hz). Used when bUseStablePD == true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning", meta=(ClampMin="0.1"))
	float ResponseHz = 1.5f;
	
	// Damping torque from COM to current target impact point.
	// Note: cannot cancel orthogonal torque. Consider adding another input for rotation manipulation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float AngularDamping = 1.0f;
	
	// How far can trace to start a drag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float MaxGrabDistance = 200.f;

	// If the grab point falls this far behind the desired point, let go.
	// Covers "too heavy", "dragged too hard", and "obstructed".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float ReleaseDistance = 200.f;
	
	// Cap desired-point velocity estimate (feed-forward clamp).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float MaxDesiredSpeed = 2500.f;
	
	// Apply forces from physics substep callback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	bool bUseSubstepForces = true;

	//
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	bool bUseStablePD = true;

	// Wake in rare edge cases like "sleep floating".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	bool bForceWakeWhileDragging = false;

	// Add upward force to compensate gravity; reduce static hover error.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	bool bEnableGravityCompensation = true;

	// If somehow cannot fetch the World
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float BackupGravitationalForce = -980.f;

	// 1.0 = full gravity cancel, 0.0 = off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning", meta=(ClampMin="0.0", ClampMax="2.0"))
	float GravityCompensation = 0.3f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;


	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Debug")
	bool bDebugDraw = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Debug")
	bool bEnableAngularDamping = true;
	
	// ==================== Queries ====================
	
	UFUNCTION(BlueprintPure, Category = "Drag")
	bool IsDragging() const { return State == EDragState::Dragging; }

	// ==================== Internal functions ==================== 

protected:
	// view location + direction.
	bool GetOwnerViewPoint(FVector& OutLocation, FVector& OutDirection) const;

private:
	// Monitor conditions and Call Auth_SubstepDrag().
	// Called per game tick. 
	void Auth_UpdateDrag(float DeltaTime);

	// Default runs in physic thread. Backup runs in game tick.
	void Auth_SubstepApplyDrag(float DeltaTime, FBodyInstance* BodyInstance);

	// Trace and if possible, cache and initialize drag state.
	bool Auth_TryStartDrag(const FVector& ViewLoc, const FVector& ViewDir);

	UFUNCTION(Server, Reliable)
	void RpcServer_TryStartDrag(FVector_NetQuantize ViewLoc, FVector_NetQuantizeNormal ViewDir);

	UFUNCTION(Server, Reliable)
	void RpcServer_StopDrag();


	// ==================== Internal Variables ====================

public:
	TWeakObjectPtr<UPrimitiveComponent> Grabbed;
	FName GrabbedBone = NAME_None;
	
private:
	UPROPERTY(Transient, Replicated)
	EDragState State = EDragState::Undrag;
	
	// Grab point stored in the body's local space so it tracks rotation.
	FVector LocalGrabPoint = FVector::ZeroVector;

	// Distance from the view origin to the grab point at grab time;
	// preserved so the object floats at the same depth.
	float GrabDistance = 0.f;
	
	// Desired point cache (updated on game thread tick, consumed in substeps).
	FVector CachedDesiredPoint = FVector::ZeroVector;
	FVector CachedDesiredVelocity = FVector::ZeroVector;
	FVector PrevDesiredPoint = FVector::ZeroVector;
	bool bHasPrevDesiredPoint = false;

	// Defer stop to game thread if substep detects failure.
	bool bPendingStopDrag = false;


	FCalculateCustomPhysics SubstepDragDelegate;
	
};
