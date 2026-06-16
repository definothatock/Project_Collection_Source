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
		// match walk angle with stop climb angle, with a bit of spare.
		// this->SetWalkableFloorAngle(90.f - Climb_StopClimbAngleFromUpDeg + 5.f);
	}
}


void UDefaultMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// NEW
	TickExitUprightBlend(DeltaTime);
}


void UDefaultMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] OnMovementModeChanged - Current Mode switched to: %s (%hhu)"),
		*UEnum::GetDisplayValueAsText(MovementMode).ToString(),
		CustomMovementMode);
	
	// CLIMB: ENTER
	if (IsClimbing())
	{
		bOrientRotationToMovement = false; // make rotation driven by the surface normal

		// NEW
		Climb_TimeSinceEntered = 0.f;
		bClimb_ExitUprightBlendActive = false;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(Climb_CapsuleHalfHeight);
		}

		// SLIDING ENTRY
		// Player may be entering with large velocity. Keep that velocity:
		// 1. Raw velocity is projected onto the Surface plane, keeping only the tangential speed.
		//		This makes flying straight INTO a wall stop almost instantly, Game-ish feel.
		// 2. clamp to Climb_MaxEntrySlideSpeed so a long fall can't produce a crazy slide.
		//		The actual deceleration happens in PhysClimb() while bClimb_IsEntrySliding is true.

		// CanStartClimbing() traced the surface but never averaged the normal, now do it again just to make sure.
		// ANCHOR: improve this flow, seems duplicated.
		TraceAndCacheClimbableSurfaces();
		AveragesClimableSurfaceInfo();

		const FVector RawEntryVelocity = Velocity;
		FVector PlaneEntryVelocity = RawEntryVelocity;
		
		if (!Climb_CurrentSurfaceNormal.IsNearlyZero())
		{
			// V - (V . N) * N, removes orthogonal component.
			PlaneEntryVelocity = FVector::VectorPlaneProject(RawEntryVelocity, Climb_CurrentSurfaceNormal);
		}
		else if (bClimb_DebugLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Enter climb - surface normal was ZERO, entry velocity NOT plane-projected. Check trace setup."));
		}

		// cap speed
		if (PlaneEntryVelocity.Size() > Climb_MaxEntrySlideSpeed)
		{
			PlaneEntryVelocity = PlaneEntryVelocity.GetSafeNormal() * Climb_MaxEntrySlideSpeed;
		}

		// set Velocity
		Velocity = PlaneEntryVelocity;

		// Flag sliding when faster than a normal climb by this much.
		bClimb_IsEntrySliding = (Velocity.Size() > Climb_MaxSpeed * SlideEntryOverspeedMultiplier);

		// Kill Velocity when not sliding, let player have the control
		if (!bClimb_IsEntrySliding)
		{
			StopMovementImmediately();
		}
		
		OnEnter_ClimbStateDelegate.ExecuteIfBound();
		
		// --- Debug: sliding state and data ---
		if (bClimb_DebugLog)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Enter climb | RawVel=%s (|v|=%.1f) -> PlaneVel=%s (|v|=%.1f) | Normal=%s | EntrySliding=%s"),
				*RawEntryVelocity.ToString(), RawEntryVelocity.Size(),
				*Velocity.ToString(), Velocity.Size(),
				*Climb_CurrentSurfaceNormal.ToString(),
				bClimb_IsEntrySliding ? TEXT("YES") : TEXT("no"));
		}
		if (bClimb_DebugDraw && bClimb_IsEntrySliding && GetWorld() && UpdatedComponent)
		{
			// the velocity we are entering the surface with (after projection).
			const FVector Loc = UpdatedComponent->GetComponentLocation();
			DrawDebugDirectionalArrow(GetWorld(), Loc, Loc + Velocity, 30.f, FColor::Cyan, false, 3.f, 0, 2.f);
		}
		
	}

	// CLIMB: EXIT
	// Skipped when transitioning Climb -> LedgeClimb.
	// Full capsule / reset rotation is skipped; until the mantle finished.
	if (PreviousMovementMode == MOVE_Custom
		&& PreviousCustomMode == ECustomMovementMode::MOVE_Climb
		&& !IsLedgeClimbing())
	{
		// bOrientRotationToMovement = true;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			const float RestoreHalfHeight = (DefaultCapsuleHalfHeight > 0.f) ? DefaultCapsuleHalfHeight : FallBackCapsuleHalfHeight;
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(RestoreHalfHeight);
		}

		// const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		// const FRotator CleanStandRotation = FRotator(0.f, DirtyRotation.Yaw, 0.f);
		// UpdatedComponent->SetRelativeRotation(CleanStandRotation);

		// NEW
		ExitUprightBlend();       // smooth pitch/roll to 0
		
		bClimb_IsEntrySliding = false;

		OnExit_ClimbStateDelegate.ExecuteIfBound();
	}

	// CLIMB: EXIT LEDGE
	// makes the feet land exactly on the surface with no pop.
	// ANCHOR: Significant overlapping with MOVE_Climb. Consider combined.
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

		// NEW
		Climb_TimeSinceEntered += deltaTime;

		return;
	}
	
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
 *
 * TODO:
 * 1. Convert ClimbEdge to vaulting. Bad player experience when they cant just skip the standard climbing to ClimbEdge.
 * 2. Add Climb Hop, allowing player to climb faster.
 * 3. Change the walkable edge detection to simple edge detection. Up to player to decide where they wanna go,
 * or vault into.
 * 4. When exiting from climb, should push player forward a bit, to make sure player wont slide down the slope.
 * 
 * 7. TraceAndCacheClimbableSurfaces() and AveragesClimableSurfaceInfo() seems to be using everywhere. Check again
 * if there are needs for that many check.
 * 8. When tracing multisurface, if some objects have bad overlaps (eg a corner with asset overlapping each other),
 * The orientation snapping would orient to the overlapping area and cause stuttering until moved far enough
 * 9. Exit lock is not implemented
 * 
 * Note:
 * - @UpdatedComponent for movement/rotation/traces; CharacterOwner->GetCapsuleComponent() for query/APIs. This how
 *		CMC works internally, safer.
 */



/* ==================== APIs ==================== */


void UDefaultMovementComponent::Request_ToggleClimbing(bool bWantsClimb)
{
	if (bClimb_DebugLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClimbingMovement] >> Request_ToggleClimbing(enable=%d) | LocalRole=%d HasAuth=%d Mode=%d Custom=%d IsClimbing=%d IsFalling=%d"),
			bWantsClimb ? 1 : 0,
			CharacterOwner ? (int32)CharacterOwner->GetLocalRole() : -1,
			(CharacterOwner && CharacterOwner->HasAuthority()) ? 1 : 0,
			(int32)MovementMode, (int32)CustomMovementMode,
			IsClimbing() ? 1 : 0, IsFalling() ? 1 : 0);

		// Uncomment to dump the C++ call stack and find the exact second caller:
		// FDebug::DumpStackTraceToLog(ELogVerbosity::Log);
	}

	
	if (bWantsClimb && !CanStartClimbing())
	{
		return;
	}

	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{
		RpcServer_ToggleClimbing(bWantsClimb);
		return;
	}

	Auth_ToggleClimbing(bWantsClimb);
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
) const
{
	bShowDebugShape = bShowDebugShape && bClimb_MasterDebugTrace;
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
	
	return OutCapsuleTraceHitResults;
}

FHitResult UDefaultMovementComponent::DoLineTraceSingleByObject(
	const FVector& Start,
	const FVector& End,
	bool bShowDebugShape,
	bool bDrawPersistantShapes
) const
{
	FHitResult OutHit;

	EDrawDebugTrace::Type DebugTraceType = EDrawDebugTrace::None;
	if (bShowDebugShape)
	{
		DebugTraceType = bDrawPersistantShapes ? EDrawDebugTrace::Persistent : EDrawDebugTrace::ForDuration;
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

	return OutHit;
}


FHitResult UDefaultMovementComponent::TraceFromEyeHeight(
	float TraceDistance,
	float TraceStartHeightOffset,
	bool bShowDebugShape,
	bool bDrawPersistantShapes
) const
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


bool UDefaultMovementComponent::TraceAndCacheClimbableSurfaces()
{
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * Climb_ForwardTraceStartOffset;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * Climb_ComponentForwardTraceDistance;

	Climb_ClimableSurfaceMultiTracedResults = DoCapsuleTraceMultiByObject(Start, End);
	const bool bFoundSurfaces = !Climb_ClimableSurfaceMultiTracedResults.IsEmpty();
	
	if (!bFoundSurfaces)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] TraceClimbableSurfaces - No surfaces found (check ClimbableSurfaceTraceTypes configuration)"));
	}
	
	return bFoundSurfaces;
}


bool UDefaultMovementComponent::TraceLedgeTopSurface(FHitResult& OutTopSurfaceHit, FVector& OutForwardProbeEnd, bool bDrawDebug)
{
	const FHitResult ForwardClearHit = TraceFromEyeHeight(Climb_EyeForwardTraceDistance, LedgeClimb_EyeHeightOffset, bDrawDebug);

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


/* ----- Core ----- */


void UDefaultMovementComponent::RpcServer_ToggleClimbing_Implementation(bool bEnableClimb)
{
	if (bClimb_DebugLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] >> RpcServer_ToggleClimbing(enable=%d) received on server | Mode=%d Custom=%d"),
			bEnableClimb ? 1 : 0, (int32)MovementMode, (int32)CustomMovementMode);
	}
	
	Auth_ToggleClimbing(bEnableClimb);
}


void UDefaultMovementComponent::Auth_ToggleClimbing(bool bEnableClimb)
{
	if (bEnableClimb)
	{
		// Reject Toggle that arrives right after a manual exit climb.
		// NOTE: This was used to patch double entry. Later cause was found to be BP & Cpp double input.
		//			Keep as this is a decent safeguard. 
		const double Now = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
		if (Climb_ReEntryLockoutSeconds > 0.f
			&& Climb_LastManualStopTime >= 0.0
			&& (Now - Climb_LastManualStopTime) < Climb_ReEntryLockoutSeconds)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ClimbingMovement] Auth_ToggleClimbing(true) REJECTED - within re-entry lockout (%.3fs since manual stop). This is the spurious restart being blocked."),
				Now - Climb_LastManualStopTime);
			return;
		}

		if (CanStartClimbing())
		{
			StartClimbing();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing from server - conditions not met"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ToggleClimbing(false) - stopping climb"));

		// Refresh safeguard
		Climb_LastManualStopTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

		StopClimbing();
	}
}


bool UDefaultMovementComponent::CanStartClimbing()
{

	if (IsFalling() && bClimb_DebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] CanStartClimbing - starting from AIR (falling). Entry slide will engage. Velocity=%s (|v|=%.1f)"),
			*Velocity.ToString(), Velocity.Size());
	}
	
	if (!TraceAndCacheClimbableSurfaces())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] Cannot start climbing - no climbable surfaces detected in forward trace"));
		return false;
	}
	
	FHitResult EyeHeightHit = TraceFromEyeHeight(Climb_EyeForwardTraceDistance);
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
	TraceAndCacheClimbableSurfaces();
	AveragesClimableSurfaceInfo();

	// Check should stop climbing
	if (CheckShouldStopClimbing() || CheckHasReachedFloor())
	{
		StopClimbing();
		return;
	}


	
	// Desired Velocity handling
	if (bClimb_IsEntrySliding)
	{
		// Issue: when input is held, the stock CalcVelocity() clamps Velocity down to Climb_MaxSpeed within a tick;
		// So a fast entry would instantly become capped if the player was holding a direction.
		// To guarantee the slide is driven by the ENTRY velocity,
		// uses separate deceleration and ignores move inputs, until @Velocity bled down to normal climb speed.
		
		const float CurrentSpeed = Velocity.Size();
		const FVector SlideDir = Velocity.GetSafeNormal();

		const float NewSpeed = FMath::Max(0.f, CurrentSpeed - Climb_EntrySlideDeceleration * deltaTime);
		Velocity = SlideDir * NewSpeed;

		// Re-project onto the (possibly updated) surface plane;
		// Surface normal changes while sliding across a curved/segmented wall, into-wall velocity opted out entirely.
		if (!Climb_CurrentSurfaceNormal.IsNearlyZero())
		{
			Velocity = FVector::VectorPlaneProject(Velocity, Climb_CurrentSurfaceNormal);
		}

		if (bClimb_DebugLog)
		{
			UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] EntrySlide | speed %.1f -> %.1f (decel %.0f) Vel=%s"),
				CurrentSpeed, Velocity.Size(), Climb_EntrySlideDeceleration, *Velocity.ToString());
		}

		if (bClimb_DebugDraw && GetWorld() && UpdatedComponent)
		{
			const FVector Loc = UpdatedComponent->GetComponentLocation();
			DrawDebugDirectionalArrow(GetWorld(), Loc, Loc + Velocity, 25.f, FColor::Orange, false, -1.f, 0, 2.f);
		}

		// Slide finished; flag back to the normal Desired Velocity handling next tick.
		if (Velocity.Size() <= Climb_MaxSpeed * SlideEntryOverspeedMultiplier)
		{
			StopMovementImmediately();
			bClimb_IsEntrySliding = false;
			if (bClimb_DebugLog)
			{
				UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] EntrySlide COMPLETE -> normal climb control resumed."));
			}
		}
	}
	else
	{
		// unreal standard calling; Update Desired @Velocity based on input
		CalcVelocity(deltaTime, 0.f, true, Climb_MaxBreakDeceleration);
	}

	
	// Resolve movement using current Velocity
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

	// Update Final Velocity after manual-triggering Resolving
	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;

	// Snap movement to climbable surface
	Climb_SnapMovementToSurfaces(deltaTime);


	// Heads to MOVE_ClimbLedge
	if (CheckReachingLedge())
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


void UDefaultMovementComponent::AveragesClimableSurfaceInfo()
{
	Climb_CurrentSurfaceLocation = FVector::ZeroVector;
	Climb_CurrentSurfaceNormal = FVector::ZeroVector;

	if (Climb_ClimableSurfaceMultiTracedResults.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ProcessClimableSurfaceInfo - No traced results available"));
		return;
	}

	// int count = 0;
	// Average out all the traced surfaces to get a more stable, representative location and normal
	for (const FHitResult& TracedHitResult : Climb_ClimableSurfaceMultiTracedResults)
	{
		// count++;
		// UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ProcessClimableSurfaceInfo - iteration: %i"), count);
		// UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] ProcessClimableSurfaceInfo - Traced: %s"), *TracedHitResult.ToString());

		Climb_CurrentSurfaceLocation += TracedHitResult.ImpactPoint; 
		Climb_CurrentSurfaceNormal += TracedHitResult.ImpactNormal;
	}

	Climb_CurrentSurfaceLocation /= Climb_ClimableSurfaceMultiTracedResults.Num();
	Climb_CurrentSurfaceNormal = Climb_CurrentSurfaceNormal.GetSafeNormal();
	
	 UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] AveragesClimableSurfaceInfo() - Surface Location: %s, Normal: %s"),
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
	return DegreeDiff <= Climb_StopClimbAngleFromUpDeg;	
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
	if (!UpdatedComponent || Climb_CurrentSurfaceNormal.IsNearlyZero())
	{
		return;
	}
	
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector ComponentWrldLoc = UpdatedComponent->GetComponentLocation();
	
	const FVector RelativeDistance = Climb_CurrentSurfaceLocation - ComponentWrldLoc;

	// Estimate how far the surface point is along player forward (not the true perpendicular distance).
	const float ForwardDistance = FVector::DotProduct(RelativeDistance, ComponentForward);
	if (ForwardDistance <= 0.f)
	{
		return;
	}

	const FVector DesiredSnap = -Climb_CurrentSurfaceNormal * ForwardDistance;

	// Entry ramp
	float EntryAlpha = 1.f;
	if (Climb_EntryWallSnapBlendTime > 0.f)
	{
		if (Climb_TimeSinceEntered < Climb_EntryWallSnapBlendTime)
		{
			EntryAlpha = Climb_TimeSinceEntered / Climb_EntryWallSnapBlendTime;
		}
	}

	const float SnapSpeed = FMath::Lerp(Climb_EntryWallSnapSpeed, Climb_WallSnapSpeed, EntryAlpha);
	const FVector TickSnapAmount = DesiredSnap.GetClampedToMaxSize(SnapSpeed * DeltaTime);

	FHitResult SnapHit(1.f);
	SafeMoveUpdatedComponent(TickSnapAmount, UpdatedComponent->GetComponentQuat(), true, SnapHit);

}


/* ----- Climb Ledge ----- */


bool UDefaultMovementComponent::CheckReachingLedge()
{
	FHitResult TopSurfaceHit;
	FVector ForwardProbeEnd;

	if (TraceLedgeTopSurface(TopSurfaceHit, ForwardProbeEnd, false) && GetLocalSpaceVelocity().Z > 10.f)
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

	// check again to cache data
	// ANCHOR: might want to remove? State change from PhysClimb() already called CheckHasReachedLedge(),
	// which called TraceLedgeTopSurface().
	if (!TraceLedgeTopSurface(TopSurfaceHit, ForwardProbeEnd, bClimb_DebugDraw))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] CalcLedgeClimbTarget - no top surface found below the lip."));
		return false;
	}

	// Reject surfaces that are too steep to stand on (e.g. a slanted underside).
	// ANCHOR: Maybe allow it later. If player slides, let it be; player cant tell if walkable or not until they tried
	const float SurfaceDotUp = FVector::DotProduct(TopSurfaceHit.ImpactNormal, FVector::UpVector);
	if (SurfaceDotUp < GetWalkableFloorZ()) // CMC setting
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] CalcLedgeClimbTarget - top surface not walkable (dot %.3f < floorZ %.3f)."),
			SurfaceDotUp, GetWalkableFloorZ());
		return false;
	}

	// Build the final capsule rest location
	// - Use the default half-height (not the shrunk climb height) to make sure there is enough room
	// - Push forward onto the surface so we don't land balancing on the edge.
	// ANCHOR: make player stand near-edge to allow player to vault over a wall
	const float FullHalfHeight = (DefaultCapsuleHalfHeight > 0.f) ? DefaultCapsuleHalfHeight : FallBackCapsuleHalfHeight;

	// Horizontal Forward
	FVector HorizForward = -Climb_CurrentSurfaceNormal;
	HorizForward.Z = 0.f;
	
	// Fallback
	if (!HorizForward.Normalize())
	{
		HorizForward = UpdatedComponent->GetForwardVector();
		HorizForward.Z = 0.f;
		HorizForward.Normalize();
	}

	// Use the capsule's horizontal (XY) position but take Z from the top surface hit
	const FVector CapsuleLoc = UpdatedComponent->GetComponentLocation();
	const FVector Up = UpdatedComponent->GetUpVector();

	// Add extra forward push equal to 2 * capsule radius (in addition to configured offset)
	float CapsuleRadius = 0.f;
	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		CapsuleRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	}
	const float ForwardPush = LedgeClimb_ForwardLandOffset + 2.f * CapsuleRadius;

	OutLandLocation = FVector(CapsuleLoc.X, CapsuleLoc.Y, TopSurfaceHit.ImpactPoint.Z)
		+ Up * (FullHalfHeight + 2.f)      // +2 padding to avoid embedded into ground
		+ HorizForward * ForwardPush;

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

	// Build the L-shaped path
	// Start: where we currently are on the wall.
	// OverLedge: straight up from Start, to just above the target surface (+clearance) so the lip is cleared.
	// Target: the final rest spot on top.
	LedgeClimb_StartLocation = UpdatedComponent->GetComponentLocation();
	LedgeClimb_TargetLocation = LandLocation;
	
	const FTransform PlayerTM = UpdatedComponent->GetComponentTransform();

	const FVector StartLocal = PlayerTM.InverseTransformPosition(LedgeClimb_StartLocation);
	const FVector LandLocal  = PlayerTM.InverseTransformPosition(LedgeClimb_TargetLocation);

	// Keep Start local X/Y, but raise to (Land local Z + clearance) in local up axis.
	const FVector OverLedgeLocal(
		StartLocal.X,
		StartLocal.Y,
		LandLocal.Z + LedgeClimb_VerticalClearance);

	LedgeClimb_OverLedgeLocation = PlayerTM.TransformPosition(OverLedgeLocal);

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

	// --- Debug: draw planned path ---
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

	// normalization to duration
	LedgeClimb_Alpha += deltaTime / FMath::Max(LedgeClimb_Duration, KINDA_SMALL_NUMBER);
	const float ClampedAlpha = FMath::Clamp(LedgeClimb_Alpha, 0.f, 1.f);

	// Lerp progression
	// Segment 1 (0 .. PhaseSplit):   Start      -> OverLedge   (rise, over the edge tip)
	// Segment 2 (PhaseSplit .. 1):   OverLedge  -> Target      (move over and settle)
	// Interp rotation toward the upright "on top" facing in the second segment to avoid collision to the lip.
	FQuat NewQuat;
	FVector DesiredLocation;
	if (ClampedAlpha <= LedgeClimb_PhaseSplit)
	{
		const float SegAlpha = FMath::InterpEaseOut(0.f, 1.f, ClampedAlpha / LedgeClimb_PhaseSplit, 1.2f);
		DesiredLocation = FMath::Lerp(LedgeClimb_StartLocation, LedgeClimb_OverLedgeLocation, SegAlpha);

		NewQuat = UpdatedComponent->GetComponentQuat();
	}
	else
	{
		const float SegAlpha = FMath::InterpEaseIn(0.f, 1.f, (ClampedAlpha - LedgeClimb_PhaseSplit) / (1.f - LedgeClimb_PhaseSplit), 1.2f);
		DesiredLocation = FMath::Lerp(LedgeClimb_OverLedgeLocation, LedgeClimb_TargetLocation, SegAlpha);

		NewQuat = FMath::QInterpTo(UpdatedComponent->GetComponentQuat(), LedgeClimb_TargetRotation, deltaTime, 5.f);

	}

	// Delta from the capsule's ACTUAL location instead of ideal math, so if a previous tick got
	// blocked, tries to catch up next tick instead of drifting.
	const FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	const FVector MoveDelta = DesiredLocation - CurrentLocation;


	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(MoveDelta, NewQuat, true, Hit);

	// If math sends the capsule into geometry, it stops to avoid tunneling
	if (Hit.IsValidBlockingHit())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] PhysLedgeClimb BLOCKED at alpha %.2f by %s (normal %s)"),
			ClampedAlpha, *GetNameSafe(Hit.GetActor()), *Hit.Normal.ToString());

		if (bClimb_DebugDraw && GetWorld())
		{
			DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 14.f, FColor::Red, false, 2.f);
		}
	}

	// Update Velocity after manual-triggering move
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


/* ----- Climb Exit ----- */


void UDefaultMovementComponent::ExitUprightBlend()
{
	if (!UpdatedComponent)
	{
		return;
	}

	const FRotator Dirty = UpdatedComponent->GetComponentRotation();
	Climb_ExitUprightTargetQuat = FRotator(0.f, Dirty.Yaw, 0.f).Quaternion();

	if (Climb_ExitUprightInterpSpeed <= 0.f)
	{
		UpdatedComponent->SetWorldRotation(Climb_ExitUprightTargetQuat);
		bClimb_ExitUprightBlendActive = false;
		bOrientRotationToMovement = true;
		return;
	}

	bClimb_ExitUprightBlendActive = true;
	bOrientRotationToMovement = false;
}


void UDefaultMovementComponent::TickExitUprightBlend(float DeltaTime)
{
	if (!bClimb_ExitUprightBlendActive || !UpdatedComponent)
	{
		return;
	}

	const FQuat Current = UpdatedComponent->GetComponentQuat();
	const FQuat Next = FMath::QInterpTo(Current, Climb_ExitUprightTargetQuat, DeltaTime, Climb_ExitUprightInterpSpeed);
	UpdatedComponent->SetWorldRotation(Next);

	if (Next.Equals(Climb_ExitUprightTargetQuat, 0.0025f))
	{
		UpdatedComponent->SetWorldRotation(Climb_ExitUprightTargetQuat);
		bClimb_ExitUprightBlendActive = false;
		bOrientRotationToMovement = true;
	}
}