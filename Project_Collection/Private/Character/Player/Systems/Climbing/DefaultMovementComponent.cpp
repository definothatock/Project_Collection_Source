// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Player/Systems/Climbing/DefaultMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"


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

	// Clean up climb mode
	if (PreviousMovementMode == MOVE_Custom && PreviousCustomMode == ECustomMovementMode::MOVE_Climb)
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

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}


void UDefaultMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (IsClimbing())
	{
		PhysClimb(deltaTime, Iterations);
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


/* ==================== APIs ==================== */


void UDefaultMovementComponent::Request_ToggleClimbing(bool bEnableClimb)
{
	if (!CanStartClimbing())
	{return;}
	
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{
		RpcServer_ToggleClimbing(bEnableClimb);
	}
}


/* ==================== Queries ==================== */


bool UDefaultMovementComponent::IsClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
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
	
	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoCapsuleTraceMultiByObject - Found %d hits"), OutCapsuleTraceHitResults.Num());

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
	
	if (OutHit.bBlockingHit)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoLineTraceSingleByObject - Hit actor: %s at location: %s"),
			*GetNameSafe(OutHit.GetActor()), *OutHit.ImpactPoint.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] DoLineTraceSingleByObject - No hit detected"));
	}

	return OutHit;
}


bool UDefaultMovementComponent::TraceClimbableSurfaces()
{
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * Climb_ForwardTraceStartOffset;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * Climb_ForwardTraceDistance;

	Climb_ClimableSurfaceMultiTracedResults = DoCapsuleTraceMultiByObject(Start, End);
	const bool bFoundSurfaces = !Climb_ClimableSurfaceMultiTracedResults.IsEmpty();
	
	if (bFoundSurfaces)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] TraceClimbableSurfaces - Found %d surfaces"), Climb_ClimableSurfaceMultiTracedResults.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClimbingMovement] TraceClimbableSurfaces - No surfaces found (check ClimbableSurfaceTraceTypes configuration)"));
	}
	
	return bFoundSurfaces;
}


FHitResult UDefaultMovementComponent::TraceFromEyeHeight(
	float TraceDistance,
	float TraceStartOffset,
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
		UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);

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

	// Define climb velocity
	CalcVelocity(deltaTime, 0.f, true, Climb_MaxBreakDeceleration);

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);

	// Handle climb rotation + movement
	SafeMoveUpdatedComponent(Adjusted, Climb_CalculateSurfaceAlignedRot(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;

	// Snap movement to climbable surface
	Climb_SnapMovementToSurfaces(deltaTime);
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

	// Average out all the traced surfaces to get a more stable location and normal
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
	return DegreeDiff <= Climb_MaxSurfaceDegree;
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

bool UDefaultMovementComponent::CheckHasReachedLedge()
{
	// ANCHOR: might want to make some offset in the future; make a config var?
	FHitResult LedgetHitResult = TraceFromEyeHeight(Climb_EyeHeightTraceDistance);

	// If no surface detected at eye height, check if there's a walkable surface below to determine if we've reached a ledge.
	if(!LedgetHitResult.bBlockingHit)
	{
		const FVector WalkableSurfaceTraceStart = LedgetHitResult.TraceEnd;

		const FVector DownVector = -UpdatedComponent->GetUpVector();
		const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

		FHitResult WalkabkeSurfaceHitResult =
		DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

		// If we hit a walkable surface and we're moving downward, we can consider that we've reached a ledge.
		if(WalkabkeSurfaceHitResult.bBlockingHit && GetLocalSpaceVelocity().Z > 10.f)
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
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

	const FVector ProjectedCharacterToSurface =
		(Climb_CurrentSurfaceLocation - ComponentLocation).ProjectOnTo(ComponentForward);

	const FVector SnapVector = -Climb_CurrentSurfaceNormal * ProjectedCharacterToSurface.Length();
	const FVector FinalSnapAmount = SnapVector * DeltaTime * Climb_MaxSpeed;
	
	UE_LOG(LogTemp, Log, TEXT("[ClimbingMovement] SnapMovementToClimableSurfaces - SnapVector: %s, FinalSnapAmount: %s"),
		*SnapVector.ToString(), *FinalSnapAmount.ToString());

	UpdatedComponent->MoveComponent(
		FinalSnapAmount,
		UpdatedComponent->GetComponentQuat(),
		true
	);
}


