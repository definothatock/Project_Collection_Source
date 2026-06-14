// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DefaultMovementComponent.generated.h"


/* ==================== Declares ==================== */

/* ----- Generals and routing ----- */

// Custom movement mode used to identify current movement state.
UENUM(BlueprintType)
namespace ECustomMovementMode
{
	enum Type
	{
		MOVE_Climb			UMETA(DisplayName = "Climb Mode"),
		MOVE_ClimbLedge		UMETA(DisplayName = "Ledge Climb Mode") // mantle maneuver
	};
}

/* ----- Climb Mode ----- */

constexpr float FallBackCapsuleHalfHeight = 96.f;

UENUM(BlueprintType)
enum class ELedgeClimbMethod : uint8
{
	Coded       UMETA(DisplayName = "Coded (Lerp)"),
	RootMotion  UMETA(DisplayName = "Root Motion (Montage)")
};

// Delegates from the reference, not doing anything. might want to use Multicast instead.
DECLARE_DELEGATE(FOnEnterClimbState)
DECLARE_DELEGATE(FOnExitClimbState)



/*
 * Custom Movement Component with climbing movement mode.
 *
 * Function:
 * -
 *
 * 
 * State:
 *
 *
 * TODO:
 * - Add Rope Climb Mode
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

	// Select custom "physics" (game-thread logic) When in CustomMode, Runs every *game-tick* (NOT phys-tick!).
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
	 * Climbing movement and its derived modes.
	 *
	 * Function:
	 * - @MOVE_Climb: Climbs Surfaces that is at least: available obj type, not too steep, at least eye height.
	 * - @MOVE_ClimbLedge: 
	 *
	 * Rules:
	 * @MOVE_Climb:
	 * - Can start climbing only from grounded state and valid front/eye traces.
	 * - While climbing, movement speed/acceleration are overridden.
	 * - Climb exits when surface is invalid or floor is reached while moving down.
	 * @MOVE_ClimbLedge:
	 *
	 * Workflow:
	 * -
	 * 
	 * State:
	 *
	 * Boundary/Limitation:
	 * - no specific support for moving platforms yet.
	 *
	 * Networking:
	 * - Initial Check and requests are local, Surface detection and movement application are server-authoritative.
	 *
	 * Reference:
	 * - https://github.com/vinceright3/ClimbSystemSourceCode
	 * 
	 * Note:
	 * If ever want to make @MOVE_ClimbLedge support moving platforms, check this video: https://www.youtube.com/watch?v=2sLa4z4nOlI
	 */
	

public:
	/* ==================== APIs ==================== */

	// Climbing Mode Entry Point.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Request_ToggleClimbing(bool bWantsClimb);
	
	
	
	/* ==================== Queries ==================== */
	
	// Returns true when the current movement mode is the MOVE_Climb mode.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsClimbing() const;

	// True while the coded ledge-climb (mantle) maneuver is running.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsLedgeClimbing() const;
	
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

	// Performs a forward line trace from eye height to validate climb surface reachability.
	FHitResult TraceFromEyeHeight(
		float TraceDistance,
		float TraceStartHeightOffset = 0.f,
		bool bShowDebugShape = true,
		bool bDrawPersistantShapes = false
	);
	
	// Traces forward for climbable surfaces and stores the results.
	// Starts from @UpdatedComponent (Character Capsule).
	bool TraceAndCacheClimbableSurfaces();

	// Call TraceFromEyeHeight() and test if there is a surface at the end of Eye Trace 
	bool TraceLedgeTopSurface(FHitResult& OutTopSurfaceHit, FVector& OutForwardProbeEnd, bool bDrawDebug = false);

	
	/* ----- Networking ----- */
	
	UFUNCTION(Server, Reliable)
	void RpcServer_ToggleClimbing(bool bEnableClimb);
	
	// Check Conditions; then Start or Stop Climbing accordingly.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Auth_ToggleClimbing(bool bEnableClimb);

	
	/* ----- Climb Core ----- */
	
	// Returns true if the character is grounded and a valid climbable surface is detected.
	bool CanStartClimbing();

	// Climbing Mode Tigger. Calls OnMovementModeChanged()
	void StartClimbing();

	// Exits climbing mode and transitions to falling.
	void StopClimbing();

	// Applies climbing movement, rotation, and surface snapping each game tick.
	void PhysClimb(float deltaTime, int32 Iterations);

	// Computes averaged climb surface location and normal from trace hits.
	void AveragesClimableSurfaceInfo();

	// Determines whether the current surface is too flat to continue climbing.
	bool CheckShouldStopClimbing();

	// Detects when the character has reached a floor below while descending.
	bool CheckHasReachedFloor();
	
	// Interpolates the component rotation to align with the climbable surface.
	FQuat Climb_CalculateSurfaceAlignedRot(float DeltaTime);
	
	// Estimate distance and pushes the character toward the climbable surface to maintain contact.
	void Climb_SnapMovementToSurfaces(float DeltaTime);
	

	/* ----- Climb Ledge ----- */
	
	// Detects when the character has reached a ledge that can be mantled up to.
	// Calls TraceLedgeTopSurface().
	bool CheckReachingLedge();
	
	// Computes the final capsule rest location on top of the ledge.
	// Returns false if there is no valid, walkable top surface to mantle onto.
	bool CalcLedgeClimbTarget(FVector& OutLandLocation);

	// Validates the target, builds the L-shaped path and switches into MOVE_LedgeClimb.
	// Change CMC Configs for walking-surface related things
	void Auth_TryStartLedgeClimb();

	// Drives the capsule along the path from CalcLedgeClimbTarget(); each game tick.
	// Lerp up, then forward; use @LedgeClimb_PhaseSplit to segregate progress.
	void PhysLedgeClimb(float deltaTime, int32 Iterations);
	
	// Currently not used. Might just let player falls.
	bool CanClimbDownLedge();

	
	
	/* ==================== Runtime State ==================== */
	
	/* ----- Climb Core ----- */
	
	// Latest hits detected from climbable surface traces.
	// Updated by TraceClimbableSurfaces().
	TArray<FHitResult> Climb_ClimableSurfaceMultiTracedResults;

	// Average location of the currently detected climbable surface.
	FVector Climb_CurrentSurfaceLocation = FVector::ZeroVector;

	// Average normal of the currently detected climbable surface.
	FVector Climb_CurrentSurfaceNormal = FVector::ZeroVector;

	// Cached default capsule half-height so it can be restored after climbing.
	UPROPERTY(Transient)
	float DefaultCapsuleHalfHeight = 0.f;
	
	// True while bleeding off a high entry velocity after grabbing the Surface (faster than a normal climb).
	// While true, PhysClimb decelerates Velocity MANUALLY and ignores player input,
	// so the slide is governed purely by the entry velocity.
	// Otherwise, CalcVelocity would clamp to Climb_MaxSpeed.
	bool bClimb_IsEntrySliding = false;
	
	// Timestamp (world seconds) of the last MANUAL climb stop. Used to reject an
	// immediate re-entry during the brief Falling window right after toggling off.
	double Climb_LastManualStopTime = -1.0;
	

	/* ----- Climb Ledge ----- */

	// World space of where the ClimbLedge starts
	FVector LedgeClimb_StartLocation = FVector::ZeroVector;

	// World space of 
	FVector LedgeClimb_OverLedgeLocation = FVector::ZeroVector;

	 // final capsule rest location on the surface
	FVector LedgeClimb_TargetLocation = FVector::ZeroVector;

	// Target upright rotation we interp to while mantling (faces across the top surface).
	FQuat LedgeClimb_TargetRotation = FQuat::Identity;

	// Normalised progress 0..1 along the whole maneuver.
	float LedgeClimb_Alpha = 0.f;

	
	
	/* ==================== Config ==================== */

	
	/* ----- Tracing ----- */
	
	// Object types that are considered climbable during trace queries.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ClimbableSurfaceTraceTypes;

	// Radius of the capsule used to detect climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleTraceRadius = 50.f;

	// Half-height of the capsule used for climbing surface tracing.
	// ANCHOR: Should unify half-heights later.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleTraceHalfHeight = 72.f;
	
	// Distance for the eye-level forward trace used to validate climbable surfaces.
	// ANCHOR: Should Change to trace frm camera later.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_EyeHeightTraceDistance = 100.f;

	// Distance to offset the start of the forward climb trace from the character.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ForwardTraceStartOffset = 30.f;

	// Forward distance used to search for climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ForwardTraceDistance = 50.f;

	//
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category = "CustomMovement|Climbing|Tracing",meta = (AllowPrivateAccess = "true"))
	float Climb_DownWalkableSurfaceTraceOffset = 100.f;

	//
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category = "CustomMovement|Climbing|Tracing",meta = (AllowPrivateAccess = "true"))
	float Climb_DownLedgeTraceOffset = 50.f;

	
	/* ----- Climb Core ----- */
	
	// Master toggle for the climb debug draws.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true"))
	bool bClimb_DebugDraw = false;
	
	// Master toggle for the climb debug draws.
	// ANCHOR: too many action calling the same tracings. Should make separate Tracing Config.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true"))
	bool bClimb_MasterDebugTrace = false;

	// Master toggle for the climb debug loggings.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true"))
	bool bClimb_DebugLog = false;

	// Deceleration used when the climb input is released or when coming to a stop.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxBreakDeceleration = 400.f;

	// Maximum allowed speed while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxSpeed = 100.f;

	// Maximum allowed acceleration while climbing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxAcceleration = 300.f;

	// Stop Climbing when Surface normal is smaller that this from Up Vector (surface too horizontal).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_StopClimbAngleFromUpDeg = 40.f;
	
	// Capsule half-height applied to the character during climbing.
	// ANCHOR: Might overlapped with @DefaultCapsuleHalfHeight / should fetch instead of hardcoded.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_CapsuleHalfHeight = 48.f;

	/* ----- Climb Entry Slide ----- */
	
	// Hard cap on the velocity carried into climbing.
	// Stops a huge fall from turning into an absurd slide. Set very high to effectively disable.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Slide", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_MaxEntrySlideSpeed = 8191.f;

	// Threshold of starting a slide when faster that this much from @Climb_MaxEntrySlideSpeed
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Slide", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float SlideEntryOverspeedMultiplier = 1.5f;
	
	// Deceleration (u/s^2) applied during the entry slide.
	// NOT the same with, Climb_MaxBreakDeceleration, this is about "caught the wall while falling"
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Slide", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_EntrySlideDeceleration = 900.f;

	// Spamming safeguard; ANCHOR: implement later.
	// Save player from "panicked spam" after success.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ExitLockoutSeconds = 0.25f;
	
	// Spamming safeguard; ignore start requests for this long after a manual StopClimbing().
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ReEntryLockoutSeconds = 0.25f;
	
	
	/* ----- Climb Ledge ----- */

	// Total time (seconds) for the whole mantle.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true", ClampMin="0.05"))
	float LedgeClimb_Duration = 0.6f;

	// Fraction of the duration spent on the vertical (rise) segment. 0.5 = half up, half over.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true", ClampMin="0.05", ClampMax="0.95"))
	float LedgeClimb_PhaseSplit = 0.7f;

	// How far above the landing location the rise segment overshoots
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float LedgeClimb_VerticalClearance = 10.f;

	// Extra forward distance onto the surface so the capsule lands further in.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true"))
	float LedgeClimb_ForwardLandOffset = 0.f;

	// Vertical offset added to the eye-height forward probe used to find the ledge top.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true"))
	float LedgeClimb_TopProbeUpOffset = 50.f;

	// How far down we probe from above the lip to find the walkable top surface.
	// ANCHOR: Consider change this to Full capsule height with a bit of padding.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float LedgeClimb_TopProbeDownDistance = 150.f;

	// ANCHOR: For future use, switching to root motion. 
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true"))
	ELedgeClimbMethod LedgeClimbMethod = ELedgeClimbMethod::Coded;
};
