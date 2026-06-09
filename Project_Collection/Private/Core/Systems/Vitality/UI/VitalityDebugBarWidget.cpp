// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Systems/Vitality/UI/VitalityDebugBarWidget.h"

#include "Styling/AppStyle.h"
#include "Rendering/DrawElements.h"


void UVitalityDebugBarWidget::SetObservedVitality(UVitalityComponent* InSystem)
{
	ObservedSystem = InSystem;
	RefreshCachedView();
}

void UVitalityDebugBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Do not block gameplay input even if viewport slot is large/fullscreen.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	RefreshCachedView();
}

void UVitalityDebugBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshCachedView();
}

int32 UVitalityDebugBarWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{

	const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
	if (!WhiteBrush || !ObservedSystem)
	{
		return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	// Draw only a compact bar region, never fill full allotted area.
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float DrawW = FMath::Clamp(
		(bAutoResizeWidth && CachedBarView.BaseCap > 0)
			? BaseWidthPx * FMath::Max(0.25f, CachedBarView.TotalVisualUnits / FMath::Max(1.f, static_cast<float>(CachedBarView.BaseCap)))
			: BaseWidthPx,
		1.f, LocalSize.X);

	const float DrawH = FMath::Clamp(BarHeightPx + BorderPx * 2.f, 1.f, LocalSize.Y);

	const auto MakeGeom = [&AllottedGeometry](float X, float Y, float W, float H)
	{
		// UE5+ signature (non-deprecated)
		return AllottedGeometry.ToPaintGeometry(
			FVector2f(W, H),
			FSlateLayoutTransform(FVector2f(X, Y)));
	};

	// Outer border
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId++,
		MakeGeom(0.f, 0.f, DrawW, DrawH),
		WhiteBrush, ESlateDrawEffect::None, ColorBorder);

	// Inner rect
	const float X0 = BorderPx;
	const float Y0 = BorderPx;
	const float IW = FMath::Max(1.f, DrawW - BorderPx * 2.f);
	const float IH = FMath::Max(1.f, DrawH - BorderPx * 2.f);

	// Background (only inside bar area)
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId++,
		MakeGeom(X0, Y0, IW, IH),
		WhiteBrush, ESlateDrawEffect::None, ColorBackground);

	// Positive segment (left extension zone)
	const float PosW = IW * FMath::Clamp(CachedBarView.PositiveFrac, 0.f, 1.f);
	if (PosW > 0.f)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++,
			MakeGeom(X0, Y0, PosW, IH),
			WhiteBrush, ESlateDrawEffect::None, ColorPositive);
	}

	// Fill (continuous)
	const float FillW = IW * FMath::Clamp(CachedBarView.FillFrac, 0.f, 1.f);
	if (FillW > 0.f)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++,
			MakeGeom(X0, Y0, FillW, IH),
			WhiteBrush, ESlateDrawEffect::None, ColorFill);
	}

	// Negative occupied segment (right aligned)
	const float NegW = IW * FMath::Clamp(CachedBarView.NegativeFrac, 0.f, 1.f);
	if (NegW > 0.f)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++,
			MakeGeom(X0 + IW - NegW, Y0, NegW, IH),
			WhiteBrush, ESlateDrawEffect::None, ColorNegative);
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void UVitalityDebugBarWidget::RefreshCachedView()
{
	UVitalityUIAdaptorLib::BuildVitalityBarView(ObservedSystem, CachedBarView, CachedGroups);

	const float DesiredW =
		(bAutoResizeWidth && CachedBarView.BaseCap > 0)
		? BaseWidthPx * FMath::Max(0.25f, CachedBarView.TotalVisualUnits / FMath::Max(1.f, static_cast<float>(CachedBarView.BaseCap)))
		: BaseWidthPx;

	const float DesiredH = BarHeightPx + BorderPx * 2.f;
	SetDesiredSizeInViewport(FVector2D(DesiredW, DesiredH));

	InvalidateLayoutAndVolatility();
}
