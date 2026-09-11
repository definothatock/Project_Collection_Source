

#include "Entity/Rope/V3/ClimbWorldStaticRopeComponent.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogClimbWorldStaticRopeComponent, Log, All);


/*
 * TODO:
 * - Make support with object anchoring, not just world anchoring
 * - Make Client prediction for the rope's location then greatly reduce replication. The actual critical player attachment
 * location will be handled by CMC directly. 
 *
 * Minor Issue:
 * - When re-entering processing range, rope will wake even when the rope was originally sleeping.
 *
 */



/* ==================== Constructor / Overrides ==================== */

UClimbWorldStaticRopeComponent::UClimbWorldStaticRopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	SetIsReplicatedByDefault(true);
}

void UClimbWorldStaticRopeComponent::BeginPlay()
{
	Super::BeginPlay();

	WorldStaticOnlyObjectQueryParams = FCollisionObjectQueryParams();
	WorldStaticOnlyObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);

	AActor* OwnerActor = GetOwner();
	SharedCollisionQueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(ClimbWorldStaticRopeComponent), false, OwnerActor);
	SharedCollisionQueryParams.bReturnPhysicalMaterial = false;

	bFreezeByDistanceEnabled = bConfig_EnableDistanceFreezeByDefault;

	SetComponentTickEnabled(bProcessingEnabled);

	// Spline
	if (bConfig_CreateInternalSpline && OwnerActor && !RopeSpline)
	{
		const FName UniqueName = MakeUniqueObjectName(OwnerActor, USplineComponent::StaticClass(), TEXT("RopeSpline_Internal"));
		RopeSpline = NewObject<USplineComponent>(OwnerActor, UniqueName);

		if (RopeSpline)
		{
			RopeSpline->SetupAttachment(this);
			RopeSpline->SetClosedLoop(false);
			RopeSpline->SetMobility(EComponentMobility::Movable);
			RopeSpline->RegisterComponent();
			OwnerActor->AddInstanceComponent(RopeSpline);
		}
	}

	// auto anchor
	if (GetOwner() && GetOwner()->HasAuthority() && bConfig_AutoAnchorAtBeginPlay)
	{
		Request_AnchorPreview(
			GetComponentLocation(),
			Config_DefaultDropDirection,
			Config_DefaultInitialLength
		);

		if (bConfig_AutoFinalizeOnBeginPlay)
		{
			Request_FinalizeCurrentLength();
		}
	}
}

void UClimbWorldStaticRopeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bProcessingEnabled)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	const bool bHasAuthority = (OwnerActor && OwnerActor->HasAuthority());

	if (bHasAuthority)
	{
		// Distance-freeze check (server-authoritative).
		Auth_UpdateDistanceFreezeState(DeltaTime);

		// Rope simulation gate.
		const bool bCanSimulateThisFrame =
			bRopeAnchored &&
			bSimulationEnabled &&
			!bIsSleeping &&
			!bIsFrozenByDistance &&
			HasValidRuntimeParticles();

		// Fixed substep simulation.
		if (bCanSimulateThisFrame)
		{
			SimAccumulatorSecs += DeltaTime;

			int32 ExecutedSubsteps = 0;
			const float SafeSubstep = FMath::Max(Config_FixedSubstepSeconds, 0.001f);

			while (SimAccumulatorSecs >= SafeSubstep && ExecutedSubsteps < Config_MaxSubstepsPerTick)
			{
				Auth_SimulateOneFixedSubstep(SafeSubstep);
				SimAccumulatorSecs -= SafeSubstep;
				++ExecutedSubsteps;
			}

			// Back-pressure protection: drop excessive remainder for prototype stability.
			if (ExecutedSubsteps >= Config_MaxSubstepsPerTick && SimAccumulatorSecs > SafeSubstep)
			{
				UE_LOG(LogClimbWorldStaticRopeComponent, Verbose, TEXT("Substep budget saturated; dropping accumulator remainder."));
				SimAccumulatorSecs = 0.f;
			}
		}

		// Keep spline synchronized with latest runtime particles.
		if (bRopeAnchored)
		{
			RefreshSplineFromRuntimeParticles();
		}

		// Periodic snapshot replication.
		SnapshotSendAccumulatorSecs += DeltaTime;
		if (bRopeAnchored && SnapshotSendAccumulatorSecs >= Config_SnapshotSendIntervalSeconds)
		{
			SnapshotSendAccumulatorSecs = 0.f;

			if (!bIsSleeping || bConfig_SendSnapshotsWhileSleeping)
			{
				Auth_PushNetworkSnapshot(false);
			}
		}
	}

	// Optional debug draw on all peers.
	DrawDebugRopeState();
}

void UClimbWorldStaticRopeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bRopeAnchored);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bLengthFinalized);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bSimulationEnabled);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bProcessingEnabled);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bFreezeByDistanceEnabled);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bIsSleeping);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, bIsFrozenByDistance);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, AnchorWorldLoc);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, CurrSegments);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, FinalizedSegments);
	DOREPLIFETIME(UClimbWorldStaticRopeComponent, ReplicatedNetworkSnapshot);
}


/* ==================== Public Request APIs ==================== */

void UClimbWorldStaticRopeComponent::Request_AnchorPreview(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_AnchorPreview(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
		return;
	}

	RpcServer_AnchorPreview(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
}

void UClimbWorldStaticRopeComponent::Request_FinalizeCurrentLength()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_FinalizeCurrentLength();
		return;
	}

	RpcServer_FinalizeCurrentLength();
}

void UClimbWorldStaticRopeComponent::Request_AnchorAndFinalize(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_AnchorAndFinalize(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
		return;
	}

	RpcServer_AnchorAndFinalize(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
}

void UClimbWorldStaticRopeComponent::Request_RecycleRope()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_RecycleRope();
		return;
	}

	RpcServer_RecycleRope();
}

void UClimbWorldStaticRopeComponent::Request_SetRuntimeLength(float InDesiredLength)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_SetRuntimeLength(InDesiredLength);
		return;
	}

	RpcServer_SetRuntimeLength(InDesiredLength);
}

void UClimbWorldStaticRopeComponent::Request_AdjustRuntimeLengthBy(float InLengthDelta)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_AdjustRuntimeLengthBy(InLengthDelta);
		return;
	}

	RpcServer_AdjustRuntimeLengthBy(InLengthDelta);
}

void UClimbWorldStaticRopeComponent::Request_SetSimulationEnabled(bool bInSimulationEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_SetSimulationEnabled(bInSimulationEnabled);
		return;
	}

	RpcServer_SetSimulationEnabled(bInSimulationEnabled);
}

void UClimbWorldStaticRopeComponent::Request_SetProcessingEnabled(bool bInProcessingEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_SetProcessingEnabled(bInProcessingEnabled);
		return;
	}

	RpcServer_SetProcessingEnabled(bInProcessingEnabled);
}

void UClimbWorldStaticRopeComponent::Request_SetFreezeByDistanceEnabled(bool bInFreezeEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_SetFreezeByDistanceEnabled(bInFreezeEnabled);
		return;
	}

	RpcServer_SetFreezeByDistanceEnabled(bInFreezeEnabled);
}

void UClimbWorldStaticRopeComponent::Request_LayToRest()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_LayToRest();
		return;
	}

	RpcServer_LayToRest();
}

void UClimbWorldStaticRopeComponent::Request_AddImpulseAtDistance(float InDistanceFromAnchor, const FVector& InImpulseWorld)
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		Auth_AddImpulseAtDistance(InDistanceFromAnchor, InImpulseWorld);
		return;
	}

	RpcServer_AddImpulseAtDistance(InDistanceFromAnchor, InImpulseWorld);
}


/* ==================== Queries ==================== */

float UClimbWorldStaticRopeComponent::Query_GetCurrentLength() const
{
	return static_cast<float>(CurrSegments) * FMath::Max(Config_SegmentLength, RopeDataFallback);
}

float UClimbWorldStaticRopeComponent::Query_GetRuntimeArcLength() const
{
	if (!HasValidRuntimeParticles() || RuntimeParticlePos.Num() < 2)
	{
		return 0.f;
	}

	float TotalLength = 0.f;

	for (int32 i = 0; i < RuntimeParticlePos.Num() - 1; ++i)
	{
		TotalLength += FVector::Dist(
			RuntimeParticlePos[i],
			RuntimeParticlePos[i + 1]
		);
	}

	return TotalLength;
}

float UClimbWorldStaticRopeComponent::Query_GetFinalizedLength() const
{
	return static_cast<float>(FinalizedSegments) * FMath::Max(Config_SegmentLength, RopeDataFallback);
}

bool UClimbWorldStaticRopeComponent::Query_GetRopeFrameAtDistance(
	float InDistanceFromAnchor,
	FVector& OutWorldLocation,
	FVector& OutWorldTangent
) const
{
	if (!HasValidRuntimeParticles() || RuntimeParticlePos.Num() < 2)
	{
		return false;
	}

	const float TargetDistance = FMath::Max(0.f, InDistanceFromAnchor);

	float AccumulatedDistance = 0.f;
	for (int32 SegmentIndex = 0; SegmentIndex < RuntimeParticlePos.Num() - 1; ++SegmentIndex)
	{
		const FVector A = RuntimeParticlePos[SegmentIndex];
		const FVector B = RuntimeParticlePos[SegmentIndex + 1];
		const FVector Segment = B - A;
		const float SegmentLength = Segment.Size();

		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (AccumulatedDistance + SegmentLength >= TargetDistance)
		{
			const float Remaining = TargetDistance - AccumulatedDistance;
			const float Alpha = FMath::Clamp(Remaining / SegmentLength, 0.f, 1.f);

			OutWorldLocation = FMath::Lerp(A, B, Alpha);
			OutWorldTangent = Segment / SegmentLength;
			return true;
		}

		AccumulatedDistance += SegmentLength;
	}

	// If distance exceeds rope length, return tail.
	OutWorldLocation = RuntimeParticlePos.Last();
	OutWorldTangent = (RuntimeParticlePos.Last() - RuntimeParticlePos[RuntimeParticlePos.Num() - 2]).GetSafeNormal();
	return true;
}

bool UClimbWorldStaticRopeComponent::Query_FindNearestPointOnRope(
	const FVector& InWorldLocation,
	FVector& OutNearestWorldLocation,
	FVector& OutNearestWorldTangent,
	float& OutDistanceAlongRope,
	float& OutDistanceToRope
) const
{
	if (!HasValidRuntimeParticles() || RuntimeParticlePos.Num() < 2)
	{return false;}

	float BestDistanceSquared = TNumericLimits<float>::Max();
	float BestDistanceAlongRope = 0.f;
	FVector BestPoint = FVector::ZeroVector;
	FVector BestTangent = FVector::ForwardVector;

	float AccumulatedDistance = 0.f;

	for (int32 SegmentIndex = 0; SegmentIndex < RuntimeParticlePos.Num() - 1; ++SegmentIndex)
	{
		const FVector A = RuntimeParticlePos[SegmentIndex];
		const FVector B = RuntimeParticlePos[SegmentIndex + 1];
		const FVector Segment = B - A;
		const float SegmentLength = Segment.Size();

		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		float SegmentT = 0.f;
		const FVector CandidatePoint = ClosestPointOnSegment(InWorldLocation, A, B, SegmentT);
		const float CandidateDistSq = FVector::DistSquared(InWorldLocation, CandidatePoint);

		if (CandidateDistSq < BestDistanceSquared)
		{
			BestDistanceSquared = CandidateDistSq;
			BestPoint = CandidatePoint;
			BestTangent = Segment / SegmentLength;
			BestDistanceAlongRope = AccumulatedDistance + (SegmentLength * SegmentT);
		}

		AccumulatedDistance += SegmentLength;
	}

	OutNearestWorldLocation = BestPoint;
	OutNearestWorldTangent = BestTangent;
	OutDistanceAlongRope = BestDistanceAlongRope;
	OutDistanceToRope = FMath::Sqrt(BestDistanceSquared);
	return true;
}


/* ==================== Subclasses Extension ==================== */

void UClimbWorldStaticRopeComponent::HandleOnRopeAnchored() {}
void UClimbWorldStaticRopeComponent::HandleOnRopeRecycled() {}
void UClimbWorldStaticRopeComponent::HandleOnLengthFinalized() {}
void UClimbWorldStaticRopeComponent::HandleOnSleepStateChanged(bool bInNowSleeping) {}
void UClimbWorldStaticRopeComponent::HandleOnSimulationStateChanged(bool bInSimulationEnabled) {}
void UClimbWorldStaticRopeComponent::HandleOnLengthChanged(float InNewLength) {}


/* ==================== Internal Function ==================== */

void UClimbWorldStaticRopeComponent::OnRep_RopeAnchored()
{
	if (bRopeAnchored)
	{
		HandleOnRopeAnchored();
		OnRopeAnchored.Broadcast();
	}
	else
	{
		HandleOnRopeRecycled();
		OnRopeRecycled.Broadcast();
	}
}

void UClimbWorldStaticRopeComponent::OnRep_LengthFinalized()
{
	if (bLengthFinalized)
	{
		HandleOnLengthFinalized();
		OnRopeLengthFinalized.Broadcast();
	}
}

void UClimbWorldStaticRopeComponent::OnRep_CurrentSegments()
{
	const float NewLength = Query_GetCurrentLength();
	HandleOnLengthChanged(NewLength);
	OnRopeLengthChanged.Broadcast(NewLength);
}

void UClimbWorldStaticRopeComponent::OnRep_SimulationEnabled()
{
	HandleOnSimulationStateChanged(bSimulationEnabled);
	OnRopeSimulationStateChanged.Broadcast(bSimulationEnabled);
}

void UClimbWorldStaticRopeComponent::OnRep_ProcessingEnabled()
{
	SetComponentTickEnabled(bProcessingEnabled);
}

void UClimbWorldStaticRopeComponent::OnRep_IsSleeping()
{
	HandleOnSleepStateChanged(bIsSleeping);
	OnRopeSleepStateChanged.Broadcast(bIsSleeping);
}

void UClimbWorldStaticRopeComponent::OnRep_ReplicatedNetworkSnapshot()
{
	RebuildRuntimeParticlesFromSnapshot();
	RefreshSplineFromRuntimeParticles();
}

/* ----- Network Requests ----- */

void UClimbWorldStaticRopeComponent::RpcServer_AnchorPreview_Implementation(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	Auth_AnchorPreview(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
}

void UClimbWorldStaticRopeComponent::RpcServer_AnchorAndFinalize_Implementation(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	Auth_AnchorAndFinalize(InAnchorWorldLocation, InInitialDropDirection, InInitialLength);
}

void UClimbWorldStaticRopeComponent::RpcServer_FinalizeCurrentLength_Implementation()
{
	Auth_FinalizeCurrentLength();
}

void UClimbWorldStaticRopeComponent::RpcServer_RecycleRope_Implementation()
{
	Auth_RecycleRope();
}

void UClimbWorldStaticRopeComponent::RpcServer_SetRuntimeLength_Implementation(float InDesiredLength)
{
	Auth_SetRuntimeLength(InDesiredLength);
}

void UClimbWorldStaticRopeComponent::RpcServer_AdjustRuntimeLengthBy_Implementation(float InLengthDelta)
{
	Auth_AdjustRuntimeLengthBy(InLengthDelta);
}

void UClimbWorldStaticRopeComponent::RpcServer_SetSimulationEnabled_Implementation(bool bInSimulationEnabled)
{
	Auth_SetSimulationEnabled(bInSimulationEnabled);
}

void UClimbWorldStaticRopeComponent::RpcServer_SetProcessingEnabled_Implementation(bool bInProcessingEnabled)
{
	Auth_SetProcessingEnabled(bInProcessingEnabled);
}

void UClimbWorldStaticRopeComponent::RpcServer_SetFreezeByDistanceEnabled_Implementation(bool bInFreezeEnabled)
{
	Auth_SetFreezeByDistanceEnabled(bInFreezeEnabled);
}

void UClimbWorldStaticRopeComponent::RpcServer_AddImpulseAtDistance_Implementation(float InDistanceFromAnchor, const FVector& InImpulseWorld)
{
	Auth_AddImpulseAtDistance(InDistanceFromAnchor, InImpulseWorld);
}

void UClimbWorldStaticRopeComponent::RpcServer_LayToRest_Implementation()
{
	Auth_LayToRest();
}

/* ----- Authority-Gated Entrypoints ----- */

bool UClimbWorldStaticRopeComponent::Auth_AnchorPreview(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	const FVector SafeDropDirection = InInitialDropDirection.IsNearlyZero() ? FVector(0.f, 0.f, -1.f) : InInitialDropDirection.GetSafeNormal();

	AnchorWorldLoc = InAnchorWorldLocation;
	LastRequestedDropDir = SafeDropDirection;

	CurrSegments = QuantizeLengthToSegments(InInitialLength);
	FinalizedSegments = 0;

	bRopeAnchored = true;
	bLengthFinalized = false;
	bIsSleeping = false;
	bIsFrozenByDistance = false;

	SimAccumulatorSecs = 0.f;
	SleepAccumulatedSecs = 0.f;
	LastSubstepMaxCorr = 0.f;

	Auth_RebuildParticlesFromAnchor();
	Auth_ApplyTopologyProjectionPass(Config_InitialProjectionIterations);

	RefreshSplineFromRuntimeParticles();
	Auth_PushNetworkSnapshot(true);

	UE_LOG(
		LogClimbWorldStaticRopeComponent,
		Log,
		TEXT("Rope anchored (preview). Anchor=%s Segments=%d Length=%.2f"),
		*AnchorWorldLoc.ToCompactString(),
		CurrSegments,
		Query_GetCurrentLength()
	);

	HandleOnRopeAnchored();
	OnRopeAnchored.Broadcast();

	HandleOnLengthChanged(Query_GetCurrentLength());
	OnRopeLengthChanged.Broadcast(Query_GetCurrentLength());

	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_AnchorAndFinalize(
	const FVector& InAnchorWorldLocation,
	const FVector& InInitialDropDirection,
	float InInitialLength
)
{
	if (!Auth_AnchorPreview(InAnchorWorldLocation, InInitialDropDirection, InInitialLength))
	{
		return false;
	}

	return Auth_FinalizeCurrentLength();
}

bool UClimbWorldStaticRopeComponent::Auth_FinalizeCurrentLength()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !bRopeAnchored)
	{
		return false;
	}

	if (bLengthFinalized)
	{
		return true;
	}

	bLengthFinalized = true;
	FinalizedSegments = CurrSegments;

	UE_LOG(
		LogClimbWorldStaticRopeComponent,
		Log,
		TEXT("Rope length finalized. Segments=%d Length=%.2f"),
		FinalizedSegments,
		Query_GetFinalizedLength()
	);

	HandleOnLengthFinalized();
	OnRopeLengthFinalized.Broadcast();

	Auth_PushNetworkSnapshot(true);
	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_RecycleRope()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	bRopeAnchored = false;
	bLengthFinalized = false;
	bSimulationEnabled = true;
	bIsSleeping = false;
	bIsFrozenByDistance = false;

	AnchorWorldLoc = FVector::ZeroVector;
	CurrSegments = 0;
	FinalizedSegments = 0;

	RuntimeParticlePos.Reset();
	RuntimeParticlePrevPos.Reset();
	RuntimeParticleHadContact.Reset();
	RuntimeParticleContactNorm.Reset();

	ReplicatedNetworkSnapshot = FRopeNetworkSnapshot();

	RefreshSplineFromRuntimeParticles();
	Auth_PushNetworkSnapshot(true);

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Rope recycled."));

	HandleOnRopeRecycled();
	OnRopeRecycled.Broadcast();

	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_SetRuntimeLength(float InDesiredLength)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !bRopeAnchored)
	{
		return false;
	}

	if (bLengthFinalized)
	{
		UE_LOG(LogClimbWorldStaticRopeComponent, Warning, TEXT("SetRuntimeLength rejected: length already finalized."));
		return false;
	}

	const int32 NewSegments = QuantizeLengthToSegments(InDesiredLength);
	if (NewSegments == CurrSegments)
	{
		return true;
	}

	const int32 OldSegments = CurrSegments;
	CurrSegments = NewSegments;

	Auth_ResizeParticlesToCurrentSegments();
	Auth_ApplyTopologyProjectionPass(Config_LengthChangeProjectionIterations);

	Auth_WakeRope(TEXT("Runtime length changed"));
	RefreshSplineFromRuntimeParticles();
	Auth_PushNetworkSnapshot(true);

	UE_LOG(
		LogClimbWorldStaticRopeComponent,
		Log,
		TEXT("Runtime length changed. Segments %d -> %d, Length=%.2f"),
		OldSegments,
		CurrSegments,
		Query_GetCurrentLength()
	);

	HandleOnLengthChanged(Query_GetCurrentLength());
	OnRopeLengthChanged.Broadcast(Query_GetCurrentLength());

	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_AdjustRuntimeLengthBy(float InLengthDelta)
{
	const float TargetLength = Query_GetCurrentLength() + InLengthDelta;
	return Auth_SetRuntimeLength(TargetLength);
}

bool UClimbWorldStaticRopeComponent::Auth_SetSimulationEnabled(bool bInSimulationEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	if (bSimulationEnabled == bInSimulationEnabled)
	{
		return true;
	}

	bSimulationEnabled = bInSimulationEnabled;

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Simulation enabled changed: %s"), bSimulationEnabled ? TEXT("true") : TEXT("false"));

	if (bSimulationEnabled)
	{
		Auth_WakeRope(TEXT("Simulation enabled"));
	}
	else
	{
		Auth_EnterSleep(TEXT("Simulation disabled"));
	}

	HandleOnSimulationStateChanged(bSimulationEnabled);
	OnRopeSimulationStateChanged.Broadcast(bSimulationEnabled);

	Auth_PushNetworkSnapshot(true);
	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_SetProcessingEnabled(bool bInProcessingEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	if (bProcessingEnabled == bInProcessingEnabled)
	{
		return true;
	}

	bProcessingEnabled = bInProcessingEnabled;
	SetComponentTickEnabled(bProcessingEnabled);

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Processing enabled changed: %s"), bProcessingEnabled ? TEXT("true") : TEXT("false"));

	Auth_PushNetworkSnapshot(true);
	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_SetFreezeByDistanceEnabled(bool bInFreezeEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	bFreezeByDistanceEnabled = bInFreezeEnabled;

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Distance freeze policy changed: %s"), bFreezeByDistanceEnabled ? TEXT("true") : TEXT("false"));

	if (!bFreezeByDistanceEnabled && bIsFrozenByDistance)
	{
		bIsFrozenByDistance = false;
		Auth_WakeRope(TEXT("Distance freeze disabled"));
	}

	Auth_PushNetworkSnapshot(true);
	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_LayToRest()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !HasValidRuntimeParticles() || !bRopeAnchored)
	{
		return false;
	}

	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		RuntimeParticlePrevPos[ParticleIndex] = RuntimeParticlePos[ParticleIndex];
	}

	for (int32 Iter = 0; Iter < Config_LayProjectionIterations; ++Iter)
	{
		Auth_SolveDistanceConstraintsOneIteration();
		Auth_SolveWorldStaticCollisionsOneIteration(FMath::Max(Config_FixedSubstepSeconds, 0.001f));
	}

	Auth_EnterSleep(TEXT("Request_LayToRest"));
	RefreshSplineFromRuntimeParticles();
	Auth_PushNetworkSnapshot(true);

	return true;
}

bool UClimbWorldStaticRopeComponent::Auth_AddImpulseAtDistance(float InDistanceFromAnchor, const FVector& InImpulseWorld)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !HasValidRuntimeParticles() || !bRopeAnchored)
	{
		return false;
	}

	const float SegmentLength = FMath::Max(GetTargetSegmentLength(), KINDA_SMALL_NUMBER);
	const float DistanceClamped = FMath::Clamp(InDistanceFromAnchor, 0.f, Query_GetCurrentLength());
	const float SegmentFloat = DistanceClamped / SegmentLength;

	const int32 LastParticleIndex = RuntimeParticlePos.Num() - 1;
	const int32 SegmentIndex = FMath::Clamp(FMath::FloorToInt(SegmentFloat), 0, LastParticleIndex - 1);
	const float LocalAlpha = FMath::Clamp(SegmentFloat - static_cast<float>(SegmentIndex), 0.f, 1.f);

	const int32 ParticleA = FMath::Clamp(SegmentIndex, 1, LastParticleIndex);
	const int32 ParticleB = FMath::Clamp(SegmentIndex + 1, 1, LastParticleIndex);

	const float WeightA = 1.f - LocalAlpha;
	const float WeightB = LocalAlpha;

	const FVector VelocityDelta = InImpulseWorld * Config_ImpulseToVelocityScale;
	const float Dt = FMath::Max(Config_FixedSubstepSeconds, 0.001f);

	RuntimeParticlePrevPos[ParticleA] -= VelocityDelta * WeightA * Dt;
	RuntimeParticlePrevPos[ParticleB] -= VelocityDelta * WeightB * Dt;

	Auth_WakeRope(TEXT("Impulse applied"));

	return true;
}

/* ----- Rope Topology / Build ----- */

void UClimbWorldStaticRopeComponent::Auth_RebuildParticlesFromAnchor()
{
	RuntimeParticlePos.Reset();
	RuntimeParticlePrevPos.Reset();
	RuntimeParticleHadContact.Reset();
	RuntimeParticleContactNorm.Reset();

	RuntimeParticlePos.Add(AnchorWorldLoc);
	RuntimeParticlePrevPos.Add(AnchorWorldLoc);
	RuntimeParticleHadContact.Add(false);
	RuntimeParticleContactNorm.Add(FVector::ZeroVector);

	while (RuntimeParticlePos.Num() < GetExpectedParticleCount())
	{
		if (!Auth_AppendOneParticleFromTail())
		{
			break;
		}
	}

	// Enforce pinned head.
	RuntimeParticlePos[0] = AnchorWorldLoc;
	RuntimeParticlePrevPos[0] = AnchorWorldLoc;
}

void UClimbWorldStaticRopeComponent::Auth_ResizeParticlesToCurrentSegments()
{
	const int32 TargetCount = GetExpectedParticleCount();

	if (RuntimeParticlePos.Num() <= 0)
	{
		Auth_RebuildParticlesFromAnchor();
		return;
	}

	if (RuntimeParticlePos.Num() < TargetCount)
	{
		while (RuntimeParticlePos.Num() < TargetCount)
		{
			if (!Auth_AppendOneParticleFromTail())
			{
				break;
			}
		}
	}
	else if (RuntimeParticlePos.Num() > TargetCount)
	{
		RuntimeParticlePos.SetNum(TargetCount);
		RuntimeParticlePrevPos.SetNum(TargetCount);
		RuntimeParticleHadContact.SetNum(TargetCount);
		RuntimeParticleContactNorm.SetNum(TargetCount);
	}

	// Ensure all arrays are coherent.
	RuntimeParticlePrevPos.SetNum(RuntimeParticlePos.Num());
	RuntimeParticleHadContact.SetNum(RuntimeParticlePos.Num());
	RuntimeParticleContactNorm.SetNum(RuntimeParticlePos.Num());

	// Head remains pinned.
	RuntimeParticlePos[0] = AnchorWorldLoc;
	RuntimeParticlePrevPos[0] = AnchorWorldLoc;
}

bool UClimbWorldStaticRopeComponent::Auth_AppendOneParticleFromTail()
{
	if (RuntimeParticlePos.Num() <= 0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TailStart = RuntimeParticlePos.Last();

	// Extension direction:
	// 1) prefer tail local direction (last - previous)
	// 2) fallback to last requested drop direction
	FVector ExtendDirection = LastRequestedDropDir;

	if (RuntimeParticlePos.Num() >= 2)
	{
		const FVector TailDelta = RuntimeParticlePos.Last() - RuntimeParticlePos[RuntimeParticlePos.Num() - 2];
		if (!TailDelta.IsNearlyZero())
		{
			ExtendDirection = TailDelta.GetSafeNormal();
		}
	}

	if (ExtendDirection.IsNearlyZero())
	{
		ExtendDirection = FVector(0.f, 0.f, -1.f);
	}

	const float SegmentLength = GetTargetSegmentLength();
	const FVector CandidateTarget = TailStart + ExtendDirection * SegmentLength;

	FVector ResolvedTarget = CandidateTarget;

	FCollisionQueryParams QueryParams = SharedCollisionQueryParams;
	QueryParams.bTraceComplex = bConfig_TraceComplex;

	const FCollisionShape ParticleShape = FCollisionShape::MakeSphere(Config_ParticleCollisionRadius);

	FHitResult Hit;
	const bool bHitBlocking = World->SweepSingleByObjectType(
		Hit,
		TailStart,
		CandidateTarget,
		FQuat::Identity,
		WorldStaticOnlyObjectQueryParams,
		ParticleShape,
		QueryParams
	);

	if (bHitBlocking)
	{
		FVector SafeNormal = Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.GetSafeNormal();
		ResolvedTarget = Hit.Location + SafeNormal * Config_CollisionContactOffset;

		if (Hit.bStartPenetrating)
		{
			const float SafePenDepth = FMath::Max(Hit.PenetrationDepth, 0.f);
			ResolvedTarget = TailStart + SafeNormal * (SafePenDepth + Config_CollisionContactOffset);
		}
	}

	RuntimeParticlePos.Add(ResolvedTarget);
	RuntimeParticlePrevPos.Add(ResolvedTarget);
	RuntimeParticleHadContact.Add(false);
	RuntimeParticleContactNorm.Add(FVector::ZeroVector);

	return true;
}

void UClimbWorldStaticRopeComponent::Auth_ApplyTopologyProjectionPass(int32 InIterations)
{
	const int32 SafeIterations = FMath::Max(0, InIterations);
	for (int32 Iter = 0; Iter < SafeIterations; ++Iter)
	{
		Auth_SolveDistanceConstraintsOneIteration();
		Auth_SolveWorldStaticCollisionsOneIteration(FMath::Max(Config_FixedSubstepSeconds, 0.001f));
	}

	// Kill residual velocity after topology changes to avoid spike jitter.
	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		RuntimeParticlePrevPos[ParticleIndex] = RuntimeParticlePos[ParticleIndex];
	}
}

/* ----- Core Simulation ----- */

void UClimbWorldStaticRopeComponent::Auth_SimulateOneFixedSubstep(float InFixedSubstepSeconds)
{
	if (!HasValidRuntimeParticles() || RuntimeParticlePos.Num() < 2)
	{
		return;
	}

	const float Dt = FMath::Max(InFixedSubstepSeconds, 0.001f);
	const float DampingFactor = FMath::Clamp(1.f - (Config_LinearDampingPerSecond * Dt), 0.f, 1.f);

	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.f;
	const FVector GravityAcceleration = FVector(0.f, 0.f, GravityZ * Config_GravityScale);

	LastSubstepMaxCorr = 0.f;

	// 1) Verlet integration (except pinned particle 0).
	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		const FVector Current = RuntimeParticlePos[ParticleIndex];
		const FVector Previous = RuntimeParticlePrevPos[ParticleIndex];

		const FVector VelocityTerm = (Current - Previous) * DampingFactor;
		const FVector AccelTerm = GravityAcceleration * Dt * Dt;

		const FVector Next = Current + VelocityTerm + AccelTerm;

		RuntimeParticlePrevPos[ParticleIndex] = Current;
		RuntimeParticlePos[ParticleIndex] = Next;

		RuntimeParticleHadContact[ParticleIndex] = false;
		RuntimeParticleContactNorm[ParticleIndex] = FVector::ZeroVector;
	}

	// Pinned particle remains exact.
	RuntimeParticlePos[0] = AnchorWorldLoc;
	RuntimeParticlePrevPos[0] = AnchorWorldLoc;

	// 2) Iterative solve: distance constraints + world static collisions.
	for (int32 SolverIter = 0; SolverIter < Config_ConstraintIterations; ++SolverIter)
	{
		Auth_SolveDistanceConstraintsOneIteration();
		Auth_SolveWorldStaticCollisionsOneIteration(Dt);
	}

	// Final pin correction.
	RuntimeParticlePos[0] = AnchorWorldLoc;
	RuntimeParticlePrevPos[0] = AnchorWorldLoc;

	// 3) Sleep evaluation after stabilization.
	Auth_EvaluateSleepAfterSubstep(Dt);
}

void UClimbWorldStaticRopeComponent::Auth_SolveDistanceConstraintsOneIteration()
{
	if (RuntimeParticlePos.Num() < 2)
	{
		return;
	}

	const float TargetSegmentLength = GetTargetSegmentLength();

	// Keep index 0 pinned.
	RuntimeParticlePos[0] = AnchorWorldLoc;

	for (int32 SegmentIndex = 0; SegmentIndex < RuntimeParticlePos.Num() - 1; ++SegmentIndex)
	{
		FVector& A = RuntimeParticlePos[SegmentIndex];
		FVector& B = RuntimeParticlePos[SegmentIndex + 1];

		const FVector Delta = B - A;
		const float CurrentDistance = Delta.Size();

		if (CurrentDistance <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float Error = CurrentDistance - TargetSegmentLength;
		LastSubstepMaxCorr = FMath::Max(LastSubstepMaxCorr, FMath::Abs(Error));

		const FVector Direction = Delta / CurrentDistance;

		// Segment attached to pinned head: move only B.
		if (SegmentIndex == 0)
		{
			B -= Direction * Error;
		}
		else
		{
			// Equal mass split correction for dynamic particles.
			const FVector HalfCorrection = Direction * (Error * 0.5f);
			A += HalfCorrection;
			B -= HalfCorrection;
		}
	}

	// Reinforce pin.
	RuntimeParticlePos[0] = AnchorWorldLoc;
}

void UClimbWorldStaticRopeComponent::Auth_SolveWorldStaticCollisionsOneIteration(float InFixedSubstepSeconds)
{
	UWorld* World = GetWorld();
	if (!World || RuntimeParticlePos.Num() < 2)
	{
		return;
	}

	const float Dt = FMath::Max(InFixedSubstepSeconds, 0.001f);
	const FCollisionShape ParticleShape = FCollisionShape::MakeSphere(Config_ParticleCollisionRadius);

	FCollisionQueryParams QueryParams = SharedCollisionQueryParams;
	QueryParams.bTraceComplex = bConfig_TraceComplex;

	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		FVector& Current = RuntimeParticlePos[ParticleIndex];
		FVector& Previous = RuntimeParticlePrevPos[ParticleIndex];

		FHitResult Hit;
		const bool bHitBlocking = World->SweepSingleByObjectType(
			Hit,
			Previous,
			Current,
			FQuat::Identity,
			WorldStaticOnlyObjectQueryParams,
			ParticleShape,
			QueryParams
		);

		if (!bHitBlocking)
		{
			continue;
		}

		const FVector SafeNormal = Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.GetSafeNormal();

		// Sweep location for sphere is already center-at-contact; add small separation offset.
		FVector ResolvedPosition = Hit.Location + SafeNormal * Config_CollisionContactOffset;

		// Start-penetrating fallback.
		if (Hit.bStartPenetrating)
		{
			const float SafePenDepth = FMath::Max(Hit.PenetrationDepth, 0.f);
			ResolvedPosition = Current + SafeNormal * (SafePenDepth + Config_CollisionContactOffset);
		}

		LastSubstepMaxCorr = FMath::Max(LastSubstepMaxCorr, FVector::Dist(Current, ResolvedPosition));
		Current = ResolvedPosition;

		// Velocity-level contact response:
		// - remove inward normal component
		// - apply static/kinetic friction to tangential component
		FVector Velocity = (Current - Previous) / Dt;

		const float NormalSpeed = FVector::DotProduct(Velocity, SafeNormal);
		if (NormalSpeed < 0.f)
		{
			Velocity -= SafeNormal * NormalSpeed;
		}

		FVector TangentialVelocity = Velocity - SafeNormal * FVector::DotProduct(Velocity, SafeNormal);
		const float TangentialSpeed = TangentialVelocity.Size();

		if (TangentialSpeed <= Config_StaticFrictionSpeedThreshold)
		{
			TangentialVelocity = FVector::ZeroVector;
		}
		else
		{
			const float FrictionFactor = FMath::Clamp(1.f - (Config_KineticFrictionPerSecond * Dt), 0.f, 1.f);
			TangentialVelocity *= FrictionFactor;
		}

		Previous = Current - TangentialVelocity * Dt;

		RuntimeParticleHadContact[ParticleIndex] = true;
		RuntimeParticleContactNorm[ParticleIndex] = SafeNormal;
	}
}

void UClimbWorldStaticRopeComponent::Auth_EvaluateSleepAfterSubstep(float InFixedSubstepSeconds)
{
	if (!bConfig_EnableSleeping || !bSimulationEnabled || !bRopeAnchored || bIsFrozenByDistance)
	{
		SleepAccumulatedSecs = 0.f;
		return;
	}

	const float Dt = FMath::Max(InFixedSubstepSeconds, 0.001f);
	float MaxParticleSpeed = 0.f;

	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		const float Speed = (RuntimeParticlePos[ParticleIndex] - RuntimeParticlePrevPos[ParticleIndex]).Size() / Dt;
		MaxParticleSpeed = FMath::Max(MaxParticleSpeed, Speed);
	}

	const bool bBelowThresholds =
		(MaxParticleSpeed <= Config_SleepVelocityThreshold) &&
		(LastSubstepMaxCorr <= Config_SleepCorrectionThreshold);

	if (bBelowThresholds)
	{
		SleepAccumulatedSecs += Dt;
		if (SleepAccumulatedSecs >= Config_SleepDelaySeconds)
		{
			Auth_EnterSleep(TEXT("Velocity/correction thresholds satisfied"));
		}
	}
	else
	{
		SleepAccumulatedSecs = 0.f;
	}
}

void UClimbWorldStaticRopeComponent::Auth_WakeRope(const FString& InReason)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	SleepAccumulatedSecs = 0.f;

	if (!bIsSleeping)
	{
		return;
	}

	bIsSleeping = false;

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Rope woke. Reason=%s"), *InReason);

	HandleOnSleepStateChanged(false);
	OnRopeSleepStateChanged.Broadcast(false);

	Auth_PushNetworkSnapshot(true);
}

void UClimbWorldStaticRopeComponent::Auth_EnterSleep(const FString& InReason)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	// Zero residual velocity to avoid tiny wake-jitter loop.
	for (int32 ParticleIndex = 1; ParticleIndex < RuntimeParticlePos.Num(); ++ParticleIndex)
	{
		RuntimeParticlePrevPos[ParticleIndex] = RuntimeParticlePos[ParticleIndex];
	}

	SleepAccumulatedSecs = 0.f;

	if (bIsSleeping)
	{
		return;
	}

	bIsSleeping = true;

	UE_LOG(LogClimbWorldStaticRopeComponent, Log, TEXT("Rope entered sleep. Reason=%s"), *InReason);


	HandleOnSleepStateChanged(true);
	OnRopeSleepStateChanged.Broadcast(true);

	Auth_PushNetworkSnapshot(true);
}

void UClimbWorldStaticRopeComponent::Auth_UpdateDistanceFreezeState(float InDeltaSeconds)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !bRopeAnchored)
	{
		return;
	}

	if (!bFreezeByDistanceEnabled)
	{
		if (bIsFrozenByDistance)
		{
			bIsFrozenByDistance = false;
			Auth_WakeRope(TEXT("Distance-freeze disabled"));
			Auth_PushNetworkSnapshot(true);
		}
		return;
	}

	DistFreezeAccumulatorSecs += InDeltaSeconds;
	if (DistFreezeAccumulatorSecs < Config_DistanceFreezeCheckIntervalSeconds)
	{
		return;
	}
	DistFreezeAccumulatorSecs = 0.f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float FreezeDistanceSq = FMath::Square(FMath::Max(Config_FreezeDistanceFromPlayers, 1.f));
	bool bAnyPlayerNear = false;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;

		if (!IsValid(Pawn))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), AnchorWorldLoc);
		if (DistSq <= FreezeDistanceSq)
		{
			bAnyPlayerNear = true;
			break;
		}
	}

	const bool bShouldFreeze = !bAnyPlayerNear;

	if (bIsFrozenByDistance != bShouldFreeze)
	{
		bIsFrozenByDistance = bShouldFreeze;

		UE_LOG(
			LogClimbWorldStaticRopeComponent,
			Log,
			TEXT("Distance-freeze changed: %s"),
			bIsFrozenByDistance ? TEXT("FROZEN") : TEXT("UNFROZEN")
		);

		if (!bIsFrozenByDistance && bSimulationEnabled)
		{
			Auth_WakeRope(TEXT("Player returned in range"));
		}

		Auth_PushNetworkSnapshot(true);
	}
}


/* ==================== Runtime Data / Visual / Replication ==================== */

void UClimbWorldStaticRopeComponent::RefreshSplineFromRuntimeParticles()
{
	if (!RopeSpline)
	{
		return;
	}

	RopeSpline->ClearSplinePoints(false);

	if (RuntimeParticlePos.Num() == 0)
	{
		RopeSpline->UpdateSpline();
		return;
	}

	const FTransform ComponentTransform = GetComponentTransform();

	for (int32 Index = 0; Index < RuntimeParticlePos.Num(); ++Index)
	{
		const FVector LocalPoint = ComponentTransform.InverseTransformPosition(RuntimeParticlePos[Index]);
		RopeSpline->AddSplinePoint(LocalPoint, ESplineCoordinateSpace::Local, false);
		RopeSpline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}

	RopeSpline->UpdateSpline();
}

void UClimbWorldStaticRopeComponent::Auth_PushNetworkSnapshot(bool bInForceNetUpdate)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	ReplicatedNetworkSnapshot.SnapshotRevision++;
	ReplicatedNetworkSnapshot.bSnapshotAnchored = bRopeAnchored;
	ReplicatedNetworkSnapshot.bSnapshotLengthFinalized = bLengthFinalized;
	ReplicatedNetworkSnapshot.bSnapshotSimulationEnabled = bSimulationEnabled;
	ReplicatedNetworkSnapshot.bSnapshotSleeping = bIsSleeping;
	ReplicatedNetworkSnapshot.bSnapshotFrozenByDistance = bIsFrozenByDistance;

	ReplicatedNetworkSnapshot.ParticleWorldPositions.Reset();
	ReplicatedNetworkSnapshot.ParticleWorldPositions.Reserve(RuntimeParticlePos.Num());

	for (const FVector& P : RuntimeParticlePos)
	{
		ReplicatedNetworkSnapshot.ParticleWorldPositions.Add(P);
	}

	if (bInForceNetUpdate)
	{
		OwnerActor->ForceNetUpdate();
	}
}

void UClimbWorldStaticRopeComponent::RebuildRuntimeParticlesFromSnapshot()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		return;
	}

	// Prevent duplicate work.
	if (ReplicatedNetworkSnapshot.SnapshotRevision == LastAppliedSnapshotRev)
	{
		return;
	}
	LastAppliedSnapshotRev = ReplicatedNetworkSnapshot.SnapshotRevision;

	// Mirror state booleans from snapshot for robustness.
	bRopeAnchored = ReplicatedNetworkSnapshot.bSnapshotAnchored;
	bLengthFinalized = ReplicatedNetworkSnapshot.bSnapshotLengthFinalized;
	bSimulationEnabled = ReplicatedNetworkSnapshot.bSnapshotSimulationEnabled;
	bIsSleeping = ReplicatedNetworkSnapshot.bSnapshotSleeping;
	bIsFrozenByDistance = ReplicatedNetworkSnapshot.bSnapshotFrozenByDistance;

	RuntimeParticlePos.Reset();
	RuntimeParticlePrevPos.Reset();
	RuntimeParticleHadContact.Reset();
	RuntimeParticleContactNorm.Reset();

	const int32 NumParticles = ReplicatedNetworkSnapshot.ParticleWorldPositions.Num();
	if (NumParticles <= 0)
	{
		CurrSegments = 0;
		return;
	}

	RuntimeParticlePos.SetNum(NumParticles);
	RuntimeParticlePrevPos.SetNum(NumParticles);
	RuntimeParticleHadContact.SetNum(NumParticles);
	RuntimeParticleContactNorm.SetNum(NumParticles);

	for (int32 Index = 0; Index < NumParticles; ++Index)
	{
		const FVector Position = ReplicatedNetworkSnapshot.ParticleWorldPositions[Index];
		RuntimeParticlePos[Index] = Position;
		RuntimeParticlePrevPos[Index] = Position;
		RuntimeParticleHadContact[Index] = false;
		RuntimeParticleContactNorm[Index] = FVector::ZeroVector;
	}
	
	CurrSegments = FMath::Max(0, NumParticles - 1);
}


/* ==================== Utility ==================== */

int32 UClimbWorldStaticRopeComponent::QuantizeLengthToSegments(float InLength) const
{
	const float SafeSegmentLength = FMath::Max(Config_SegmentLength, RopeDataFallback);
	const int32 DesiredSegments = FMath::RoundToInt(InLength / SafeSegmentLength);
	return FMath::Clamp(DesiredSegments, Config_MinSegments, Config_MaxSegments);
}

int32 UClimbWorldStaticRopeComponent::GetExpectedParticleCount() const
{
	return FMath::Max(CurrSegments, 1) + 1;
}

float UClimbWorldStaticRopeComponent::GetTargetSegmentLength() const
{
	return FMath::Max(Config_SegmentLength, RopeDataFallback);
}

bool UClimbWorldStaticRopeComponent::HasValidRuntimeParticles() const
{
	return RuntimeParticlePos.Num() >= 2 &&
		RuntimeParticlePrevPos.Num() == RuntimeParticlePos.Num();
}

FVector UClimbWorldStaticRopeComponent::ClosestPointOnSegment(
	const FVector& InPoint,
	const FVector& InSegmentStart,
	const FVector& InSegmentEnd,
	float& OutSegmentT
)
{
	const FVector Segment = InSegmentEnd - InSegmentStart;
	const float SegmentLengthSq = Segment.SizeSquared();

	if (SegmentLengthSq <= KINDA_SMALL_NUMBER)
	{
		OutSegmentT = 0.f;
		return InSegmentStart;
	}

	const float T = FVector::DotProduct(InPoint - InSegmentStart, Segment) / SegmentLengthSq;
	OutSegmentT = FMath::Clamp(T, 0.f, 1.f);

	return InSegmentStart + Segment * OutSegmentT;
}

void UClimbWorldStaticRopeComponent::DrawDebugRopeState() const
{
	if (!bConfig_DebugDrawRope && !bConfig_DebugDrawContacts && !bConfig_DebugDrawSleep)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || RuntimeParticlePos.Num() == 0)
	{
		return;
	}

	const FColor RopeColor =
		bIsFrozenByDistance ? FColor::Cyan :
		(bIsSleeping ? FColor::Yellow : FColor::Green);

	if (bConfig_DebugDrawRope)
	{
		for (int32 Index = 0; Index < RuntimeParticlePos.Num() - 1; ++Index)
		{
			DrawDebugLine(
				World,
				RuntimeParticlePos[Index],
				RuntimeParticlePos[Index + 1],
				RopeColor,
				false,
				Config_DebugDrawDuration,
				0,
				1.5f
			);
		}

		for (const FVector& Particle : RuntimeParticlePos)
		{
			DrawDebugSphere(
				World,
				Particle,
				2.5f,
				8,
				RopeColor,
				false,
				Config_DebugDrawDuration
			);
		}
	}

	if (bConfig_DebugDrawContacts && RuntimeParticleHadContact.Num() == RuntimeParticlePos.Num())
	{
		for (int32 Index = 1; Index < RuntimeParticlePos.Num(); ++Index)
		{
			if (!RuntimeParticleHadContact[Index])
			{
				continue;
			}

			const FVector Start = RuntimeParticlePos[Index];
			const FVector End = Start + RuntimeParticleContactNorm[Index] * 25.f;

			DrawDebugLine(World, Start, End, FColor::Orange, false, Config_DebugDrawDuration, 0, 1.5f);
		}
	}

	if (bConfig_DebugDrawSleep)
	{
		DrawDebugSphere(
			World,
			AnchorWorldLoc,
			8.f,
			12,
			bIsSleeping ? FColor::Yellow : FColor::Red,
			false,
			Config_DebugDrawDuration
		);
	}
}