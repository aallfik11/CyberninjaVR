// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GibAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class CYBERNINJAVR_API UGibAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable)
	void ResetPhysicsFlag() { bSimulatePhysics = true; };
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	FPoseSnapshot PoseSnapshot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Physics)
	bool bSimulatePhysics = true;
};
