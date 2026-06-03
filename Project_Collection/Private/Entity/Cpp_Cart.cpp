// Fill out your copyright notice in the Description page of Project Settings.


#include "Entity/Cpp_Cart.h"

#include "Components/SceneComponent.h"

// Sets default values
ACpp_Cart::ACpp_Cart()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}


// ====================

void ACpp_Cart::BeginPlay()
{
	Super::BeginPlay();
}

