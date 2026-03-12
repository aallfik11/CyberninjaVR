// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/PoseSnapshot.h"
#include "UObject/Interface.h"
#include "PosableGib.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UPosableGib : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class CYBERNINJAVR_API IPosableGib
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	FPoseSnapshot GetGibPose();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void SetGibPose(const FPoseSnapshot& Pose);
};
