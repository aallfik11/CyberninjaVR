// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VRUtilsLibrary.generated.h"

UENUM(BLueprintType)
enum class ERotationState : uint8
{
	ELowerLeft,
	ELowerRight,
	EUpperRight,
	EUpperLeft,
	ENeutral UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct FJointOrientation
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite)
	FVector TargetPosition {0, 0, 0};
	UPROPERTY(BlueprintReadWrite)
	float	ArmCircleAngle {0.0f};
	UPROPERTY(BlueprintReadWrite)
	ERotationState RotationState {ERotationState::ENeutral};
};

/**
 * Library containing various (hopefully) useful stuff that I might come up journey that doesn't really belong to any particular class
 */
UCLASS()
class CYBERNINJAVR_API UVRUtilsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	
	/**
	 * Based on the supplied local X axis vector, rotates the global Y axis around a quaternion determined
	 * between the local and global X axes, which *should* result in a proper, local Y Axis
	 * 
	 * @param LocalXAxis A vector defining the local X Axis
	 * @return Y Axis in the coordinate space of the supplied X Axis
	 */
	UFUNCTION(BlueprintPure, Category = "VRUtils | Coordinate Spaces")
	static FVector CalculateLocalYAxis(const FVector& LocalXAxis);
	
	/**
	 * Based on the supplied local X axis vector, rotates the global Z Axis around a quaternion determined
	 * between the local and global X axes, which *should* result in a proper, local Z Axis
	 * 
	 * @param LocalXAxis A vector defining the local X Axis
	 * @return Z Axis in the coordinate space of the supplied X Axis
	 */
	UFUNCTION(BlueprintPure, Category = "VRUtils | Coordinate Spaces")
	static FVector CalculateLocalZAxis(const FVector& LocalXAxis);

	/**
	 * Creates a virtual circle upon which the joint target will rotate in accordance to the controller, returning the final target's position
	 * 
	 * @param ControllerForwardVector 
	 * @param ControllerPosition 
	 * @param UpperArmBonePosition 
	 * @param LowerAngleBound 
	 * @param UpperAngleBound 
	 * @param bIsRightHand 
	 * @return 
	 */
	UFUNCTION(BlueprintCallable, Category = "VRUtils | IK")
	static FJointOrientation CalculateElbowJointTargetLocation(const FVector& ControllerForwardVector, const FVector& ControllerUpVector, const FVector& ControllerRightVector, const FVector& ControllerPosition, const FVector& UpperArmBonePosition, const float MaxArmLength, const float LowerAngleBound, const float UpperAngleBound, const FJointOrientation& PreviousOrientation ,const bool bIsRightHand, const bool bDrawDebug = false);
	
	// Plays a random animation from the supplied montage.
	/**
	 * 
	 * @param Montage
	 * @param AnimatedCharacter 
	 * @param InPlayRate 
	 * @return 0 in case of failure, the section's length otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "VRUtils | Animations")
	static float PlayRandomMontageSection(UAnimMontage* Montage, USkeletalMeshComponent* TargetSkeletalMesh, float InPlayRate = 1.0f);
	
	// Plays a selected animation from the supplied montage.
	/**
	 * 
	 * @param Montage
	 * @param AnimatedCharacter 
	 * @param InPlayRate 
	 * @return 0 in case of failure, the section's length otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "VRUtils | Animations")
	static float PlayMontageSection(UAnimMontage* Montage, USkeletalMeshComponent* TargetSkeletalMesh, float InPlayRate = 1.0f, FName SectionName = NAME_None);
};
