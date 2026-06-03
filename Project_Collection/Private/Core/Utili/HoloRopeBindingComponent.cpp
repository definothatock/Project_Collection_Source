// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Utili/HoloRopeBindingComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

// Sets default values for this component's properties
UHoloRopeBindingComponent::UHoloRopeBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ==================== APIs ====================

bool UHoloRopeBindingComponent::HoloRope_Bind(AActor* InTarget, FName InTargetSocket, FName InBinderSocket)
{
	if (!IsValid(InTarget)) {return false;}
	AActor* Binder = GetOwner();
	if (!IsValid(Binder)) {return false;}
	if (InTarget == Binder) {return false;}
	
	// Release any existing binding first so Bind() is idempotent.
	// Anchor: might want to change this to support many-to-one binding.
	if (IsBound()) {HoloRope_Unbind(EHoloRopeUnbindReason::Manual);}

	
	BoundTarget      = InTarget;
	TargetSocketName = InTargetSocket;
	BinderSocketName = InBinderSocket;

	UPrimitiveComponent* Body = ResolveTargetPhyBody();
	if (!Body)
	{
		// No simulating body to push; binding is meaningless.
		BoundTarget.Reset();
		TargetSocketName = NAME_None;
		BinderSocketName = NAME_None;
		UE_LOG(LogTemp, Warning,
			   TEXT("HoloRope: '%s' has no simulating primitive; cannot bind."),
			   *InTarget->GetName());
		return false;
	}

	BoundTargetPhyBody = Body;

	// Resolve socket-owning components once; cheap per-tick reads after this.
	CacheAnchorComponents();

	SetBoundTickEnabled(true);
	
	UE_LOG(LogTemp, Warning,
	   TEXT("HoloRope: '%s' is bound."), *InTarget->GetName());
	
	return true;
}


void UHoloRopeBindingComponent::HoloRope_Unbind(EHoloRopeUnbindReason Reason)
{
	if (!IsBound() && !BoundTarget.IsValid())
	{
		// Already clean; still allow broadcast suppression by early-out.
		SetBoundTickEnabled(false);
		return;
	}

	AActor* WasBound = BoundTarget.Get();

	BoundTarget.Reset();
	BoundTargetPhyBody.Reset();
	BinderAnchorComp.Reset();
	TargetAnchorComp.Reset();
	TargetSocketName = NAME_None;
	BinderSocketName = NAME_None;

	SetBoundTickEnabled(false);

	OnHoloRopeUnbound.Broadcast(WasBound, Reason);
}


// ==================== Queries ==================== 


bool UHoloRopeBindingComponent::IsBound() const
{
	return BoundTarget.IsValid() && BoundTargetPhyBody.IsValid();
}

AActor* UHoloRopeBindingComponent::GetBoundTarget() const
{
	return BoundTarget.Get();
}

float UHoloRopeBindingComponent::GetCurrentDistance() const
{
	if (!IsBound())
	{
		return -1.f;
	}
	return FVector::Dist(GetBinderAnchorLocation(), GetTargetAnchorLocation());
}


// ==================== Internal ==================== 


// Check and call ApplyPullForce()
void UHoloRopeBindingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Entry Condition
	if (!IsBound())
	{
		SetBoundTickEnabled(false);
		return;
	}
	
	// Failure case: binder gone.
	if (!IsValid(GetOwner()))
	{
		HoloRope_Unbind(EHoloRopeUnbindReason::BinderInvalid);
		return;
	}
	// Failure case: target or its body became invalid.
	if (!BoundTarget.IsValid() || !BoundTargetPhyBody.IsValid())
	{
		HoloRope_Unbind(EHoloRopeUnbindReason::TargetInvalid);
		return;
	}

	
	const FVector BinderLoc = GetBinderAnchorLocation();
	const FVector TargetLoc = GetTargetAnchorLocation();
	const float   Distance  = FVector::Dist(BinderLoc, TargetLoc);

	// Failure case: out of range.
	if (bAutoUnbindOnMaxDistance && Distance > MaxDistance)
	{
		HoloRope_Unbind(EHoloRopeUnbindReason::ExceededMaxDistance);
		return;
	}

	
	// ----- Debug Readout -----
	if (bDebugDraw)
	{
		if (UWorld* W = GetWorld())
		{
			const bool bOverMax  = Distance > MaxDistance;
			const bool bInSlack  = Distance <= SlackDistance;
			const FColor RopeCol = bOverMax ? FColor::Red : (bInSlack ? FColor::Green : FColor::Yellow);

			// Anchor points + rope segment
			DrawDebugSphere(W, BinderLoc, 6.f, 8, FColor::Cyan,  false, -1.f, 0, 0.6f);
			DrawDebugSphere(W, TargetLoc, 6.f, 8, FColor::Green, false, -1.f, 0, 0.6f);
			DrawDebugCylinder(W, BinderLoc, TargetLoc, 10.f, 12, RopeCol,  false, -1.f, 0, 0.6f);
			
			// Slack / Max range rings (as spheres centered on binder)
			if (SlackDistance > KINDA_SMALL_NUMBER)
			{
				DrawDebugSphere(W, BinderLoc, SlackDistance, 24, FColor(80, 180, 80), false, -1.f, 0, 0.6f);
			}
			if (MaxDistance > KINDA_SMALL_NUMBER)
			{
				DrawDebugSphere(W, BinderLoc, MaxDistance, 24, FColor(200, 80, 80), false, -1.f, 0, 0.6f);
			}
		}

		if (GEngine)
		{
			const int32 KeyBase = static_cast<int32>(GetUniqueID() & 0x7fffffff);
			GEngine->AddOnScreenDebugMessage(
				KeyBase + 10, 0.f, FColor::White,
				FString::Printf(TEXT("[HoloRope] Dist: %.1f | Slack: %.1f | Max: %.1f"),
					Distance, SlackDistance, MaxDistance));
		}
	}
	
	
	ApplyPullForce(BinderLoc, TargetLoc, Distance);
}


void UHoloRopeBindingComponent::ApplyPullForce(const FVector& BinderLoc, const FVector& TargetLoc, float Distance)
{
	UPrimitiveComponent* Body = BoundTargetPhyBody.Get();
	if (!Body) {return;}
	
	const FVector ToBinder = BinderLoc - TargetLoc;
	// Within slack, no force.
	if (Distance <= SlackDistance || Distance <= KINDA_SMALL_NUMBER)
	{return;}

	
	const FVector Direction = ToBinder / Distance;
	const float Range    = FMath::Max(MaxDistance - SlackDistance, KINDA_SMALL_NUMBER);
	const float Alpha    = FMath::Clamp((Distance - SlackDistance) / Range, 0.f, 1.f);
	const float Scale    = ForceCurve ? ForceCurve->GetFloatValue(Alpha) : Alpha;

	FVector Force = Direction * (MaxPullForce * Scale);

	/*  Cached at bind: if a target socket component exists, pull from the socket
	 *  otherwise push at COM as before. */
	if (TargetAnchorComp.IsValid())
	{
		// AddForceAtLocation has no acceleration-change option; convert manually
		// to preserve bForceIsAcceleration semantics.
		if (bForceIsAcceleration)
		{
			Force *= Body->GetMass();
		}
		Body->AddForceAtLocation(Force, TargetLoc);
	}
	else
	{
		Body->AddForce(Force, NAME_None, bForceIsAcceleration);
	}

	// UE_LOG(LogTemp, Warning, TEXT("HoloRope: Force applied. %s"), *Force.ToString());

	// ---- Debug readout (force) ----
	if (bDebugDraw)
	{
		if (UWorld* W = GetWorld())
		{
			// force vector at target anchor
			DrawDebugLine(W, TargetLoc, TargetLoc + Force * 0.0005f, FColor::Red, false, -1.f, 0, 1.2f);

			// torque arm (COM -> target anchor) if pulling at location
			if (TargetAnchorComp.IsValid())
			{
				const FVector COM = Body->GetComponentLocation();
				DrawDebugLine(W, COM, TargetLoc, FColor::Magenta, false, -1.f, 0, 1.0f);
			}
		}

		if (GEngine)
		{
			const int32 KeyBase = static_cast<int32>(GetUniqueID() & 0x7fffffff);

			GEngine->AddOnScreenDebugMessage(
				KeyBase + 11, 0.f, FColor::White,
				FString::Printf(TEXT("[HoloRope] Alpha: %.2f | Scale: %.2f | Force: %.0f"),
					Alpha, Scale, Force.Size()));

			GEngine->AddOnScreenDebugMessage(
				KeyBase + 12, 0.f, bForceIsAcceleration ? FColor::Cyan : FColor::Silver,
				FString::Printf(TEXT("[HoloRope] Mode: %s | BodyMass: %.1f"),
					bForceIsAcceleration ? TEXT("Acceleration") : TEXT("Force"),
					Body->GetMass()));
		}
	}
}


FVector UHoloRopeBindingComponent::GetBinderAnchorLocation() const
{
	const AActor* Binder = GetOwner();
	if (!IsValid(Binder))
	{
		return FVector::ZeroVector;
	}

	if (const USceneComponent* SocketComp = BinderAnchorComp.Get())
	{
		return SocketComp->GetSocketLocation(BinderSocketName);
	}
	return Binder->GetActorLocation();
}


FVector UHoloRopeBindingComponent::GetTargetAnchorLocation() const
{
	const AActor* Target = BoundTarget.Get();
	if (!IsValid(Target))
	{
		return FVector::ZeroVector;
	}

	if (const USceneComponent* SocketComp = TargetAnchorComp.Get())
	{
		return SocketComp->GetSocketLocation(TargetSocketName);
	}

	// Prefer the body's center of mass if we have it, else actor location.
	if (const UPrimitiveComponent* Body = BoundTargetPhyBody.Get())
	{
		return Body->GetComponentLocation();
	}
	return Target->GetActorLocation();
}


USceneComponent* UHoloRopeBindingComponent::FindComponentWithSocket(const AActor* Actor, FName SocketName) const
{
	if (!IsValid(Actor) || SocketName == NAME_None)
	{return nullptr;}

	// Prefer the root if it owns the socket
	if (USceneComponent* Root = Actor->GetRootComponent())
	{
		if (Root->DoesSocketExist(SocketName))
		{
			return Root;
		}
	}

	// Otherwise, first scene component that owns the socket.
	TArray<USceneComponent*> SceneComps;
	Actor->GetComponents<USceneComponent>(SceneComps);
	for (USceneComponent* Comp : SceneComps)
	{
		if (Comp && Comp->DoesSocketExist(SocketName))
		{
			return Comp;
		}
	}

	return nullptr;
}

void UHoloRopeBindingComponent::CacheAnchorComponents()
{
	BinderAnchorComp = FindComponentWithSocket(GetOwner(), BinderSocketName);
	TargetAnchorComp = FindComponentWithSocket(BoundTarget.Get(), TargetSocketName);
}


UPrimitiveComponent* UHoloRopeBindingComponent::ResolveTargetPhyBody() const
{
    AActor* Target = BoundTarget.Get();
    if (!IsValid(Target)) {return nullptr;}

    // 1) Prefer the root if it simulates physics.
    if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
    {
        if (Root->IsSimulatingPhysics())
        {
            return Root;
        }
    }

    // 2) Otherwise, first simulating primitive we can find.
    TArray<UPrimitiveComponent*> Prims;
    Target->GetComponents<UPrimitiveComponent>(Prims);
    for (UPrimitiveComponent* Prim : Prims)
    {
        if (Prim && Prim->IsSimulatingPhysics())
        {
            return Prim;
        }
    }

    return nullptr;
}


void UHoloRopeBindingComponent::SetBoundTickEnabled(bool bEnabled)
{
    SetComponentTickEnabled(bEnabled);
}