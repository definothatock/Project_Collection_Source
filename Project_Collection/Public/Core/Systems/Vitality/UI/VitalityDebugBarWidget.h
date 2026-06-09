// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "Core/Systems/Vitality/UI/VitalityUIAdaptorLib.h"

#include "VitalityDebugBarWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_COLLECTION_API UVitalityDebugBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Vitality|UI")
	void SetObservedVitality(UVitalityComponent* InSystem);

	UFUNCTION(BlueprintPure, Category = "Vitality|UI")
	const FVitalityBarView& GetBarView() const { return CachedBarView; }

	UFUNCTION(BlueprintPure, Category = "Vitality|UI")
	const TArray<FVitalityEffectGroup>& GetGroupedEffects() const { return CachedGroups; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

public:
	// Optional auto-resize: positive effects increase displayed bar width.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	bool bAutoResizeWidth = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	float BaseWidthPx = 320.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	float BarHeightPx = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	float BorderPx = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	FLinearColor ColorBackground = FLinearColor(0.05f, 0.05f, 0.05f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	FLinearColor ColorPositive = FLinearColor(0.18f, 0.55f, 1.f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	FLinearColor ColorFill = FLinearColor(0.2f, 0.9f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	FLinearColor ColorNegative = FLinearColor(1.f, 0.65f, 0.1f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vitality|UI")
	FLinearColor ColorBorder = FLinearColor::White;

private:
	void RefreshCachedView();
	
	UPROPERTY(Transient)
	TObjectPtr<UVitalityComponent> ObservedSystem = nullptr;

	UPROPERTY(Transient)
	FVitalityBarView CachedBarView;

	UPROPERTY(Transient)
	TArray<FVitalityEffectGroup> CachedGroups;
};
