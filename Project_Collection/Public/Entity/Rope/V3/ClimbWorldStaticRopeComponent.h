

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ClimbWorldStaticRopeComponent.generated.h"


/* ==================== Declares ==================== */

constexpr float RopeDataFallback = 1.f;

class USplineComponent;

/*
 * Replicated rope snapshot.
 * - revision for deterministic "new snapshot" checks
 * - state booleans for client-side readback
 * - particle world positions for visual + climb tracking query
 */
USTRUCT(BlueprintType)
struct FRopeNetworkSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SnapshotRevision = 0; // skip duplicate

	UPROPERTY()
	bool bSnapshotAnchored = false;
	UPROPERTY()
	bool bSnapshotLengthFinalized = false;

	UPROPERTY()
	bool bSnapshotSimulationEnabled = true;

	UPROPERTY()
	bool bSnapshotSleeping = false;
	UPROPERTY()
	bool bSnapshotFrozenByDistance = false;

	UPROPERTY()
	TArray<FVector_NetQuantize10> ParticleWorldPositions;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRopeSimpleDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRopeSleepStateChangedDelegate, bool, bIsNowSleeping);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRopeSimulationStateChangedDelegate, bool, bIsSimulationEnabled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRopeLengthChangedDelegate, float, NewLength);



/*
 * Particle-based rope simulation component.
 *
 * Functions:
 * - Anchor rope and simulate it with fixed substeps.
 * - Runtime rope length preview (quantized by segment length).
 * - Finalize current length on demand.
 * - Collide only with WorldStatic and settle/sleep on rest.
 * - Replicate rope shape snapshots for client view/query.
 *
 * Rules:
 * - Particle[0] is pinned to anchor world location.
 * - Only WorldStatic collision is considered.
 * - Server authoritative for topology/simulation.
 * - Length can change while not finalized.
 *
 * Workflow:
 * - Request_AnchorPreview(...) -> server Auth_AnchorPreview(...)
 * - Request_SetRuntimeLength(...) while previewing
 * - Request_FinalizeCurrentLength() when player confirms length
 * - TickComponent fixed-substep simulation + collision + sleep
 *
 * States:
 * - Anchored / not anchored
 * - Length preview / finalized
 * - Simulation enabled / disabled
 * - Sleeping / awake
 * - Frozen by distance / active
 *
 * Boundary/Limitation:
 * - No dynamic-object collision (world static only).
 *
 * Networking:
 * - Server-authoritative simulation.
 * - Replicated state booleans and rope particle snapshots.
 * - Client-to-server request RPCs for actions.
 *
 * Future Works:
 * - FastArray / delta compression for large rope counts.
 * - Better collision depenetration fallback when start-penetrating deep.
 * - Moving anchor support? or just move parented object
 *
 * Notice:
 * - Class intentionally verbose and heavily commented for inspection/debug.
 * - Snapshot replication is not optimized.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UClimbWorldStaticRopeComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UClimbWorldStaticRopeComponent();


	/* ==================== Overrides ==================== */
public:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	
	/* ==================== Components ==================== */
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rope|Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USplineComponent> RopeSpline = nullptr;

	
public:
	/* ==================== APIs ==================== */
	
	/* ----- Lifecycle / Rope Setup ----- */

	// Anchor the rope in preview mode; length still change able; not networked (?).
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_AnchorPreview(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);
	// Finalize rope for use; networked.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_FinalizeCurrentLength();
	// Directly calls AnchorPreview and FinalizeCurrentLength.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_AnchorAndFinalize(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);
	// Reset: state, anchor, particles. Currently not really *recycling*.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_RecycleRope();
	
	/* ----- Runtime Length Controls ----- */
	
	// quantized to segment count.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_SetRuntimeLength(float InDesiredLength);
	// quantized to segment count.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_AdjustRuntimeLengthBy(float InLengthDelta);
	
	/* ----- Simulation / Performance Controls ----- */
	
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_SetSimulationEnabled(bool bInSimulationEnabled);
	// Toggle both tick and simulation progression.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_SetProcessingEnabled(bool bInProcessingEnabled);

	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_SetFreezeByDistanceEnabled(bool bInFreezeEnabled);
	// Force settle, and put to sleep.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_LayToRest();
	// Apply impulse near a distance from anchor.
	UFUNCTION(BlueprintCallable, Category="Rope|Request")
	void Request_AddImpulseAtDistance(float InDistanceFromAnchor, const FVector& InImpulseWorld);


	/* ==================== Queries ==================== */

	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool IsRopeAnchored() const { return bRopeAnchored; }
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool IsLengthFinalized() const { return bLengthFinalized; }
	
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool IsSimulationEnabled() const { return bSimulationEnabled; }
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool IsRopeSleeping() const { return bIsSleeping; }
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool IsRopeFrozenByDistance() const { return bIsFrozenByDistance; }
	UFUNCTION(BlueprintPure, Category="Rope|Query")

	// Thertheoretical iodical length
	float Query_GetCurrentLength() const;
	// ACtual runtime length. Anchor: should be the same with thertheoretical; remove if so (not adding stretch).
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	float Query_GetRuntimeArcLength() const;
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	int32 Query_GetCurrentSegments() const { return CurrSegments; }
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	float Query_GetFinalizedLength() const;
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	int32 Query_GetFinalizedSegments() const { return FinalizedSegments; }

	// Returns rope loc & tan at distance from anchor.
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool Query_GetRopeFrameAtDistance(
		float InDistanceFromAnchor,
		FVector& OutWorldLocation,
		FVector& OutWorldTangent
	) const;
	// Returns nearest point/tangent on rope.
	UFUNCTION(BlueprintPure, Category="Rope|Query")
	bool Query_FindNearestPointOnRope(
		const FVector& InWorldLocation,
		FVector& OutNearestWorldLocation,
		FVector& OutNearestWorldTangent,
		float& OutDistanceAlongRope,
		float& OutDistanceToRope
	) const;
	

	/* ==================== Event Delegates ==================== */

	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeSimpleDelegate OnRopeAnchored;
	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeSimpleDelegate OnRopeRecycled;
	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeSimpleDelegate OnRopeLengthFinalized;

	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeSleepStateChangedDelegate OnRopeSleepStateChanged;
	
	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeSimulationStateChangedDelegate OnRopeSimulationStateChanged;

	UPROPERTY(BlueprintAssignable, Category="Rope|Event")
	FRopeLengthChangedDelegate OnRopeLengthChanged;
	

	/* ==================== Subclasses Extension ==================== */
protected:
	virtual void HandleOnRopeAnchored();
	virtual void HandleOnRopeRecycled();
	virtual void HandleOnLengthFinalized();
	virtual void HandleOnSleepStateChanged(bool bInNowSleeping);
	virtual void HandleOnSimulationStateChanged(bool bInSimulationEnabled);
	virtual void HandleOnLengthChanged(float InNewLength);

	/* ==================== Internal Function ==================== */

	/* ----- Networking and replication ----- */
protected:
	UFUNCTION()
	void OnRep_RopeAnchored();
	UFUNCTION()
	void OnRep_LengthFinalized();
	UFUNCTION()
	void OnRep_CurrentSegments();
	UFUNCTION()
	void OnRep_SimulationEnabled();
	UFUNCTION()
	void OnRep_ProcessingEnabled();
	UFUNCTION()
	void OnRep_IsSleeping();
	UFUNCTION()
	void OnRep_ReplicatedNetworkSnapshot();

private:
	UFUNCTION(Server, Reliable)
	void RpcServer_AnchorPreview(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);
	UFUNCTION(Server, Reliable)
	void RpcServer_AnchorAndFinalize(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);
	UFUNCTION(Server, Reliable)
	void RpcServer_FinalizeCurrentLength();
	
	UFUNCTION(Server, Reliable)
	void RpcServer_RecycleRope();
	
	UFUNCTION(Server, Reliable)
	void RpcServer_SetRuntimeLength(float InDesiredLength);
	UFUNCTION(Server, Reliable)
	void RpcServer_AdjustRuntimeLengthBy(float InLengthDelta);
	UFUNCTION(Server, Reliable)
	void RpcServer_SetSimulationEnabled(bool bInSimulationEnabled);
	UFUNCTION(Server, Reliable)
	void RpcServer_SetProcessingEnabled(bool bInProcessingEnabled);
	UFUNCTION(Server, Reliable)
	void RpcServer_SetFreezeByDistanceEnabled(bool bInFreezeEnabled);
	UFUNCTION(Server, Reliable)
	void RpcServer_AddImpulseAtDistance(float InDistanceFromAnchor, const FVector& InImpulseWorld);
	UFUNCTION(Server, Reliable)
	void RpcServer_LayToRest();

	/* ----- Authority-Gated Entrypoints ----- */
	
	bool Auth_AnchorPreview(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);
	
	bool Auth_AnchorAndFinalize(
		const FVector& InAnchorWorldLocation,
		const FVector& InInitialDropDirection,
		float InInitialLength
	);

	bool Auth_FinalizeCurrentLength();
	bool Auth_RecycleRope();

	bool Auth_SetRuntimeLength(float InDesiredLength);
	bool Auth_AdjustRuntimeLengthBy(float InLengthDelta);

	bool Auth_SetSimulationEnabled(bool bInSimulationEnabled);
	bool Auth_SetProcessingEnabled(bool bInProcessingEnabled);

	bool Auth_SetFreezeByDistanceEnabled(bool bInFreezeEnabled);
	bool Auth_LayToRest();

	bool Auth_AddImpulseAtDistance(float InDistanceFromAnchor, const FVector& InImpulseWorld);

	/* ----- Rope Topology / Build ----- */

	void Auth_RebuildParticlesFromAnchor();
	void Auth_ResizeParticlesToCurrentSegments();
	bool Auth_AppendOneParticleFromTail();

	// stabilize topology
	void Auth_ApplyTopologyProjectionPass(int32 InIterations);
	
	/* ----- Core Simulation  ----- */

	// One fixed physics substep
	void Auth_SimulateOneFixedSubstep(float InFixedSubstepSeconds);

	void Auth_SolveDistanceConstraintsOneIteration();
	void Auth_SolveWorldStaticCollisionsOneIteration(float InFixedSubstepSeconds);

	void Auth_EvaluateSleepAfterSubstep(float InFixedSubstepSeconds);
	void Auth_WakeRope(const FString& InReason);
	void Auth_EnterSleep(const FString& InReason);
	void Auth_UpdateDistanceFreezeState(float InDeltaSeconds);


	/* ----- Runtime Data / Visual / Replication ----- */

	void RefreshSplineFromRuntimeParticles();

	void Auth_PushNetworkSnapshot(bool bInForceNetUpdate);
	void RebuildRuntimeParticlesFromSnapshot();


	/* ----- Math / Utility ----- */

	int32 QuantizeLengthToSegments(float InLength) const;

	int32 GetExpectedParticleCount() const;
	float GetTargetSegmentLength() const;
	bool HasValidRuntimeParticles() const;
	static FVector ClosestPointOnSegment(const FVector& InPoint, const FVector& InSegmentStart, const FVector& InSegmentEnd, float& OutSegmentT);

	void DrawDebugRopeState() const;



	/* ==================== Runtime State ==================== */
private:
	UPROPERTY(Transient, ReplicatedUsing=OnRep_RopeAnchored)
	bool bRopeAnchored = false;
	UPROPERTY(Transient, ReplicatedUsing=OnRep_LengthFinalized)
	bool bLengthFinalized = false;
	UPROPERTY(Transient, ReplicatedUsing=OnRep_SimulationEnabled)
	bool bSimulationEnabled = true;
	UPROPERTY(Transient, ReplicatedUsing=OnRep_ProcessingEnabled)
	bool bProcessingEnabled = true;
	UPROPERTY(Transient, Replicated)
	bool bFreezeByDistanceEnabled = true;
	UPROPERTY(Transient, ReplicatedUsing=OnRep_IsSleeping)
	bool bIsSleeping = false;
	UPROPERTY(Transient, Replicated)
	bool bIsFrozenByDistance = false;

	UPROPERTY(Transient, Replicated)
	FVector_NetQuantize10 AnchorWorldLoc = FVector::ZeroVector;

	// Runtime quantized segment count.
	UPROPERTY(Transient, ReplicatedUsing=OnRep_CurrentSegments)
	int32 CurrSegments = 0;
	UPROPERTY(Transient, Replicated)
	int32 FinalizedSegments = 0;

	// Replicated rope snapshot for client rebuild
	UPROPERTY(Transient, ReplicatedUsing=OnRep_ReplicatedNetworkSnapshot)
	FRopeNetworkSnapshot ReplicatedNetworkSnapshot;
	
	TArray<FVector> RuntimeParticlePos;
	TArray<FVector> RuntimeParticlePrevPos;
	TArray<bool> RuntimeParticleHadContact;
	TArray<FVector> RuntimeParticleContactNorm;

	// Topology extension fallback
	FVector LastRequestedDropDir = FVector(0.f, 0.f, -1.f);
	// Fixed-step accumulator
	float SimAccumulatorSecs = 0.f;
	
	float DistFreezeAccumulatorSecs = 0.f;
	float SleepAccumulatedSecs = 0.f;

	// Max correction seen during latest substep
	float LastSubstepMaxCorr = 0.f;

	float SnapshotSendAccumulatorSecs = 0.f;
	// Last snapshot revision consumed locally
	int32 LastAppliedSnapshotRev = INDEX_NONE;

	FCollisionObjectQueryParams WorldStaticOnlyObjectQueryParams;
	FCollisionQueryParams SharedCollisionQueryParams;
	

	/* ==================== Config ==================== */
public:
	/* ----- Setup / Anchor ----- */

	// Immediately anchor using the owning actor's transform. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup")
	bool bConfig_AutoAnchorAtBeginPlay = false;
	// When true and auto-anchoring enabled, finalized length immediately.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup")
	bool bConfig_AutoFinalizeOnBeginPlay = false;

	// Construct a spline matching runtime particles, so CMC Climb can attach to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup")
	bool bConfig_CreateInternalSpline = true;

	// Used by auto-anchor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup", meta=(ClampMin="30.0"))
	float Config_DefaultInitialLength = 180.f;
	// Used by auto-anchor; fallback if no explicit drop direction is provided
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup")
	FVector Config_DefaultDropDirection = FVector(0.f, 0.f, -1.f);

	// Target segment length. Runtime length is quantized to integer segments of this length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup", meta=(ClampMin="1.0"))
	float Config_SegmentLength = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup", meta=(ClampMin="1", ClampMax="512"))
	int32 Config_MinSegments = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Setup", meta=(ClampMin="1", ClampMax="512"))
	int32 Config_MaxSegments = 96;

	/* ----- Topology Change Stabilization ----- */

	// Number of projection passes used right after the rope topology is first built. 
	// Higher -> fewer visible penetrations.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Topology", meta=(ClampMin="0", ClampMax="128"))
	int32 Config_InitialProjectionIterations = 8;
	// Use to stabilize particles when segment counts are added/removed. 
	// Increase if length adjustments leave visible gaps or collisions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Topology", meta=(ClampMin="0", ClampMax="128"))
	int32 Config_LengthChangeProjectionIterations = 6;

	/* ----- Simulation ----- */

	// Fixed substep for deterministic-ish simulation; Should match project's physic substep settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="0.001", ClampMax="0.1"))
	float Config_FixedSubstepSeconds = 0.0083333333f; // 120Hz
	// Max fixed substeps consumed per Tick; process safeguard.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="1", ClampMax="16"))
	int32 Config_MaxSubstepsPerTick = 4;

	// More iterations yield stiffness and improve collision.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="1", ClampMax="32"))
	int32 Config_ConstraintIterations = 6;
	// Determine how fast the rope loose energy (less swing).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="0.0", ClampMax="100.0"))
	float Config_LinearDampingPerSecond = 1.2f;

	// Gravity scale for rope acceleration; not actual weight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="0.0", ClampMax="5.0"))
	float Config_GravityScale = 3.f;

	// Scale factor for AddImpulseAtDistance requests.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Simulation", meta=(ClampMin="0.0", ClampMax="100.0"))
	float Config_ImpulseToVelocityScale = 1.f;
	
	/* ----- Collision (WorldStatic only) ----- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Collision", meta=(ClampMin="0.1", ClampMax="100.0"))
	float Config_ParticleCollisionRadius = 10.f;
	// Extra push-out offset applied on contact projection to balance Friction; let the rope 'glide' along surfaces.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Collision", meta=(ClampMin="0.0", ClampMax="20.0"))
	float Config_CollisionContactOffset = 0.5f;

	// Tangential speed threshold which static friction snaps to rest; hold rope in place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Collision", meta=(ClampMin="0.0", ClampMax="200.0"))
	float Config_StaticFrictionSpeedThreshold = 8.f;
	// Damping while a particle is sliding tangentially along geometry; increase to reduce slide distance over time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Collision", meta=(ClampMin="0.0", ClampMax="100.0"))
	float Config_KineticFrictionPerSecond = 7.f;

	// Sweeps use complex collision (triangles); trade performance with precise collision against detailed meshes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Collision")
	bool bConfig_TraceComplex = false;
	
	/* ----- Sleeping / Stability ----- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Sleep")
	bool bConfig_EnableSleeping = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Sleep", meta=(ClampMin="0.0", ClampMax="300.0"))
	float Config_SleepVelocityThreshold = 100.f;
	// Maximum constraint correction Magnitude allowed while still counting toward sleep. 
	// If constraint corrections are large each substep, the rope will not sleep.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Sleep", meta=(ClampMin="0.0", ClampMax="200.0"))
	float Config_SleepCorrectionThreshold = 50.0f;
	// time with satisfied thresholds before sleeping. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Sleep", meta=(ClampMin="0.1", ClampMax="10.0"))
	float Config_SleepDelaySeconds = 2.f;

	// Used by Request_LayToRest() for aggressively projecting the rope into a settle. 
	// Increase to reduce penetration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Sleep", meta=(ClampMin="1", ClampMax="128"))
	int32 Config_LayProjectionIterations = 10;
	
	/* ----- Replication ----- */

	// Server snapshot send interval; poorly optimized.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Replication", meta=(ClampMin="0.01", ClampMax="1.0"))
	float Config_SnapshotSendIntervalSeconds = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Replication")
	bool bConfig_SendSnapshotsWhileSleeping = false;
	
	/* ----- Distance Freeze ----- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|DistanceFreeze")
	bool bConfig_EnableDistanceFreezeByDefault = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|DistanceFreeze", meta=(ClampMin="100.0"))
	float Config_FreezeDistanceFromPlayers = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|DistanceFreeze", meta=(ClampMin="0.05", ClampMax="5.0"))
	float Config_DistanceFreezeCheckIntervalSeconds = 0.25f;
	
	/* ----- Debug ----- */

	// Draw particle and reference lines.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Debug")
	bool bConfig_DebugDrawRope = false;
	// Draw contact points and normals for particles in contact.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Debug")
	bool bConfig_DebugDrawContacts = false;
	// Draw sleep/freeze debug state markers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Debug")
	bool bConfig_DebugDrawSleep = false;
	// 0 -> one-frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rope|Config|Debug", meta=(ClampMin="0.0", ClampMax="5.0"))
	float Config_DebugDrawDuration = 0.f;
};