// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ClimbingMovementComponent.generated.h"


/* ==================== Declares ==================== */

constexpr float FallBackCapsuleHalfHeight = 96.f;

// Custom movement mode used to identify the climb physics state.
UENUM(BlueprintType)
namespace ECustomMovementMode
{
	enum Type
	{
		MOVE_Climb UMETA(DisplayName = "Climb Mode")
	};
}

DECLARE_DELEGATE(FOnEnterClimbState)
DECLARE_DELEGATE(FOnExitClimbState)



/*
 * Lightweight climbing movement mode, Currently for testing only.
 *
 * Main Function:
 * -
 *
 * Main Rules:
 * - Can start climbing only from grounded state and valid front/eye traces.
 * - While climbing, movement speed/acceleration are overridden.
 * - Climb exits when surface is invalid or floor is reached while moving down.
 * 
 * State:
 * - Default UE movement modes
 * - Custom mode: ECustomMovementMode::MOVE_Climb
 *
 * Boundary:
 * - Uses object-type traces (configurable) to detect climbable surfaces.
 *
 * Networking:
 * - No custom replication/RPC in this stripped version.
 *
 * Reference:
 * - https://github.com/vinceright3/ClimbSystemSourceCode
 *
 * Notice:
 * All animation related codes were stripped from the original tutorial.
 * 
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_COLLECTION_API UClimbingMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
	
public:
	UClimbingMovementComponent();

	
protected:
	
	/* ==================== Overridden Functions ==================== */

	// Get snapshots
	virtual void BeginPlay() override;
	
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Handles entering and exiting the custom climbing movement mode.
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	// When climbing: Executes custom physics
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	// When climbing: Returns a climb-specific max speed
	virtual float GetMaxSpeed() const override;

	// When climbing: Returns a climb-specific max acceleration
	virtual float GetMaxAcceleration() const override;

	
public:
	
	/* ==================== APIs ==================== */

	// Enable or disable climbing depending on the requested state.
	UFUNCTION(BlueprintCallable, Category="Character Movement: Climbing")
	void ToggleClimbing(bool bEnableClimb);

	// Returns true when the current movement mode is the custom climb mode.
	UFUNCTION(BlueprintPure, Category="Character Movement: Climbing")
	bool IsClimbing() const;

	
	/* ==================== Queries ==================== */

	// Gets the current velocity in local component space before component rotation is applied.
	UFUNCTION(BlueprintPure, Category="Character Movement: Climbing")
	FVector GetUnrotatedClimbVelocity() const;

	// Returns the normal of the currently detected climbable surface.
	FORCEINLINE FVector GetClimbableSurfaceNormal() const { return CurrentClimbableSurfaceNormal; }

	
	/* ==================== Delegates ==================== */

	// Delegate called when climbing begins.
	FOnEnterClimbState OnEnterClimbStateDelegate;

	// Delegate called when climbing ends.
	FOnExitClimbState OnExitClimbStateDelegate;

	
private:
	
	/* ==================== Internal Function ==================== */

	/* ----- Traces ----- */
	
	// Performs a capsule trace against climbable object types and returns all hits.
	TArray<FHitResult> DoCapsuleTraceMultiByObject(
		const FVector& Start,
		const FVector& End,
		bool bShowDebugShape = false,
		bool bDrawPersistantShapes = false
	);

	// Performs a single line trace against climbable object types and returns the first hit.
	FHitResult DoLineTraceSingleByObject(
		const FVector& Start,
		const FVector& End,
		bool bShowDebugShape = false,
		bool bDrawPersistantShapes = false
	);
	
	// Traces forward for climbable surfaces and stores the results.
	bool TraceClimbableSurfaces();

	// Performs a forward line trace from eye height to validate climb surface reachability.
	FHitResult TraceFromEyeHeight(
		float TraceDistance,
		float TraceStartOffset = 0.f,
		bool bShowDebugShape = false,
		bool bDrawPersistantShapes = false
	);
	
	/* ----- Core ----- */

	// Returns true if the character is grounded and a valid climbable surface is detected.
	bool CanStartClimbing();

	// Switches the movement component into climbing mode.
	void StartClimbing();

	// Exits climbing mode and transitions to falling.
	void StopClimbing();

	// Applies climbing movement, rotation, and surface snapping each physics tick.
	void PhysClimb(float deltaTime, int32 Iterations);

	// Computes averaged climb surface location and normal from trace hits.
	void ProcessClimableSurfaceInfo();

	// Determines whether the current surface is too flat to continue climbing.
	bool CheckShouldStopClimbing();

	// Detects when the character has reached a floor below while descending.
	bool CheckHasReachedFloor();

	// Interpolates the component rotation to align with the climbable surface.
	FQuat GetClimbRotation(float DeltaTime);

	// Pushes the character toward the climbable surface to maintain contact.
	void SnapMovementToClimableSurfaces(float DeltaTime);

	
	/* ==================== Runtime State ==================== */

	// Latest hits detected from climbable surface traces.
	TArray<FHitResult> ClimbableSurfacesTracedResults;

	// Average location of the currently detected climbable surface.
	FVector CurrentClimbableSurfaceLocation = FVector::ZeroVector;

	// Average normal of the currently detected climbable surface.
	FVector CurrentClimbableSurfaceNormal = FVector::ZeroVector;

	// Cached default capsule half-height so it can be restored after climbing.
	UPROPERTY(Transient)
	float DefaultCapsuleHalfHeight = 0.f;

	
	/* ==================== Config ==================== */

	// Object types that are considered climbable during trace queries.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ClimbableSurfaceTraceTypes;

	// Radius of the capsule used to detect climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ClimbCapsuleTraceRadius = 50.f;

	// Half-height of the capsule used for climbing surface tracing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ClimbCapsuleTraceHalfHeight = 72.f;

	// Deceleration used when the climb input is released or when coming to a stop.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MaxBreakClimbDeceleration = 400.f;

	// Maximum allowed speed while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MaxClimbSpeed = 100.f;

	// Maximum allowed acceleration while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MaxClimbAcceleration = 300.f;

	// Capsule half-height applied to the character during climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ClimbingCapsuleHalfHeight = 48.f;

	// Distance for the eye-level forward trace used to validate climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float EyeHeightTraceDistance = 100.f;

	// Distance to offset the start of the forward climb trace from the character.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ForwardTraceStartOffset = 30.f;

	// Forward distance used to search for climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Movement: Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ForwardTraceDistance = 30.f;
	

	/* ==================== Helpers ==================== */

};
