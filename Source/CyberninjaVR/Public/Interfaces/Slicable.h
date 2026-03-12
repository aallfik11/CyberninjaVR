// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Slicable.generated.h"

// This class does not need to be modified.
UINTERFACE(BlueprintType, Blueprintable)
class USlicable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class CYBERNINJAVR_API ISlicable
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Slicing")
	void SliceMesh(UMeshComponent* MeshComponent, const FVector SlicePoint, const FVector SliceNormal, UMaterialInterface* CapMaterial, const FName BoneName = NAME_None);


};
