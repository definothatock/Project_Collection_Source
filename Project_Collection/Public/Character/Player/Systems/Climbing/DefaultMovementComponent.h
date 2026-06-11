// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DefaultMovementComponent.generated.h"


/* ==================== Declares ==================== */

/* ----- Climb Mode ----- */

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

// Delegates from the reference, not doing anything. might want to use Multicast instead.
DECLARE_DELEGATE(FOnEnterClimbState)
DECLARE_DELEGATE(FOnExitClimbState)



/*
 * Custom Movement Component with climbing movement mode.
 *
 * Function:
 * -
 *
 * Rules:
 * 
 * State:
 *
 * Boundary:
 *
 * Networking:
 *
 * Notice:
 * 
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_COLLECTION_API UDefaultMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	
	/**
	 * ====================================================================
	 * ==================== Component Core and Routing ====================
	 * ====================================================================
	 */
	
public:
	UDefaultMovementComponent();

	
	/* ==================== Overridden Functions ==================== */
protected:
	// Get snapshots
	virtual void BeginPlay() override;
	
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Handles entering and exiting the custom climbing movement mode.
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	// Executes custom "physics" (game-thread logic) When in CustomMode, Runs every *game-tick* (NOT phys-tick!).
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	// Returns a climb-specific max speed depending on mode
	virtual float GetMaxSpeed() const override;

	// Returns a climb-specific max acceleration depending on mode
	virtual float GetMaxAcceleration() const override;


	
	/**
	 * =============================================================
	 * ==================== Climb Movement Mode ====================
	 * =============================================================
	 *
	 *
	 * Lightweight climbing movement mode, with basic functionality only.
	 *
	 * Function:
	 * - Climbs Surfaces that is at least: available obj type, not too steep, at least eye height.
	 *
	 * Rules:
	 * - Can start climbing only from grounded state and valid front/eye traces.
	 * - While climbing, movement speed/acceleration are overridden.
	 * - Climb exits when surface is invalid or floor is reached while moving down.
	 *
	 * Workflow:
	 * -
	 * 
	 * State:
	 *
	 * Boundary/Limitation:
	 *
	 * Networking:
	 * - Initial Check and requests are local, Surface detection and movement application are server-authoritative.
	 *
	 * Reference:
	 * - https://github.com/vinceright3/ClimbSystemSourceCode
	 *
	 * Notice:
	 * All animation related codes were stripped from the original tutorial.
	 */
	

public:
	/* ==================== APIs ==================== */

	// Climbing Mode Entry Point.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Request_ToggleClimbing(bool bEnableClimb);
	
	
	
	/* ==================== Queries ==================== */
	
	// Returns true when the current movement mode is the custom climb mode.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsClimbing() const;
	
	// Gets the current velocity in local component space before component rotation is applied.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	FVector GetLocalSpaceVelocity() const;

	// Returns the normal of the currently detected climbable surface.
	FORCEINLINE FVector GetClimbableSurfaceNormal() const { return Climb_CurrentSurfaceNormal; }

	
	
	/* ==================== Delegates ==================== */

	// Delegate called when climbing begins.
	FOnEnterClimbState OnEnter_ClimbStateDelegate;

	// Delegate called when climbing ends.
	FOnExitClimbState OnExit_ClimbStateDelegate;


	
private:
	/* ==================== Internal Function ==================== */
	
	/* ----- Traces ----- */
	
	// Performs a capsule trace against climbable object types and returns all hits.
	TArray<FHitResult> DoCapsuleTraceMultiByObject(
		const FVector& Start,
		const FVector& End,
		bool bShowDebugShape = true,
		bool bDrawPersistantShapes = false
	);

	// Performs a single line trace against climbable object types and returns the first hit.
	FHitResult DoLineTraceSingleByObject(
		const FVector& Start,
		const FVector& End,
		bool bShowDebugShape = true,
		bool bDrawPersistantShapes = false
	);
	
	// Traces forward for climbable surfaces and stores the results; Starts from @UpdatedComponent (Character Capsule)
	bool TraceClimbableSurfaces();

	// Performs a forward line trace from eye height to validate climb surface reachability.
	// Does not use ForwardTraceStartOffset.
	// ISSUE: inconsistent trace Offset across functions. name or unify.
	FHitResult TraceFromEyeHeight(
		float TraceDistance,
		float TraceStartOffset = 0.f,
		bool bShowDebugShape = true,
		bool bDrawPersistantShapes = false
	);
	
	/* ----- Networking ----- */
	
	UFUNCTION(Server, Reliable)
	void RpcServer_ToggleClimbing(bool bEnableClimb);
	
	// Check Conditions; then Start or Stop Climbing accordingly.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Auth_ToggleClimbing(bool bEnableClimb);
	
	/* ----- Core ----- */
	
	// Returns true if the character is grounded and a valid climbable surface is detected.
	bool CanStartClimbing();
	
	bool CanClimbDownLedge();

	// Climbing Mode Tigger. Calls OnMovementModeChanged()
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

	//
	bool CheckHasReachedLedge();
	
	// Interpolates the component rotation to align with the climbable surface.
	FQuat Climb_CalculateSurfaceAlignedRot(float DeltaTime);
	
	// Pushes the character toward the climbable surface to maintain contact.
	void Climb_SnapMovementToSurfaces(float DeltaTime);





	
	
	/* ==================== Runtime State ==================== */

	// Latest hits detected from climbable surface traces.
	// Updated by DoCapsuleTraceMultiByObject().
	TArray<FHitResult> Climb_ClimableSurfaceMultiTracedResults;

	// Average location of the currently detected climbable surface.
	FVector Climb_CurrentSurfaceLocation = FVector::ZeroVector;

	// Average normal of the currently detected climbable surface.
	FVector Climb_CurrentSurfaceNormal = FVector::ZeroVector;

	// Cached default capsule half-height so it can be restored after climbing.
	UPROPERTY(Transient)
	float DefaultCapsuleHalfHeight = 0.f;


	
	/* ==================== Config ==================== */

	/* ----- Tracing ----- */
	
	// Object types that are considered climbable during trace queries.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ClimbableSurfaceTraceTypes;

	// Radius of the capsule used to detect climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleTraceRadius = 50.f;

	// Half-height of the capsule used for climbing surface tracing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleTraceHalfHeight = 72.f;
	
	// Distance for the eye-level forward trace used to validate climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_EyeHeightTraceDistance = 100.f;

	// Distance to offset the start of the forward climb trace from the character.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ForwardTraceStartOffset = 30.f;

	// Forward distance used to search for climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ForwardTraceDistance = 50.f;

	//
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category = "Character Movement: Climbing",meta = (AllowPrivateAccess = "true"))
	float Climb_DownWalkableSurfaceTraceOffset = 100.f;

	//
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category = "Character Movement: Climbing",meta = (AllowPrivateAccess = "true"))
	float Climb_DownLedgeTraceOffset = 50.f;

	/* ----- Locomotion ----- */

	// Deceleration used when the climb input is released or when coming to a stop.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxBreakDeceleration = 400.f;

	// Maximum allowed speed while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxSpeed = 100.f;

	// Maximum allowed acceleration while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxAcceleration = 300.f;

	// Maximum allowed acceleration while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxSurfaceDegree= 60.f;
	
	// Capsule half-height applied to the character during climbing.
	// ANCHOR: Might overlapped with @DefaultCapsuleHalfHeight / should fetch instead of hardcoded.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Config", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleHalfHeight = 48.f;

};
