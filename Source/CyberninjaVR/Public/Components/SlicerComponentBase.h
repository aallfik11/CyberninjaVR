// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SlicerComponentBase.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CYBERNINJAVR_API USlicerComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USlicerComponentBase();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float                        DeltaTime,
	                           ELevelTick                   TickType,
	                           FActorComponentTickFunction *ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	virtual void SliceMesh(UMeshComponent* MeshComponent, const FVector SlicePoint, const FVector SliceNormal, UMaterialInterface* CapMaterial, const FName BoneName = NAME_None){};

};
