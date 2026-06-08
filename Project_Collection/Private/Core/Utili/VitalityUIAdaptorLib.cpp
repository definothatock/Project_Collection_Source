// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Utili/VitalityUIAdaptorLib.h"
#include "Core/Utili/VitalitySystem.h"


void UVitalityUIAdaptorLib::BuildVitalityBarView(
	const UVitalitySystem* System,
	FVitalityBarView& OutBarView,
	TArray<FVitalityEffectGroupView>& OutGroups)
{
	OutBarView = FVitalityBarView{};
	OutGroups.Reset();

	if (!System)
	{
		return;
	}

	OutBarView.BaseCap = System->GetBaseCap();
	OutBarView.MeterCap = System->GetMeterCap();
	OutBarView.Available = System->GetAvailableStamina();
	OutBarView.bFainted = System->IsFainted();
	OutBarView.bConcludedDeath = System->HasConcludedDeath();

	float PositiveSum = 0.f;
	float NegativeSumAbs = 0.f;

	TMap<FName, FVitalityEffectGroupView> GroupMap;

	for (const FVitalityEffect& E : System->GetActiveEffects())
	{
		if (E.CurrentContribution > 0.f)
		{
			PositiveSum += E.CurrentContribution;
		}
		else if (E.CurrentContribution < 0.f)
		{
			NegativeSumAbs += -E.CurrentContribution;
		}

		FVitalityEffectGroupView& G = GroupMap.FindOrAdd(E.EffectId);
		G.EffectId = E.EffectId;
		G.StackCount += 1;
		G.TotalContribution += E.CurrentContribution;
	}

	GroupMap.GenerateValueArray(OutGroups);

	OutBarView.PositiveUnits = FMath::Max(0.f, PositiveSum);
	OutBarView.NegativeUnits = FMath::Max(0.f, NegativeSumAbs);

	const float BaseF = FMath::Max(1.f, static_cast<float>(OutBarView.BaseCap));
	OutBarView.TotalVisualUnits = FMath::Max(1.f, BaseF + OutBarView.PositiveUnits);

	OutBarView.PositiveFrac = FMath::Clamp(OutBarView.PositiveUnits / OutBarView.TotalVisualUnits, 0.f, 1.f);
	OutBarView.NegativeFrac = FMath::Clamp(OutBarView.NegativeUnits / OutBarView.TotalVisualUnits, 0.f, 1.f);

	const float AvailF = FMath::Max(0.f, static_cast<float>(OutBarView.Available));
	OutBarView.FillFrac = FMath::Clamp(AvailF / OutBarView.TotalVisualUnits, 0.f, 1.f);

	const float CapClamped = FMath::Clamp(static_cast<float>(OutBarView.MeterCap), 0.f, OutBarView.TotalVisualUnits);
	OutBarView.CapFrac = FMath::Clamp(CapClamped / OutBarView.TotalVisualUnits, 0.f, 1.f);
}