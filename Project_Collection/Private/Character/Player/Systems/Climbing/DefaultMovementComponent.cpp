// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Player/Systems/Climbing/DefaultMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"

#include "DrawDebugHelpers.h"


/**
 * ======================================================================
 * ATTENTION!
 * 
 * Currently, this file is NOT "Default", nor as the main router.
 * Right now this file handles climbing only.
 * To support more movement later while maintaining clean file layout,
 * it is planned to separate .cpp to wrap different data and functions.
 * ======================================================================
 */


//
UDefaultMovementComponent::UDefaultMovementComponent()
{
}

/* ==================== Overridden Functions ==================== */


void UDefaultMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		DefaultCapsuleHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	}
}


void UDefaultMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UDefaultMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// setup climb mode
	if (IsClimbing())
	{
		bOrientRotationToMovement = false;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(Climb_CapsuleHalfHeight);
		}

		OnEnter_ClimbStateDelegate.ExecuteIfBound();
	}

	// Clean up climb mode.
	// NOTE: skip this when we are transitioning Climb -> LedgeClimb (both are MOVE_Custom).
	// We must NOT restore the full capsule / reset rotation in the middle of the mantle;
	// the small climb capsule is intentionally kept for lip clearance, and cleanup happens
	// only when the mantle itself finishes (see the LedgeClimb block below).
	if (PreviousMovementMode == MOVE_Custom
		&& PreviousCustomMode == ECustomMovementMode::MOVE_Climb
		&& !IsLedgeClimbing())
	{
		bOrientRotationToMovement = true;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			const float RestoreHalfHeight = (DefaultCapsuleHalfHeight > 0.f) ? DefaultCapsuleHalfHeight : FallBackCapsuleHalfHeight;
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(RestoreHalfHeight);
		}

		const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);
		UpdatedComponent->SetRelativeRotation(CleanStandRotation);

		StopMovementImmediately();

		OnExit_ClimbStateDelegate.ExecuteIfBound();
	}

	// NEW: clean up after the coded ledge-climb (mantle) finishes.
	// Restoring the capsule here (with the target centred at surface + full half-height)
	// makes the feet land exactly on the surface with no pop.
	if (PreviousMovementMode == MOVE_Custom
		&& PreviousCustomMode == ECustomMovementMode::MOVE_ClimbLedge)
	{
		bOrientRotationToMovement = true;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			const float RestoreHalfHeight = (DefaultCapsuleHalfHeight > 0.f) ? DefaultCapsuleHalfHeight : FallBackCapsuleHalfHeight;
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(RestoreHalfHeight);
		}

		const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);
		UpdatedComponent->SetRelativeRotation(CleanStandRotation);

		StopMovementImmediately();

		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] LedgeClimb finished -> Walking, capsule restored."));

		// Treat climb + mantle as one session: broadcast the exit here.
		OnExit_ClimbStateDelegate.ExecuteIfBound();
	}

	
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}


void UDefaultMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (IsClimbing())
	{
		PhysClimb(deltaTime, Iterations);
		return;
	}
	
	// NEW: run the coded mantle physics when in the ledge-climb mode.
	if (IsLedgeClimbing())
	{
		PhysLedgeClimb(deltaTime, Iterations);
		return;
	}

	Super::PhysCustom(deltaTime, Iterations);
}


float UDefaultMovementComponent::GetMaxSpeed() const
{
	if (IsClimbing())
	{
		return Climb_MaxSpeed;
	}

	return Super::GetMaxSpeed();
}


float UDefaultMovementComponent::GetMaxAcceleration() const
{
	if (IsClimbing())
	{
		return Climb_MaxAcceleration;
	}

	return Super::GetMaxAcceleration();
}



/**
 * =============================================================
 * ==================== Climb Movement Mode ====================
 * =============================================================
 */



/* ==================== APIs ==================== */


void UDefaultMovementComponent::Request_ToggleClimbing(bool bEnableClimb)
{
	if (!CanStartClimbing())
	{return;}
	
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{
		RpcServer_ToggleClimbing(bEnableClimb);
		return;
	}

	Auth_ToggleClimbing(bEnableClimb);
}


/* ==================== Queries ==================== */


bool UDefaultMovementComponent::IsClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
}


bool UDefaultMovementComponent::IsLedgeClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_ClimbLedge;
}


FVector UDefaultMovementComponent::GetLocalSpaceVelocity() const
{
	if (!UpdatedComponent)
	{
		return Velocity;
	}

	return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}


/* ==================== Internal Function ==================== */

/* ----- Traces ----- */


TArray<FHitResult> UDefaultMovementComponent::DoCapsuleTraceMultiByObject(
	const FVector& Start,
	const FVector& End,
	bool bShowDebugShape,
	bool bDrawPersistantShapes
)
{
	TArray<FHitResult> OutCapsuleTraceHitResults;

	EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;
	if (bShowDebugShape)
	{
		DebugTraceType = bDrawPersistantShapes ? EDrawDebugTrace::Persistent : EDrawDebugTrace::ForOneFrame;
	}

	if (ClimbableSurfaceTraceTypes.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[ClimbingMovement] DoCapsuleTraceMultiByObject - ClimbableSurfaceTraceTypes is EMPTY! Please set object types in component settings."));
	}
	
	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		Start,
		End,
		Climb_CapsuleTraceRadius,
		Climb_CapsuleTraceHalfHeight,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutCapsuleTraceHitResults,
		false
	);
	
	// UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoCapsuleTraceMultiByObject - Found %d hits"), OutCapsuleTraceHitResults.Num());

	return OutCapsuleTraceHitResults;
}

FHitResult UDefaultMovementComponent::DoLineTraceSingleByObject(
	const FVector& Start,
	const FVector& End,
	bool bShowDebugShape,
	bool bDrawPersistantShapes
)
{
	FHitResult OutHit;

	EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;
	if (bShowDebugShape)
	{
		DebugTraceType = bDrawPersistantShapes ? EDrawDebugTrace::Persistent : EDrawDebugTrace::ForOneFrame;
	}

	if (ClimbableSurfaceTraceTypes.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[ClimbingMovement] DoLineTraceSingleByObject - ClimbableSurfaceTraceTypes is EMPTY! Please set object types in component settings."));
	}
	
	UKismetSystemLibrary::LineTraceSingleForObjects(
		this,
		Start,
		End,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutHit,
		false
	);
	
	/*
	if (OutHit.bBlockingHit)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoLineTraceSingleByObject - Hit actor: %s at location: %s"),
			*GetNameSafe(OutHit.GetActor()), *OutHit.ImpactPoint.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoLineTraceSingleByObject - No hit detected"));
	}
	*/

	return OutHit;
}


bool UDefaultMovementComponent::TraceClimbableSurfaces()
{
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * Climb_ForwardTraceStartOffset;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * Climb_ForwardTraceDistance;

	Climb_ClimableSurfaceMultiTracedResults = DoCapsuleTraceMultiByObject(Start, End);
	const bool bFoundSurfaces = !Climb_ClimableSurfaceMultiTracedResults.IsEmpty();
	
	if (!bFoundSurfaces)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] TraceClimbableSurfaces - No surfaces found (check ClimbableSurfaceTraceTypes configuration)"));
	}
	
	return bFoundSurfaces;
}


FHitResult UDefaultMovementComponent::TraceFromEyeHeight(
	float TraceDistance,
	float TraceStartHeightOffset,
	bool bShowDebugShape,
	bool bDrawPersistantShapes
)
{
	if (!CharacterOwner)
	{
		return FHitResult();
	}

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector EyeHeightOffset =
		UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartHeightOffset);

	const FVector Start = ComponentLocation + EyeHeightOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

	return DoLineTraceSingleByObject(Start, End, bShowDebugShape, bDrawPersistantShapes);
}

/* ----- Core ----- */


void UDefaultMovementComponent::RpcServer_ToggleClimbing_Implementation(bool bEnableClimb)
{
	Auth_ToggleClimbing(bEnableClimb);
}


void UDefaultMovementComponent::Auth_ToggleClimbing(bool bEnableClimb)
{
	if (bEnableClimb)
	{
		if (CanStartClimbing())
		{
			StartClimbing();
			StopMovementImmediately();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing from server - conditions not met"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ToggleClimbing(false) - stopping climb"));
		StopClimbing();
	}
}


bool UDefaultMovementComponent::CanStartClimbing()
{
	// ANCHOR: Planned to allow it, maybe depends on falling speed (then slides)
	if (IsFalling())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing - character is falling"));
		return false;
	}
	
	if (!TraceClimbableSurfaces())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing - no climbable surfaces detected in forward trace"));
		return false;
	}
	
	FHitResult EyeHeightHit = TraceFromEyeHeight(Climb_EyeHeightTraceDistance);
	if (!EyeHeightHit.bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing - eye height trace did not hit climbable surface"));
		return false;
	}

	return true;
}


void UDefaultMovementComponent::StartClimbing()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{return;}
	
	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_Climb);
}


void UDefaultMovementComponent::StopClimbing()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{return;}
	
	SetMovementMode(MOVE_Falling);
}


void UDefaultMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{return;}

	// Cache States
	TraceClimbableSurfaces();
	ProcessClimableSurfaceInfo();

	// Check should stop climbing
	if (CheckShouldStopClimbing() || CheckHasReachedFloor())
	{
		StopClimbing();
		return;
	}

	// Update Desired @Velocity based on input (later overwrite)
	CalcVelocity(deltaTime, 0.f, true, Climb_MaxBreakDeceleration);

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector DesiredTickDisplacement = Velocity * deltaTime;
	FHitResult Hit(1.f);

	// Tries to move (climb) and rotate to Desired values
	SafeMoveUpdatedComponent(DesiredTickDisplacement, Climb_CalculateSurfaceAlignedRot(deltaTime), true, Hit);
	
	if (Hit.Time < 1.f)
	{
		// Unreal resolve impact and slide
		HandleImpact(Hit, deltaTime, DesiredTickDisplacement);
		SlideAlongSurface(DesiredTickDisplacement, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	// Rewrite @Velocity to actual movement
	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;

	// Snap movement to climbable surface
	Climb_SnapMovementToSurfaces(deltaTime);


	// NEW: when we climb into the top lip while moving up, hand off to the coded mantle.
	// TryStartLedgeClimb() switches movement mode, after which
	// IsClimbing() becomes false and PhysClimb stops running (no repeated triggering).
	if (CheckHasReachedLedge())
	{
		if (LedgeClimbMethod == ELedgeClimbMethod::RootMotion)
		{
			UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] Reached ledge, but RootMotion is selected. Coded ledge climb is disabled until RootMotion is implemented."));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] Reached ledge -> attempting coded ledge climb."));
			Auth_TryStartLedgeClimb();
		}
	}
}


void UDefaultMovementComponent::ProcessClimableSurfaceInfo()
{
	Climb_CurrentSurfaceLocation = FVector::ZeroVector;
	Climb_CurrentSurfaceNormal = FVector::ZeroVector;

	if (Climb_ClimableSurfaceMultiTracedResults.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ProcessClimableSurfaceInfo - No traced results available"));
		return;
	}

	// Average out all the traced surfaces to get a more stable, representative location and normal
	for (const FHitResult& TracedHitResult : Climb_ClimableSurfaceMultiTracedResults)
	{
		Climb_CurrentSurfaceLocation += TracedHitResult.ImpactPoint;
		Climb_CurrentSurfaceNormal += TracedHitResult.ImpactNormal;
	}

	Climb_CurrentSurfaceLocation /= Climb_ClimableSurfaceMultiTracedResults.Num();
	Climb_CurrentSurfaceNormal = Climb_CurrentSurfaceNormal.GetSafeNormal();
	
	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] ProcessClimableSurfaceInfo - Surface Location: %s, Normal: %s"),
		*Climb_CurrentSurfaceLocation.ToString(), *Climb_CurrentSurfaceNormal.ToString());
}



bool UDefaultMovementComponent::CheckShouldStopClimbing()
{
	if (Climb_ClimableSurfaceMultiTracedResults.IsEmpty()) {return true;}

	// Check if surface is too horizontal to climb (e.g. floor or ceiling)
	const float DotResult = FMath::Clamp(
		FVector::DotProduct(Climb_CurrentSurfaceNormal, FVector::UpVector),
		-1.f, 1.f
	);

	const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));

	// Surface too horizontal => stop climb.
	return DegreeDiff <= Climb_MaxSurfaceNormalFromUp;	
}


bool UDefaultMovementComponent::CheckHasReachedFloor()
{
	const FVector DownVector = -UpdatedComponent->GetUpVector();
	const FVector StartOffset = DownVector * 50.f;

	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + DownVector;

	const TArray<FHitResult> PossibleFloorHits = DoCapsuleTraceMultiByObject(Start, End);
	if (PossibleFloorHits.IsEmpty())
		{return false;}

	for (const FHitResult& PossibleFloorHit : PossibleFloorHits)
	{
		const bool bFloorReached =
			FVector::Parallel(-PossibleFloorHit.ImpactNormal, FVector::UpVector) &&
			GetLocalSpaceVelocity().Z < -10.f;

		if (bFloorReached)
		{
			return true;
		}
	}

	return false;
}


FQuat UDefaultMovementComponent::Climb_CalculateSurfaceAlignedRot(float DeltaTime)
{
	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();
	// Make a Rotator pointing into the surface
	const FQuat TargetQuat = FRotationMatrix::MakeFromX(-Climb_CurrentSurfaceNormal).ToQuat();

	return FMath::QInterpTo(CurrentQuat, TargetQuat, DeltaTime, 5.f);
}


void UDefaultMovementComponent::Climb_SnapMovementToSurfaces(float DeltaTime)
{
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector ComponentWrldLoc = UpdatedComponent->GetComponentLocation();

	const FVector RelativeDistance = Climb_CurrentSurfaceLocation - ComponentWrldLoc;

	// Estimate how far the surface point is along player forward (not the true perpendicular distance).
	const FVector DistanceProjectedToForward =
		(RelativeDistance).ProjectOnTo(ComponentForward);

	// Move toward the surface along the inward normal by the estimated distance.
	// ignoring length signs because it is impossible to climb on your back.
	const FVector SnapToSurfaceVector = -Climb_CurrentSurfaceNormal * DistanceProjectedToForward.Length();
	const FVector TickSnapAmount = SnapToSurfaceVector * DeltaTime * Climb_MaxSpeed;
	
	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] SnapMovementToClimableSurfaces - SnapVector: %s, FinalSnapAmount: %s"),
		*SnapToSurfaceVector.ToString(), *TickSnapAmount.ToString());

	UpdatedComponent->MoveComponent(
		TickSnapAmount,
		UpdatedComponent->GetComponentQuat(),
		true
	);
}


bool UDefaultMovementComponent::QueryLedgeTopSurface(FHitResult& OutTopSurfaceHit, FVector& OutForwardProbeEnd, bool bDrawDebug)
{
	const FHitResult ForwardClearHit = TraceFromEyeHeight(Climb_EyeHeightTraceDistance, LedgeClimb_TopProbeUpOffset, bDrawDebug);

	if (ForwardClearHit.bBlockingHit)
	{
		return false;
	}

	OutForwardProbeEnd = ForwardClearHit.TraceEnd;

	const FVector DownVector = -UpdatedComponent->GetUpVector();
	const FVector DownEnd = OutForwardProbeEnd + DownVector * LedgeClimb_TopProbeDownDistance;

	OutTopSurfaceHit = DoLineTraceSingleByObject(OutForwardProbeEnd, DownEnd, bDrawDebug);
	return OutTopSurfaceHit.bBlockingHit;
}


/* ----- Climb Ledge ----- */


bool UDefaultMovementComponent::CheckHasReachedLedge()
{
	FHitResult TopSurfaceHit;
	FVector ForwardProbeEnd;

	if (QueryLedgeTopSurface(TopSurfaceHit, ForwardProbeEnd, false) && GetLocalSpaceVelocity().Z > 10.f)
	{
		return true;
	}

	return false;
}


bool UDefaultMovementComponent::CalcLedgeClimbTarget(FVector& OutLandLocation)
{
	OutLandLocation = FVector::ZeroVector;

	if (!CharacterOwner)
	{return false;}

	FHitResult TopSurfaceHit;
	FVector ForwardProbeEnd;

	// check again to cache data //ANCHOR: might
	if (!QueryLedgeTopSurface(TopSurfaceHit, ForwardProbeEnd, bClimb_DebugDraw))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] CalcLedgeClimbTarget - no top surface found below the lip."));
		return false;
	}

	// Reject surfaces that are too steep to stand on (e.g. a slanted underside).
	const float SurfaceDotUp = FVector::DotProduct(TopSurfaceHit.ImpactNormal, FVector::UpVector);
	if (SurfaceDotUp < GetWalkableFloorZ()) // CMC setting
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] CalcLedgeClimbTarget - top surface not walkable (dot %.3f < floorZ %.3f)."),
			SurfaceDotUp, GetWalkableFloorZ());
		return false;
	}

	// Build the final capsule rest location
	// - Use the FULL default half-height (not the shrunk climb height) so that when the capsule is restored on
	// mantle exit, the feet sit on the surface.
	// - Push forward onto the surface so we don't land balancing on the edge.
	const float FullHalfHeight = (DefaultCapsuleHalfHeight > 0.f) ? DefaultCapsuleHalfHeight : FallBackCapsuleHalfHeight;

	// Horizontal "into the surface" direction (the way we travel onto the top).
	FVector HorizForward = -Climb_CurrentSurfaceNormal;
	HorizForward.Z = 0.f;
	if (!HorizForward.Normalize())
	{
		// Fallback if the surface normal was near-vertical/degenerate.
		HorizForward = UpdatedComponent->GetForwardVector();
		HorizForward.Z = 0.f;
		HorizForward.Normalize();
	}

	OutLandLocation =
		TopSurfaceHit.ImpactPoint
		+ FVector::UpVector * (FullHalfHeight + 2.f)      // +2 skin to avoid starting embedded
		+ HorizForward * LedgeClimb_ForwardLandOffset;

	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] CalcLedgeClimbTarget - land location: %s"), *OutLandLocation.ToString());
	return true;
}


void UDefaultMovementComponent::Auth_TryStartLedgeClimb()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{return;}

	if (LedgeClimbMethod == ELedgeClimbMethod::RootMotion)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] Auth_TryStartLedgeClimb skipped: RootMotion ledge climb selected and not yet implemented."));
		return;
	}

	FVector LandLocation;
	if (!CalcLedgeClimbTarget(LandLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] TryStartLedgeClimb - no valid target, staying on wall."));
		return;
	}

	// --- Build the L-shaped path ---
	// Start: where we currently are on the wall.
	// OverLedge: straight up from Start, to just above the target surface (+clearance) so the lip is cleared.
	// Target: the final rest spot on top.
	LedgeClimb_StartLocation    = UpdatedComponent->GetComponentLocation();
	LedgeClimb_TargetLocation   = LandLocation;
	LedgeClimb_OverLedgeLocation = FVector(
		LedgeClimb_StartLocation.X,
		LedgeClimb_StartLocation.Y,
		LandLocation.Z + LedgeClimb_VerticalClearance);

	// Final facing: upright, looking across the top surface (the direction we travel onto it).
	FVector HorizForward = -Climb_CurrentSurfaceNormal;
	HorizForward.Z = 0.f;
	if (!HorizForward.Normalize())
	{
		HorizForward = UpdatedComponent->GetForwardVector();
		HorizForward.Z = 0.f;
		HorizForward.Normalize();
	}
	LedgeClimb_TargetRotation = FRotationMatrix::MakeFromXZ(HorizForward, FVector::UpVector).ToQuat();

	LedgeClimb_Alpha = 0.f;

	// --- Debug: draw the whole planned path so you can eyeball it against the geometry ---
	if (bClimb_DebugDraw && GetWorld())
	{
		DrawDebugSphere(GetWorld(), LedgeClimb_StartLocation,     12.f, 12, FColor::Green,  false, 4.f);
		DrawDebugSphere(GetWorld(), LedgeClimb_OverLedgeLocation, 12.f, 12, FColor::Yellow, false, 4.f);
		DrawDebugSphere(GetWorld(), LedgeClimb_TargetLocation,    12.f, 12, FColor::Red,    false, 4.f);
		DrawDebugLine(GetWorld(), LedgeClimb_StartLocation,     LedgeClimb_OverLedgeLocation, FColor::Cyan, false, 4.f, 0, 2.f);
		DrawDebugLine(GetWorld(), LedgeClimb_OverLedgeLocation, LedgeClimb_TargetLocation,    FColor::Cyan, false, 4.f, 0, 2.f);
	}

	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] LedgeClimb START | Start:%s Over:%s Target:%s"),
		*LedgeClimb_StartLocation.ToString(),
		*LedgeClimb_OverLedgeLocation.ToString(),
		*LedgeClimb_TargetLocation.ToString());

	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_ClimbLedge);
}



void UDefaultMovementComponent::PhysLedgeClimb(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Advance normalized progress.
	LedgeClimb_Alpha += deltaTime / FMath::Max(LedgeClimb_Duration, KINDA_SMALL_NUMBER);
	const float ClampedAlpha = FMath::Clamp(LedgeClimb_Alpha, 0.f, 1.f);

	// --- Pick the point on the two-segment path for this alpha ---
	// Segment 1 (0 .. PhaseSplit):   Start      -> OverLedge   (rise, clear the lip)
	// Segment 2 (PhaseSplit .. 1):   OverLedge  -> Target      (move over and settle)
	FVector DesiredLocation;
	if (ClampedAlpha <= LedgeClimb_PhaseSplit)
	{
		const float SegAlpha = ClampedAlpha / LedgeClimb_PhaseSplit;
		DesiredLocation = FMath::Lerp(LedgeClimb_StartLocation, LedgeClimb_OverLedgeLocation, SegAlpha);
	}
	else
	{
		const float SegAlpha = (ClampedAlpha - LedgeClimb_PhaseSplit) / (1.f - LedgeClimb_PhaseSplit);
		DesiredLocation = FMath::Lerp(LedgeClimb_OverLedgeLocation, LedgeClimb_TargetLocation, SegAlpha);
	}

	// Delta from the capsule's ACTUAL location (not the ideal), so if a previous tick got
	// blocked we naturally try to catch up next tick instead of drifting.
	const FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	const FVector MoveDelta = DesiredLocation - CurrentLocation;

	// Interp rotation toward the upright "on top" facing.
	const FQuat NewQuat = FMath::QInterpTo(UpdatedComponent->GetComponentQuat(), LedgeClimb_TargetRotation, deltaTime, 5.f);

	// Swept move: respects collision. If our math sends the capsule into geometry, it stops
	// here (and we log/draw) rather than tunneling — that's your debugging signal.
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(MoveDelta, NewQuat, true, Hit);

	if (Hit.IsValidBlockingHit())
	{
		// Don't fight the wall; just report it. If you see this firing a lot, increase
		// LedgeClimb_VerticalClearance or your trace offsets.
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] PhysLedgeClimb BLOCKED at alpha %.2f by %s (normal %s)"),
			ClampedAlpha, *GetNameSafe(Hit.GetActor()), *Hit.Normal.ToString());

		if (bClimb_DebugDraw && GetWorld())
		{
			DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 14.f, FColor::Red, false, 2.f);
		}
	}

	// Keep Velocity coherent for anything reading it (anim/UI), based on what actually moved.
	Velocity = (UpdatedComponent->GetComponentLocation() - CurrentLocation) / deltaTime;

	// --- Debug: current desired vs actual ---
	if (bClimb_DebugDraw && GetWorld())
	{
		DrawDebugSphere(GetWorld(), DesiredLocation, 6.f, 8, FColor::Magenta, false, -1.f);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[ClimbingMovement] PhysLedgeClimb | alpha:%.2f desired:%s actual:%s"),
		ClampedAlpha, *DesiredLocation.ToString(), *UpdatedComponent->GetComponentLocation().ToString());

	// Done -> hand back to normal walking (capsule restore happens in OnMovementModeChanged).
	if (LedgeClimb_Alpha >= 1.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] LedgeClimb COMPLETE."));
		SetMovementMode(MOVE_Walking);
	}
}


bool UDefaultMovementComponent::CanClimbDownLedge()
{
	if(IsFalling()) return false;
	 
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	const FVector WalkableSurfaceTraceStart = ComponentLocation + ComponentForward * Climb_DownWalkableSurfaceTraceOffset;
	const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

	FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

	const FVector LedgeTraceStart = WalkableSurfaceHit.TraceStart + ComponentForward * Climb_DownLedgeTraceOffset;
	const FVector LedgeTraceEnd = LedgeTraceStart + DownVector * 200.f;

	FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd);

	if(WalkableSurfaceHit.bBlockingHit && !LedgeTraceHit.bBlockingHit)
	{
		return true;
	}

	return false;
}
