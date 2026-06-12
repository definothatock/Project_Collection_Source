// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Player/DefaultPlayer/DefaultPlayerCharacter.h"
#include "Character/Player/Systems/Climbing/DefaultMovementComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Controller.h"
#include "InputAction.h"
#include "InputMappingContext.h"



// 
ADefaultPlayerCharacter::ADefaultPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDefaultMovementComponent>(
															ACharacter::CharacterMovementComponentName))
{
	// Keep character rotation behavior movement-driven
	// ANCHOR: First person, should make this into options for different movement modes
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	
	DefaultMovementComponent = Cast<UDefaultMovementComponent>(GetCharacterMovement());

	// Base walking setup
	GetCharacterMovement()->bOrientRotationToMovement = true; // cant rotate when not using ControllerRot
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
}


/* ==================== Overrides ==================== */


void ADefaultPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	AddInputMappingContext(DefaultMappingContext, 0);
}


void ADefaultPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent) return;

	// Jump
	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	// Move / Look
	if (MoveAction)
	{
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADefaultPlayerCharacter::HandleMoveInput);
	}
	if (LookAction)
	{
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADefaultPlayerCharacter::HandleLookInput);
	}

	// Climb toggle
	if (ClimbAction)
	{
		EnhancedInputComponent->BindAction(ClimbAction, ETriggerEvent::Triggered, this, &ADefaultPlayerCharacter::OnClimbActionTriggered);
	}
}


/* ==================== APIs ==================== */


void ADefaultPlayerCharacter::Request_CustomMovement_Climb()
{
	if (!DefaultMovementComponent)
	{return;}

	const bool bWantsClimb = !DefaultMovementComponent->IsClimbing();
	
	UE_LOG(LogTemp, Display, TEXT("Request_CustomMovement_Climb Called | IsClimbing=%d -> bWantsClimb=%d"),
		DefaultMovementComponent->IsClimbing() ? 1 : 0, bWantsClimb ? 1 : 0);
	
	DefaultMovementComponent->Request_ToggleClimbing(bWantsClimb);
}


/* ==================== Internal Functions ==================== */


void ADefaultPlayerCharacter::AddInputMappingContext(UInputMappingContext* ContextToAdd, int32 InPriority)
{
	if (!ContextToAdd) return;

	if (APlayerController* PlyC = Cast<APlayerController>(Controller))
	{
		if (ULocalPlayer* LPlayer = PlyC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LPlayer))
			{
				Subsystem->AddMappingContext(ContextToAdd, InPriority);
			}
		}
	}
}


void ADefaultPlayerCharacter::HandleMoveInput(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (!Controller) return;

	// If climbing: remap input to wall-relative axes.
	if (DefaultMovementComponent && DefaultMovementComponent->IsClimbing())
	{
		const FVector SurfaceNormal = DefaultMovementComponent->GetClimbableSurfaceNormal();

		const FVector ForwardDirection = FVector::CrossProduct(-SurfaceNormal, GetActorRightVector());
		const FVector RightDirection   = FVector::CrossProduct(-SurfaceNormal, -GetActorUpVector());

		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
		return;
	}

	// Ground/air movement: controller yaw-relative.
	const FRotator ControlRot = Controller->GetControlRotation();
	const FRotator YawRot(0.f, ControlRot.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector RightDirection   = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}


void ADefaultPlayerCharacter::HandleLookInput(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	if (!Controller) {return;}

	AddControllerYawInput(LookAxis.X);
	AddControllerPitchInput(LookAxis.Y);
}


void ADefaultPlayerCharacter::OnClimbActionTriggered(const FInputActionValue& Value)
{
	Request_CustomMovement_Climb();
}

