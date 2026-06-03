// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DragComponent.generated.h"

// ==================== Declares ==================== 

UENUM(BlueprintType)
enum class EDragState : uint8
{
	Undrag,
	Dragging
};

/*
UENUM(BlueprintType)
enum class EHoloRopeUnbindReason : uint8
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
 * Drag and pull for physical objects
 * 
 * Main Function:
 * Attempts to drag the object. If allowed, a force will try to push the target into the desired point.
 * Dragging force is related to the player's strength, target's mass, and the distance from the desired point.
 *
 * State:
 * Dragging <-> Un-drag
 * cache
 *
 * Boundary:
 * Physical manipulation, not "pickup".
 * For Configs only Strength cant be adjusted independently.
 *
 * Networking:
 * Ser-Auth Force Application.
 * Replicates: 
 * 
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

	// Trace from the current view and try to grab whatever physics body is hit.
	UFUNCTION(BlueprintCallable, Category = "Drag")
	void Request_StartDrag();

	UFUNCTION(BlueprintCallable, Category = "Drag")
	void Request_StopDrag();
	
	// ==================== Configs ==================== 

	// Max force the player can exert (saturation limit). This is the "strength".
	// Heavier targets accelerate less under the same cap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Strength")
	float Strength = 50000.f; // cm-based UE force units; tune to taste

	// Spring stiffness. Higher = snappier tracking.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float Stiffness = 60.f;

	// 1.0 == critically damped (no overshoot). >1 sluggish, <1 springy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float DampingRatio = 0.5f; // rename to linear later

	// higher = settles faster
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float AngularDamping = 1.0f;

	// How far we can trace to start a drag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float MaxGrabDistance = 200.f;

	// If the grab point falls this far behind the desired point, let go.
	// Covers "too heavy", "dragged too hard", and "obstructed".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drag|Tuning")
	float ReleaseDistance = 200.f;

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
	// Resolve the "desired View point": view location + direction.
	bool GetOwnerViewPoint(FVector& OutLocation, FVector& OutDirection) const;

private:
	void Auth_UpdateDrag(float DeltaTime);

	bool Auth_TryStartDrag(const FVector& ViewLoc, const FVector& ViewDir);

	UFUNCTION(Server, Reliable)
	void RpcServer_TryStartDrag(FVector_NetQuantize ViewLoc, FVector_NetQuantizeNormal ViewDir);

	UFUNCTION(Server, Reliable)
	void RpcServer_StopDrag();


	// ==================== Internal Variables ====================

public:
	// Cached grab data
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
};
