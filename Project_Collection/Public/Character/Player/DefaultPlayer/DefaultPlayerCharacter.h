// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "InputActionValue.h"

#include "DefaultPlayerCharacter.generated.h"

/* ==================== Declares ==================== */

class UClimbingMovementComponent;
class UInputMappingContext;
class UInputAction;


/**
 * 
 */
UCLASS()
class PROJECT_COLLECTION_API ADefaultPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADefaultPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	/* ==================== Overrides ==================== */
	
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	
protected:
	/* ==================== Components ==================== */

	// Cached typed movement component (custom climb movement).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UClimbingMovementComponent> ClimbingMovementComponent;

	
	/* ==================== Input Assets ==================== */

	// Assign in BP (that thing is an asset).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="InputAction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="InputAction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="InputAction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="InputAction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="InputAction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UInputAction> ClimbAction;

	
public:
	/* ==================== APIs ==================== */

	UFUNCTION(BlueprintCallable, Category="Movement|Climbing")
	void Request_ToggleClimb();

	
	/* ==================== Queries ==================== */
	
	UFUNCTION(BlueprintPure, Category="Movement|Climbing")
	UClimbingMovementComponent* GetClimbingMovementComponent() const { return ClimbingMovementComponent; }

	
private:
	/* ==================== Internal Functions ==================== */

	void AddInputMappingContext(UInputMappingContext* ContextToAdd, int32 InPriority = 0);

	/* ----- Input Callbacks ----- */
	
	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void OnClimbActionStarted(const FInputActionValue& Value);
};
