// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Player/Systems/Climbing/ClimbingMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"



UClimbingMovementComponent::UClimbingMovementComponent()
{
}

/* ==================== Overridden Functions ==================== */


void UClimbingMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
	{
		DefaultCapsuleHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	}
}


void UClimbingMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UClimbingMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// setup climb mode
	if (IsClimbing())
	{
		bOrientRotationToMovement = false;

		if (CharacterOwner && CharacterOwner->GetCapsuleComponent())
		{
			CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(ClimbingCapsuleHalfHeight);
		}

		OnEnterClimbStateDelegate.ExecuteIfBound();
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

		OnExitClimbStateDelegate.ExecuteIfBound();
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}


void UClimbingMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (IsClimbing())
	{
		PhysClimb(deltaTime, Iterations);
		return;
	}

	Super::PhysCustom(deltaTime, Iterations);
}


float UClimbingMovementComponent::GetMaxSpeed() const
{
	if (IsClimbing())
	{
		return MaxClimbSpeed;
	}

	return Super::GetMaxSpeed();
}


float UClimbingMovementComponent::GetMaxAcceleration() const
{
	if (IsClimbing())
	{
		return MaxClimbAcceleration;
	}

	return Super::GetMaxAcceleration();
}


/* ==================== APIs ==================== */


void UClimbingMovementComponent::ToggleClimbing(bool bEnableClimb)
{
	if (bEnableClimb)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] ToggleClimbing(true) called. Checking if can start climbing..."));
		if (CanStartClimbing())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Climbing] Can start climbing - initiating climb mode"));
			StartClimbing();
			StopMovementImmediately();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Climbing] Cannot start climbing - conditions not met"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] ToggleClimbing(false) - stopping climb"));
		StopClimbing();
	}
}


bool UClimbingMovementComponent::IsClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_Climb;
}


/* ==================== Queries ==================== */


FVector UClimbingMovementComponent::GetUnrotatedClimbVelocity() const
{
	if (!UpdatedComponent)
	{
		return Velocity;
	}

	return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(), Velocity);
}


/* ==================== Internal Function ==================== */

/* ----- Traces ----- */

TArray<FHitResult> UClimbingMovementComponent::DoCapsuleTraceMultiByObject(
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
		UE_LOG(LogTemp, Error, TEXT("[Climbing] DoCapsuleTraceMultiByObject - ClimbableSurfaceTraceTypes is EMPTY! This causes 'Invalid object types' warning. Please set object types in component settings."));
	}
	
	UE_LOG(LogTemp, Log, TEXT("[Climbing] DoCapsuleTraceMultiByObject - Start: %s, End: %s, Radius: %.2f, HalfHeight: %.2f, ObjectTypes: %d"),
		*Start.ToString(), *End.ToString(), ClimbCapsuleTraceRadius, ClimbCapsuleTraceHalfHeight, ClimbableSurfaceTraceTypes.Num());
	
	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		Start,
		End,
		ClimbCapsuleTraceRadius,
		ClimbCapsuleTraceHalfHeight,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutCapsuleTraceHitResults,
		false
	);
	
	UE_LOG(LogTemp, Log, TEXT("[Climbing] DoCapsuleTraceMultiByObject - Found %d hits"), OutCapsuleTraceHitResults.Num());

	return OutCapsuleTraceHitResults;
}

FHitResult UClimbingMovementComponent::DoLineTraceSingleByObject(
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
		UE_LOG(LogTemp, Error, TEXT("[Climbing] DoLineTraceSingleByObject - ClimbableSurfaceTraceTypes is EMPTY! This causes 'Invalid object types' warning. Please set object types in component settings."));
	}
	
	UE_LOG(LogTemp, Log, TEXT("[Climbing] DoLineTraceSingleByObject - Start: %s, End: %s, ObjectTypes: %d"),
		*Start.ToString(), *End.ToString(), ClimbableSurfaceTraceTypes.Num());
	
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
		UE_LOG(LogTemp, Log, TEXT("[Climbing] DoLineTraceSingleByObject - Hit actor: %s at location: %s"),
			*GetNameSafe(OutHit.GetActor()), *OutHit.ImpactPoint.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[Climbing] DoLineTraceSingleByObject - No hit detected"));
	}

	return OutHit;
}


bool UClimbingMovementComponent::TraceClimbableSurfaces()
{
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * ForwardTraceStartOffset;
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * ForwardTraceDistance;

	ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start, End);
	const bool bFoundSurfaces = !ClimbableSurfacesTracedResults.IsEmpty();
	
	if (bFoundSurfaces)
	{
		UE_LOG(LogTemp, Log, TEXT("[Climbing] TraceClimbableSurfaces - Found %d surfaces"), ClimbableSurfacesTracedResults.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] TraceClimbableSurfaces - No surfaces found (check ClimbableSurfaceTraceTypes configuration)"));
	}
	
	return bFoundSurfaces;
}


FHitResult UClimbingMovementComponent::TraceFromEyeHeight(
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

bool UClimbingMovementComponent::CanStartClimbing()
{
	if (IsFalling())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] Cannot start climbing - character is falling"));
		return false;
	}
	
	if (!TraceClimbableSurfaces())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] Cannot start climbing - no climbable surfaces detected in forward trace"));
		return false;
	}
	
	FHitResult EyeHeightHit = TraceFromEyeHeight(EyeHeightTraceDistance);
	if (!EyeHeightHit.bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] Cannot start climbing - eye height trace did not hit climbable surface"));
		return false;
	}

	return true;
}


void UClimbingMovementComponent::StartClimbing()
{
	SetMovementMode(MOVE_Custom, ECustomMovementMode::MOVE_Climb);
}


void UClimbingMovementComponent::StopClimbing()
{
	// Simple prototype behavior: exit to falling.
	SetMovementMode(MOVE_Falling);
}


void UClimbingMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
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
	CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);

	// Handle climb rotation + movement
	SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;

	// Snap movement to climbable surface
	SnapMovementToClimableSurfaces(deltaTime);
}


void UClimbingMovementComponent::ProcessClimableSurfaceInfo()
{
	CurrentClimbableSurfaceLocation = FVector::ZeroVector;
	CurrentClimbableSurfaceNormal = FVector::ZeroVector;

	if (ClimbableSurfacesTracedResults.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Climbing] ProcessClimableSurfaceInfo - No traced results available"));
		return;
	}

	for (const FHitResult& TracedHitResult : ClimbableSurfacesTracedResults)
	{
		CurrentClimbableSurfaceLocation += TracedHitResult.ImpactPoint;
		CurrentClimbableSurfaceNormal += TracedHitResult.ImpactNormal;
	}

	CurrentClimbableSurfaceLocation /= ClimbableSurfacesTracedResults.Num();
	CurrentClimbableSurfaceNormal = CurrentClimbableSurfaceNormal.GetSafeNormal();
	
	UE_LOG(LogTemp, Log, TEXT("[Climbing] ProcessClimableSurfaceInfo - Surface Location: %s, Normal: %s"),
		*CurrentClimbableSurfaceLocation.ToString(), *CurrentClimbableSurfaceNormal.ToString());
}



bool UClimbingMovementComponent::CheckShouldStopClimbing()
{
	if (ClimbableSurfacesTracedResults.IsEmpty()) return true;

	const float DotResult = FMath::Clamp(
		FVector::DotProduct(CurrentClimbableSurfaceNormal, FVector::UpVector),
		-1.f, 1.f
	);

	const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));

	// Surface too walkable/flat => stop climb.
	return DegreeDiff <= 60.f;
}


bool UClimbingMovementComponent::CheckHasReachedFloor()
{
	const FVector DownVector = -UpdatedComponent->GetUpVector();
	const FVector StartOffset = DownVector * 50.f;

	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + DownVector;

	const TArray<FHitResult> PossibleFloorHits = DoCapsuleTraceMultiByObject(Start, End);
	if (PossibleFloorHits.IsEmpty()) return false;

	for (const FHitResult& PossibleFloorHit : PossibleFloorHits)
	{
		const bool bFloorReached =
			FVector::Parallel(-PossibleFloorHit.ImpactNormal, FVector::UpVector) &&
			GetUnrotatedClimbVelocity().Z < -10.f;

		if (bFloorReached)
		{
			return true;
		}
	}

	return false;
}


FQuat UClimbingMovementComponent::GetClimbRotation(float DeltaTime)
{
	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();
	const FQuat TargetQuat = FRotationMatrix::MakeFromX(-CurrentClimbableSurfaceNormal).ToQuat();

	return FMath::QInterpTo(CurrentQuat, TargetQuat, DeltaTime, 5.f);
}


void UClimbingMovementComponent::SnapMovementToClimableSurfaces(float DeltaTime)
{
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

	const FVector ProjectedCharacterToSurface =
		(CurrentClimbableSurfaceLocation - ComponentLocation).ProjectOnTo(ComponentForward);

	const FVector SnapVector = -CurrentClimbableSurfaceNormal * ProjectedCharacterToSurface.Length();
	const FVector FinalSnapAmount = SnapVector * DeltaTime * MaxClimbSpeed;
	
	UE_LOG(LogTemp, Log, TEXT("[Climbing] SnapMovementToClimableSurfaces - SnapVector: %s, FinalSnapAmount: %s"),
		*SnapVector.ToString(), *FinalSnapAmount.ToString());

	UpdatedComponent->MoveComponent(
		FinalSnapAmount,
		UpdatedComponent->GetComponentQuat(),
		true
	);
}


