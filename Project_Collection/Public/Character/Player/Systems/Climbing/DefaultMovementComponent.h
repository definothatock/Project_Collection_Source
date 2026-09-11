// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DefaultMovementComponent.generated.h"


/* ==================== Declares ==================== */

class UClimbWorldStaticRopeComponent;

/* ----- Generals and routing ----- */

// Custom movement mode used to identify current movement state.
UENUM(BlueprintType)
namespace ECustomMovementMode
{
	enum Type
	{
		MOVE_Climb			UMETA(DisplayName = "Climb Mode"),
		MOVE_RopeClimb		UMETA(DisplayName="Rope Climb Mode"),
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



/**
 * Custom Movement Component with climbing movement mode
 *
 * Function:
 * -
 *
 * 
 * State:
 *
 *
 * Note:
 * Component is set Replicated to broadcast some necessary runtime data.
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// Handles entering and exiting the custom climbing movement mode.
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	// Select custom "physics" (game-thread logic) When in CustomMode, Runs every *game-tick* (NOT phys-tick!).
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

	// Returns a climb-specific max speed depending on mode
	virtual float GetMaxSpeed() const override;

	// Returns a climb-specific max acceleration depending on mode
	virtual float GetMaxAcceleration() const override;


	
	/**
	 * =============================================================
	 * ==================== Climbing Movement Modes ====================
	 * =============================================================
	 *
	 * Climbing locomotion and related custom modes (wall, ledge, rope).
	 *
	 * Modes:
	 * - MOVE_Climb
	 *   Wall-climb locomotion — used when the character is attached to a climbable surface.
	 * - MOVE_ClimbLedge
	 *   Ledge / mantle transition (coded L-path). RootMotion option is declared but not implemented.
	 * - MOVE_RopeClimb
	 *   Planned rope-climb mode (declared but not implemented).
	 *
	 * -----------------------------------------------------------------
	 * Quick State Flow (authoritative path)
	 * -----------------------------------------------------------------
	 * Request_ToggleClimbing(true)
	 *	-> (Client) RpcServer_ToggleClimbing(true)
	 *	-> Auth_ToggleClimbing(true)
	 *		-> Re-entry lockout check
	 *		-> CanStartClimbing()
	 *			- forward capsule trace hits climbable object type
	 *			- eye-height forward trace hits
	 *		-> StartClimbing()
	 *			- SetMovementMode(MOVE_Custom, MOVE_Climb)
	 *
	 * MOVE_Climb (PhysClimb each tick)
	 *	-> TraceAndCacheClimbableSurfaces()
	 *	-> AveragesClimableSurfaceInfo()
	 *	-> Stop if:
	 *		- surface invalid / too horizontal (CheckShouldStopClimbing), or
	 *		- floor reached while descending (CheckHasReachedFloor)
	 *	-> Movement:
	 *		- entry slide decel (high-speed catch), OR normal CalcVelocity
	 *		- SafeMoveUpdatedComponent + SlideAlongSurface
	 *		- Snap toward wall (Climb_SnapMovementToSurfaces)
	 *	-> If ledge detected (CheckReachingLedge):
	 *		- Auth_TryStartLedgeClimb()
	 *		- SetMovementMode(MOVE_Custom, MOVE_ClimbLedge)
	 *
	 * MOVE_ClimbLedge (PhysLedgeClimb each tick)
	 *	-> Move Start -> OverLedge -> Target (time-normalized lerp)
	 *	-> Interp to upright target rotation
	 *	-> On completion: SetMovementMode(MOVE_Walking)
	 *
	 * Request_ToggleClimbing(false)
	 *	-> Auth_ToggleClimbing(false) -> StopClimbing() -> MOVE_Falling
	 *
	 * -----------------------------------------------------------------
	 * Rules / Behavior
	 * -----------------------------------------------------------------
	 * - Start is allowed from ground OR air (midair grab supported), if traces are valid.
	 * - Climb mode overrides max speed/acceleration.
	 * - Entering climb with high velocity can trigger entry slide bleed-off.
	 * - Capsule half-height shrinks during climb and is restored on exit.
	 * - Climb and ledge use the same climb session enter/exit delegates.
	 *
	 * -----------------------------------------------------------------
	 * State Side Effects (OnMovementModeChanged)
	 * -----------------------------------------------------------------
	 * - Enter MOVE_Climb:
	 *		disable orient-to-movement, shrink capsule, initialize slide/session state.
	 * - Exit MOVE_Climb (except when transitioning to MOVE_ClimbLedge):
	 *		restore capsule, restore upright rotation policy, clear slide state.
	 * - Exit MOVE_ClimbLedge:
	 *		restore capsule + upright rotation, stop residual velocity.
	 *
	 * -----------------------------------------------------------------
	 * Networking
	 * -----------------------------------------------------------------
	 * - Input intent can be requested locally.
	 * - Start/stop authority, trace validation, and movement mode transitions are server-authoritative.
	 *
	 * -----------------------------------------------------------------
	 * Boundary / Limitations
	 * -----------------------------------------------------------------
	 * - No dedicated moving-platform support for climb/ledge yet.
	 * - RootMotion ledge climb path is declared but currently unimplemented.
	 * - Some trace calls are intentionally duplicated for safety; can be consolidated later.
	 *
	 * Reference:
	 * - https://github.com/vinceright3/ClimbSystemSourceCode
	 * - Moving platform note (future): https://www.youtube.com/watch?v=2sLa4z4nOlI
	 */
	

public:
	/* ==================== APIs ==================== */

	// Climbing Mode Entry Point.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Request_ToggleClimbing(bool bWantsClimb);
	
	// Attempt to attach to a specific active rope.
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Request_StartRopeClimb(UClimbWorldStaticRopeComponent* InRope);
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Request_StopRopeClimb();

	UFUNCTION(BlueprintCallable, Category="CustomMovement|Input")
	void Request_MoveIntent(const FVector2D& InMoveIntent);
	
	/* ==================== Queries ==================== */
	
	// Returns true when the current movement mode is the MOVE_Climb mode.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsClimbing() const;

	// True while the coded ledge-climb (mantle) maneuver is running.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsLedgeClimbing() const;

	// True while attached to and moving around a rope.
	UFUNCTION(BlueprintPure, Category="CustomMovement|Climbing")
	bool IsRopeClimbing() const;
	
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
	) const;

	// Performs a single line trace against climbable object types and returns the first hit.
	FHitResult DoLineTraceSingleByObject(
		const FVector& Start,
		const FVector& End,
		bool bShowDebugShape = false,
		bool bDrawPersistantShapes = false
	) const;

	// Performs a forward line trace from eye height to validate climb surface reachability.
	FHitResult TraceFromEyeHeight(
		float TraceDistance,
		float TraceStartHeightOffset = 0.f,
		bool bShowDebugShape = true,
		bool bDrawPersistantShapes = false
	) const;
	
	// Traces forward for climbable surfaces and stores the results; from @UpdatedComponent (Character Capsule).
	bool TraceAndCacheClimbableSurfaces();

	// Call TraceFromEyeHeight() and test if there is a surface at the end of Eye Trace 
	bool TraceLedgeTopSurface(FHitResult& OutTopSurfaceHit, FVector& OutForwardProbeEnd, bool bDrawDebug = false);
	
	/* ----- Networking ----- */
	
	UFUNCTION(Server, Reliable)
	void RpcServer_ToggleClimbing(bool bEnableClimb);

	UFUNCTION(Server, Reliable)
	void RpcServer_StartRopeClimb(UClimbWorldStaticRopeComponent* InRope);
	UFUNCTION(Server, Reliable)
	void RpcServer_StopRopeClimb();
	
	/* ----- Climb Core ----- */
	
	UFUNCTION(BlueprintCallable, Category="CustomMovement|Climbing")
	void Auth_ToggleClimbing(bool bEnableClimb);
	
	// True while grounded and valid climbable surface detected.
	bool CanStartClimbing();

	void StartClimbing();
	void StopClimbing();

	// Applies climbing movement, rotation, and surface snapping each game tick.
	void PhysClimb(float DeltaTime, int32 Iterations);

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
	
	/* ----- CLimb Rope ----- */

	bool Auth_StartRopeClimb(UClimbWorldStaticRopeComponent* InRope);
	void Auth_StopRopeClimb();

	void PhysRopeClimb(float DeltaTime, int32 Iterations);

	// Retrieves a point/tangent along the current rope plus a stable radial orbit basis.
	bool GetRopeClimbFrameAtDistance(
		float InDistanceAlongRope,
		FVector& OutRopeLocation,
		FVector& OutRopeTangent,
		FVector& OutReferenceRadial,
		FVector& OutOrbitRight
	) const;

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
	void PhysLedgeClimb(float DeltaTime, int32 Iterations);
	
	// Currently not used. Might just let player falls.
	bool CanClimbDownLedge();
	
	/* ----- Climb Exit ----- */

	void ExitUprightBlend();
	
	void TickExitUprightBlend(float DeltaTime);
	
	/* ==================== Runtime State ==================== */

	FVector2D CustomMovementInputIntent = FVector2D::ZeroVector;
	
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


	/* ----- CLimb Rope ----- */

	UPROPERTY(Transient, Replicated)
	TObjectPtr<UClimbWorldStaticRopeComponent> RopeClimb_Rope = nullptr;

	UPROPERTY(Transient, Replicated)
	float RopeClimb_DistanceAlongRope = 0.f;

	UPROPERTY(Transient, Replicated)
	float RopeClimb_OrbitAngleRadians = 0.f;

	/* ----- Climb Ledge ----- */
	/*
	 * Content in this section are replicated for client to perform CMC velocity prediction to
	 * specific destinations (the L-shape).
	 */

	// World space of where the ClimbLedge starts
	UPROPERTY(Replicated)
	FVector_NetQuantize10  LedgeClimb_StartLocation = FVector::ZeroVector;

	// World space of
	UPROPERTY(Replicated)
	FVector_NetQuantize10  LedgeClimb_OverLedgeLocation = FVector::ZeroVector;

	 // final capsule rest location on the surface
	UPROPERTY(Replicated)
	FVector_NetQuantize10  LedgeClimb_TargetLocation = FVector::ZeroVector;

	// Target upright rotation we interp to while mantling (faces across the top surface).
	UPROPERTY(Replicated)
	FQuat LedgeClimb_TargetRotation = FQuat::Identity;

	// server-authored start time used by all peers to compute same alpha
	UPROPERTY(Replicated)
	float LedgeClimb_ServerStartTime = -1.f;

	// Normalised progress 0..1 along the whole maneuver.
	float LedgeClimb_Alpha = 0.f;
	

	/* ----- Climb Exit ----- */

	//
	float Climb_TimeSinceEntered = BIG_NUMBER;

	// Toggle for graduated exit-climb upright.
	bool bClimb_ExitUprightBlendActive = false;

	// Target rotation for the exit upright blend.
	FQuat Climb_ExitUprightTargetQuat = FQuat::Identity;
	
	
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
	float Climb_EyeForwardTraceDistance = 100.f;

	// Distance to offset the start of the forward climb trace from the character.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ForwardTraceStartOffset = 30.f;

	// Forward distance used to search for climbable surfaces.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Tracing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ComponentForwardTraceDistance = 50.f;

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
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ExitUprightInterpSpeed = 12.f;

	
	/* ----- Climb Wall Snapping ----- */

	// Time spend in Lerping entry snap from entry to contant speed. set 0 to disable.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Attach", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_EntryWallSnapBlendTime = 0.15f;
	
	// Snap entering speed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Attach", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_EntryWallSnapSpeed = 200.f;

	// Snap contant speed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Attach", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_WallSnapSpeed = 500.f;

	
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
	float Climb_EntrySlideDeceleration = 1200.f;

	// Spamming safeguard; ANCHOR: implement later.
	// Save player from "panicked spam" after success.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ExitLockoutSeconds = 0.25f;
	
	// Spamming safeguard; ignore start requests for this long after a manual StopClimbing().
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|Debug", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float Climb_ReEntryLockoutSeconds = 0.25f;
	
	/* ----- Climb Rope ----- */

	// Maximum player-center distance from the rope at which attaching is allowed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float RopeClimb_MaxAttachDistance = 125.f;
	// Desired distance from rope centerline to the character capsule center.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing", meta=(AllowPrivateAccess="true", ClampMin="1.0"))
	float RopeClimb_RadialAttachDistance = 55.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float RopeClimb_VerticalSpeed = 120.f;
	// Degrees per second while orbiting around the rope.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float RopeClimb_OrbitSpeedDegrees = 60.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float RopeClimb_RotationInterpSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|RopeClimbing",
		meta=(AllowPrivateAccess="true"))
	bool bRopeClimb_FaceRope = true;
	
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
	float LedgeClimb_EyeHeightOffset = -10.f;

	// How far down we probe from above the lip to find the walkable top surface.
	// ANCHOR: Consider change this to Full capsule height with a bit of padding.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float LedgeClimb_TopProbeDownDistance = 150.f;

	// ANCHOR: For future use, switching to root motion. 
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CustomMovement|Climbing|LedgeClimb", meta=(AllowPrivateAccess="true"))
	ELedgeClimbMethod LedgeClimbMethod = ELedgeClimbMethod::Coded;
};
