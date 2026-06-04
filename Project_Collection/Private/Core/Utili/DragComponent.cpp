// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Utili/DragComponent.h"
#include "Net/UnrealNetwork.h"



UDragComponent::UDragComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// this component's tick runs before physics,so desired target state can be cached for the substep callback.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);

	SubstepDragDelegate.BindUObject(this, &UDragComponent::Auth_SubstepApplyDrag);
}


void UDragComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{return;}

	
	// Deferred stop from physics callback-safe path.
	if (bPendingStopDrag)
	{
		Request_StopDrag();
		return;
	}
	

	if (State == EDragState::Dragging)
	{
		Auth_UpdateDrag(DeltaTime);
	}
}


void UDragComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDragComponent, State);
}


// ==================== APIs ====================


void UDragComponent::Request_StartDrag()
{
	FVector ViewLoc, ViewDir;
	if (!GetOwnerViewPoint(ViewLoc, ViewDir))
	{
		return;
	}
	

	// Client RPC
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		RpcServer_TryStartDrag(ViewLoc, ViewDir);
		return;
	}

	// Is Server 
	Request_StopDrag(); // clean existing drag
	Auth_TryStartDrag(ViewLoc, ViewDir);

}


void UDragComponent::Request_StopDrag()
{
	// Client RPC
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		RpcServer_StopDrag();
		return;
	}

	// Is Server 
	Grabbed = nullptr;
	GrabbedBone = NAME_None;
	State = EDragState::Undrag;


	LocalGrabPoint = FVector::ZeroVector;
	GrabDistance = 0.f;

	CachedDesiredPoint = FVector::ZeroVector;
	CachedDesiredVelocity = FVector::ZeroVector;
	PrevDesiredPoint = FVector::ZeroVector;
	bHasPrevDesiredPoint = false;
	bPendingStopDrag = false;
}


// ==================== Internal ==================== 


bool UDragComponent::GetOwnerViewPoint(FVector& OutLocation, FVector& OutDirection) const
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


void UDragComponent::Auth_UpdateDrag(float DeltaTime)
{

	// Failure Cases
	UPrimitiveComponent* Comp = Grabbed.Get();
	if (!Comp || !Comp->IsSimulatingPhysics(GrabbedBone))
	{
		Request_StopDrag();
		return;
	}
	
	FVector ViewLoc, ViewDir;
	if (!GetOwnerViewPoint(ViewLoc, ViewDir))
	{
		bPendingStopDrag = true;
		return;
	}

	const FVector SafeDir = ViewDir.GetSafeNormal();
	if (SafeDir.IsNearlyZero())
	{
		bPendingStopDrag = true;
		return;
	}
	
	
	// Cache desired target state once per game thread tick.
	const FVector NewDesired = ViewLoc + SafeDir * GrabDistance;
	CachedDesiredPoint = NewDesired;

	if (bHasPrevDesiredPoint && DeltaTime > KINDA_SMALL_NUMBER)
	{
		CachedDesiredVelocity = (NewDesired - PrevDesiredPoint) / DeltaTime;
		CachedDesiredVelocity = CachedDesiredVelocity.GetClampedToMaxSize(MaxDesiredSpeed);
	}
	else
	{
		CachedDesiredVelocity = FVector::ZeroVector;
	}

	PrevDesiredPoint = NewDesired;
	bHasPrevDesiredPoint = true;

	if (bForceWakeWhileDragging)
	{
		Comp->WakeRigidBody(GrabbedBone);
	}

	// apply force from physics substeps.
	if (bUseSubstepForces)
	{
		if (FBodyInstance* BI = Comp->GetBodyInstance(GrabbedBone))
		{ // This binding will trigger Auth_SubstepDrag in physics thread during the physics substep tick,
			// which is more stable at high stiffness and lower fps.
			BI->AddCustomPhysics(SubstepDragDelegate);
		}
		else
		{
			bPendingStopDrag = true;
		}
	}
	else
	{
		// Fallback to applying forces directly from the game thread if sub-stepping is disabled,
		// which is less stable but has lower latency.
		Auth_SubstepApplyDrag(DeltaTime, nullptr);
	}
}


void UDragComponent::Auth_SubstepApplyDrag(float DeltaTime, FBodyInstance* BodyInstance)
{
	UPrimitiveComponent* Comp = Grabbed.Get();
	if (!Comp || !Comp->IsSimulatingPhysics(GrabbedBone))
	{
		bPendingStopDrag = true;
		return;
	}

	const FVector WorldGrabPoint = Comp->GetComponentTransform().TransformPosition(LocalGrabPoint);

	// Fetch cached from game tick.
	const FVector Desired = CachedDesiredPoint;
	const FVector DesiredVel = CachedDesiredVelocity;
	const FVector Error = Desired - WorldGrabPoint;

	// Release if the body can't keep up
	if (Error.SizeSquared() > FMath::Square(ReleaseDistance))
	{
		bPendingStopDrag = true;
		return;
	}

	const float Dt = FMath::Clamp(DeltaTime, DT_LOWERBOUND, DT_UPPERBOUND);
	const float Mass = Comp->GetMass();
	const FVector PointVel = Comp->GetPhysicsLinearVelocityAtPoint(WorldGrabPoint, GrabbedBone);
	FVector Force = FVector::ZeroVector;

	// I am not from MECH. these algo just works.
	if (bUseStablePD)
	{ // Stable PD
		const float Freq = FMath::Max(ResponseHz, 0.1f);
		const float Omega = 2.f * PI * Freq;
		const float Kp = Omega * Omega;
		const float Kd = 2.f * DampingRatio * Omega;

		const float G = 1.f / (1.f + Kd * Dt + Kp * Dt * Dt);
		const float Ksg = Kp * G;
		const float Kdg = (Kd + Kp * Dt) * G;

		const FVector VelError = DesiredVel - PointVel;
		Force = (Ksg * Error + Kdg * VelError) * Mass;
	}
	else
	{ // Legacy PD
		const float SafeStiffness = FMath::Max(0.f, Stiffness);
		const float Damping = 2.f * DampingRatio * FMath::Sqrt(SafeStiffness);
		Force = (SafeStiffness * Error - Damping * PointVel) * Mass;
	}

	// Canceling gravity
	if (bEnableGravityCompensation)
	{
		const float Gz = GetWorld() ? GetWorld()->GetGravityZ() : BackupGravitationalForce;
		Force += FVector(0.f, 0.f, -Gz) * Mass * GravityCompensation;
	}

	// Clamp and apply resultant force
	const float RawMag = Force.Size();
	const bool bClamped = RawMag > Strength;
	if (bClamped && RawMag > KINDA_SMALL_NUMBER)
	{
		Force *= (Strength / RawMag);
	}
	Comp->AddForceAtLocation(Force, WorldGrabPoint, GrabbedBone);

	// Add damping torque
	const FVector COM = Comp->GetCenterOfMass(GrabbedBone);
	const FVector Arm = WorldGrabPoint - COM;
	const FVector AngVel = Comp->GetPhysicsAngularVelocityInRadians(GrabbedBone);
	if (bEnableAngularDamping)
	{
		Comp->AddTorqueInRadians(-AngularDamping * AngVel, GrabbedBone, true);
	}

	
	if (bForceWakeWhileDragging)
	{
		Comp->WakeRigidBody(GrabbedBone);
	}
	
	// ----- Debug Readout -----
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if (bDebugDraw)
	{
		const UWorld* W = GetWorld();
		DrawDebugSphere(W, Desired,   6.f, 8,  FColor::Green, false, -1.f, 0, 0.5f);
		DrawDebugSphere(W, WorldGrabPoint, 6.f, 8,  FColor::Cyan,  false, -1.f, 0, 0.5f);
		DrawDebugLine(W, WorldGrabPoint, Desired, FColor::Yellow, false, -1.f, 0, 1.f);   // error
		DrawDebugLine(W, COM, WorldGrabPoint, FColor::Magenta, false, -1.f, 0, 1.f);      // torque arm r
		DrawDebugLine(W, WorldGrabPoint, WorldGrabPoint + Force * 0.0005f, FColor::Red, false, -1.f, 0, 1.f); // force dir

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
#endif
}


bool UDragComponent::Auth_TryStartDrag(const FVector& ViewLoc, const FVector& ViewDir)
{
	// Failure cases
	
	const FVector SafeDir = ViewDir.GetSafeNormal();
	if (SafeDir.IsNearlyZero())
	{
		return false;
	}

	const FVector TraceEnd = ViewLoc + SafeDir * MaxGrabDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DragTrace), true, GetOwner());
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLoc, TraceEnd, TraceChannel, Params);

	if (!bHit)
	{
		return false;
	}

	UPrimitiveComponent* Comp = Hit.GetComponent();
	if (!Comp || !Comp->IsSimulatingPhysics(Hit.BoneName))
	{ // Not a physics body
		return false;
	}

	
	// Cache grab info. Store the contact point in the body's local space so the
	// force always applies to the same spot even as the object tumbles.
	Grabbed = Comp;
	GrabbedBone = Hit.BoneName;
	LocalGrabPoint = Comp->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
	GrabDistance = (Hit.ImpactPoint - ViewLoc).Size();

	
	// Initialize target cache to avoid first-frame velocity spike.
	CachedDesiredPoint = ViewLoc + SafeDir * GrabDistance;
	PrevDesiredPoint = CachedDesiredPoint;
	CachedDesiredVelocity = FVector::ZeroVector;
	bHasPrevDesiredPoint = true;
	bPendingStopDrag = false;

	if (bForceWakeWhileDragging)
	{
		Comp->WakeRigidBody(GrabbedBone);
	}

	
	
	State = EDragState::Dragging;
	return true;
}


void UDragComponent::RpcServer_TryStartDrag_Implementation(FVector_NetQuantize ViewLoc, FVector_NetQuantizeNormal ViewDir)
{
	Request_StopDrag();
	Auth_TryStartDrag(ViewLoc, ViewDir);
}


void UDragComponent::RpcServer_StopDrag_Implementation()
{
	Request_StopDrag();
}

