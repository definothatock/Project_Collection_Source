// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cpp_Cart.generated.h"

class UPrimitiveComponent;

UCLASS()
class PROJECT_COLLECTION_API ACpp_Cart : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ACpp_Cart();
	
// ========== Param ==========

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USceneComponent* SceneRoot;


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
};
