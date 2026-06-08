// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VitalityUIAdaptorLib.generated.h"

// ==================== Declares ==================== 

class UVitalitySystem;


USTRUCT(BlueprintType)
struct PROJECT_COLLECTION_API FVitalityEffectGroupView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	FName EffectId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	int32 StackCount = 0;

	// Sum of CurrentContribution of all active instances with same EffectId.
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float TotalContribution = 0.f;
};


USTRUCT(BlueprintType)
struct PROJECT_COLLECTION_API FVitalityBarView
{
	GENERATED_BODY()

	// Raw readouts (int-facing, same spirit as your system)
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	int32 BaseCap = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	int32 MeterCap = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	int32 Available = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	bool bFainted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	bool bConcludedDeath = false;

	// Aggregates from active effects
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float PositiveUnits = 0.f; // left extension

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float NegativeUnits = 0.f; // right occupied

	// Final visual space = Base + Positive
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float TotalVisualUnits = 1.f;

	// Normalized fractions over TotalVisualUnits
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float PositiveFrac = 0.f; // [0,1]

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float NegativeFrac = 0.f; // [0,1]

	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float FillFrac = 0.f; // [0,1], Available / TotalVisualUnits

	// Cap boundary inside total visual width (clamped to [0,1] for drawing)
	UPROPERTY(BlueprintReadOnly, Category = "Vitality|UI")
	float CapFrac = 0.f;
};


/**
 * 
 */
UCLASS()
class PROJECT_COLLECTION_API UVitalityUIAdaptorLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Build a UI-friendly snapshot from UVitalitySystem.
	UFUNCTION(BlueprintPure, Category = "Vitality|UI")
	static void BuildVitalityBarView(
		const UVitalitySystem* System,
		FVitalityBarView& OutBarView,
		TArray<FVitalityEffectGroupView>& OutGroups);
};
