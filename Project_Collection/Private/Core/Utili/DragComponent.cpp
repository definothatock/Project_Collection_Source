// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Utili/DragComponent.h"


UDragComponent::UDragComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


void UDragComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (State == EDragState::Dragging)
	{
		UpdateDrag(DeltaTime);
	}
}

bool UDragComponent::TryStartDrag()
{
	StopDrag(); // clean

	FVector ViewLoc, ViewDir;
	if (!GetViewPoint(ViewLoc, ViewDir))
	{
		return false;
	}

	const FVector TraceEnd = ViewLoc + ViewDir * MaxGrabDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DragTrace), true, GetOwner());
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLoc, TraceEnd, TraceChannel, Params);

	if (!bHit)
	{
		return false;
	}

	UPrimitiveComponent* Comp = Hit.GetComponent();
	// Not a physics body.
	if (!Comp || !Comp->IsSimulatingPhysics(Hit.BoneName))
	{return false;}

	// Cache grab info. Store the contact point in the body's local space so the
	// force always applies to the same spot even as the object tumbles.
	Grabbed = Comp;
	GrabbedBone = Hit.BoneName;
	LocalGrabPoint = Comp->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
	GrabDistance = (Hit.ImpactPoint - ViewLoc).Size();

	State = EDragState::Dragging;
	return true;
}


void UDragComponent::StopDrag()
{
	Grabbed = nullptr;
	GrabbedBone = NAME_None;
	State = EDragState::Undrag;
}


// ==================== Internal ==================== 


bool UDragComponent::GetViewPoint(FVector& OutLocation, FVector& OutDirection) const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AController* PlyC = Pawn->GetController())
		{
			FVector Loc; FRotator Rot;
			PlyC->GetPlayerViewPoint(Loc, Rot);
			OutLocation = Loc;
			OutDirection = Rot.Vector();
			return true;
		}
	}

	if (const AActor* Owner = GetOwner())
	{
		FVector Loc; FRotator Rot;
		Owner->GetActorEyesViewPoint(Loc, Rot);
		OutLocation = Loc;
		OutDirection = Rot.Vector();
		return true;
	}
	return false;
}


void UDragComponent::UpdateDrag(float /*DeltaTime*/)
{ // commented param means unused for now (kept to match Tick-style signature / future time-based use).

	// Failure Cases
	UPrimitiveComponent* Comp = Grabbed.Get();
	if (!Comp || !Comp->IsSimulatingPhysics(GrabbedBone))
	{
		StopDrag();
		return;
	}
	FVector ViewLoc, ViewDir;
	if (!GetViewPoint(ViewLoc, ViewDir))
	{
		StopDrag();
		return;
	}
	
	// Where the grab point currently is in the world.
	const FVector GrabWorld =
		Comp->GetComponentTransform().TransformPosition(LocalGrabPoint);
	// Desired point: screen-center direction, at the original grab depth.
	const FVector Desired = ViewLoc + ViewDir * GrabDistance;
	const FVector Error = Desired - GrabWorld;

	// Release if the body can't keep up (too heavy / obstructed / yanked too hard).
	if (Error.SizeSquared() > FMath::Square(ReleaseDistance))
	{
		StopDrag();
		return;
	}


	// PD (Proportional-Derivative) Controller  (I am not from MECH I have no idea what it is really doing)

	const float Mass = Comp->GetMass();
	const FVector PointVel = Comp->GetPhysicsLinearVelocityAtPoint(GrabWorld, GrabbedBone);
	// Critically-dampable spring.
	// mass–spring–damper 2nd‑order linear ODE.
	// Multiply by Mass so the *unclamped* response is mass-independent (consistent feel);
	// Strength then caps the actual force, which is what makes heavy bodies sluggish or unliftable.
	const float Damping = 2.f * DampingRatio * FMath::Sqrt(Stiffness);
	FVector Force = (Stiffness * Error - Damping * PointVel) * Mass;

	

	const float RawMag = Force.Size();
	const bool bClamped = RawMag > Strength;
	if (bClamped)
	{
		Force *= (Strength / RawMag);
	}

	Comp->AddForceAtLocation(Force, GrabWorld, GrabbedBone);

	const FVector COM = Comp->GetCenterOfMass(GrabbedBone);
	const FVector Arm = GrabWorld - COM;                 // r in tau = r x F
	const FVector AngVel = Comp->GetPhysicsAngularVelocityInRadians(GrabbedBone);

	if (bEnableAngularDamping)
	{
		Comp->AddTorqueInRadians(-AngularDamping * AngVel, GrabbedBone, true);
	}


	
	// ----- Debug Readout -----
	if (bDebugDraw)
	{
		const UWorld* W = GetWorld();
		DrawDebugSphere(W, Desired,   6.f, 8,  FColor::Green, false, -1.f, 0, 0.5f);
		DrawDebugSphere(W, GrabWorld, 6.f, 8,  FColor::Cyan,  false, -1.f, 0, 0.5f);
		DrawDebugLine(W, GrabWorld, Desired, FColor::Yellow, false, -1.f, 0, 1.f);   // error
		DrawDebugLine(W, COM, GrabWorld, FColor::Magenta, false, -1.f, 0, 1.f);      // torque arm r
		DrawDebugLine(W, GrabWorld, GrabWorld + Force * 0.0005f, FColor::Red, false, -1.f, 0, 1.f); // force dir

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White,
				FString::Printf(TEXT("Mass: %.1f kg | Arm |r|: %.1f cm"), Mass, Arm.Size()));
			GEngine->AddOnScreenDebugMessage(2, 0.f, bClamped ? FColor::Red : FColor::Green,
				FString::Printf(TEXT("Force: %.0f / %.0f (raw %.0f) %s"),
					Force.Size(), Strength, RawMag, bClamped ? TEXT("[CLAMPED]") : TEXT("")));
			GEngine->AddOnScreenDebugMessage(3, 0.f, FColor::White,
				FString::Printf(TEXT("Error: %.1f cm | AngVel: %.2f rad/s"),
					Error.Size(), AngVel.Size()));
		}
	}
}


