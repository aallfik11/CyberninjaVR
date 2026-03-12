// Fill out your copyright notice in the Description page of Project Settings.


#include "VRUtilsLibrary.h"

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


FVector UVRUtilsLibrary::CalculateLocalYAxis(const FVector &LocalXAxis)
{
	FVector GlobalXAxis{1, 0, 0};
	FVector GlobalYAxis{0, 1, 0};
	FQuat   GlobalXToLocal = FQuat::FindBetweenVectors(GlobalXAxis, LocalXAxis);

	GlobalXToLocal.Normalize();
	return GlobalXToLocal.RotateVector(GlobalYAxis).GetSafeNormal();
}

FVector UVRUtilsLibrary::CalculateLocalZAxis(const FVector &LocalXAxis)
{
	FVector GlobalXAxis{1, 0, 0};
	FVector GlobalZAxis{0, 0, 1};
	FQuat   GlobalXToLocal = FQuat::FindBetweenVectors(GlobalXAxis, LocalXAxis);

	GlobalXToLocal.Normalize();
	return GlobalXToLocal.RotateVector(GlobalZAxis).GetSafeNormal();
}

FJointOrientation UVRUtilsLibrary::CalculateElbowJointTargetLocation(const FVector &          ControllerForwardVector,
                                                                     const FVector &          ControllerUpVector,
                                                                     const FVector &          ControllerRightVector,
                                                                     const FVector &          ControllerPosition,
                                                                     const FVector &          UpperArmBonePosition,
                                                                     const float              MaxArmLength,
                                                                     const float              LowerAngleBound,
                                                                     const float              UpperAngleBound,
                                                                     const FJointOrientation &PreviousOrientation,
                                                                     const bool               bIsRightHand,
                                                                     const bool               bDrawDebug)
{
	// const FVector &ControllerXAxis = ControllerForwardVector.GetUnsafeNormal(); //forward vector will never be zero
	// const FVector  ControllerYAxis = CalculateLocalYAxis(ControllerXAxis);
	// const FVector  ControllerZAxis = CalculateLocalZAxis(ControllerXAxis);

	const FVector &ControllerXAxis = ControllerForwardVector.GetUnsafeNormal();
	const FVector &ControllerYAxis = ControllerRightVector.GetUnsafeNormal();
	const FVector &ControllerZAxis = ControllerUpVector.GetUnsafeNormal();
	//negated so it points down in default position

	//Figure out which axis best lines up with the line from upper arm to controller
	const FVector ArmLine = ControllerPosition - UpperArmBonePosition;

	const float XDotArm      = FMath::Clamp(ArmLine.Dot(ControllerXAxis), 0, 1.0f);
	const float YDotArm      = FMath::Clamp(ArmLine.Dot(ControllerYAxis), 0, 1.0f);
	const float ZDotArm      = FMath::Clamp(ArmLine.Dot(ControllerZAxis), 0, 1.0f);
	const float XPrimeDotArm = FMath::Clamp(ArmLine.Dot(-ControllerXAxis), 0, 1.0f);
	const float YPrimeDotArm = FMath::Clamp(ArmLine.Dot(-ControllerYAxis), 0, 1.0f);
	const float ZPrimeDotArm = FMath::Clamp(ArmLine.Dot(-ControllerZAxis), 0, 1.0f);

	// Find the best Dot product and determine the driving axis
	// By default we'll consider X to be best unless proven otherwise

	const FVector *Forward = &ControllerXAxis;
	FVector        Driver  = -ControllerZAxis;
	if (FMath::Abs(YDotArm) > FMath::Abs(XDotArm))
	{
		Forward = &ControllerYAxis;
		Driver  = -ControllerXAxis;
		// driver can still be Z axis as a rotation from X to Y for the "forward" doesn't change Z 
	}
	if (FMath::Abs(ZDotArm) > FMath::Abs(XDotArm))
	{
		// If Z becomes the best axis, that means that the X Axis will be used as the driver
		Forward = &ControllerZAxis;
		Driver  = -ControllerXAxis;
		if (FMath::Sign(ZDotArm) == -1)
		{
			//Negative Z Dot means we need to use negative x axis as driver
			// Driver = -Driver;
		}
	}

	//construct influences. If the dot product is positive it will influence the final position based on its value
	// We can just clamp the dot to 0 -> 1 and it will result in nice weights, automatically discarding any negative dots
	// Without the need to do a bunch of If-else checks (even though I imagine doing that is a tad more expensive)
	// Realistically though, 3 vectors created and multiplied by 0 shouldn't really impact the performance in any perceivable way

	//The table of influences is as follows (Which axis will be the driver axis for each "aligned" axis
	//Aligned axis means that it is relatively forward in regard to the arm-hand axis
	// A stands for axis aligned
	// D stands for driver axis
	/* A     D
	 * X -> -Z
	 * Y -> -X
	 * Z -> -X
	 * X'-> -Y //todo: check
	 * Y'-> -X
	 * Z'-> -X
	 */
	Driver = {0, 0, 0};
	Driver += XDotArm * -ControllerZAxis;
	Driver += YDotArm * -ControllerXAxis;
	Driver += ZDotArm * -ControllerXAxis;
	Driver += XPrimeDotArm * -ControllerYAxis;
	Driver += YPrimeDotArm * -ControllerXAxis;
	Driver += ZPrimeDotArm * -ControllerXAxis;

	if (Driver.Normalize() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("%hs : Driver vector normalization failed"), __FUNCTION__);
	}

	// Constructing the "Wheel" of rotation for the elbow

	const float   Radius           = FMath::Abs(ArmLine.Length() - MaxArmLength);
	const FVector Center           = UpperArmBonePosition + (ArmLine.GetSafeNormal() * ArmLine.Length() / 2.0f);
	const FVector ArmDownDirection = -CalculateLocalZAxis(ArmLine);
	const FVector ArmDownPoint     = Center + (ArmDownDirection * Radius);

	// The circle lies on a plane defined by its center and the normal defined by the arm line's direction
	FPlane RotationPlane(Center, ArmLine.GetSafeNormal());

	// Now let's project the driving axis onto the circle plane
	// const FVector FromCenterToDriver = (Center + *Driver).GetSafeNormal();
	const FVector FromCenterToDriver = Center + (Driver * Radius);
	const FVector FromCenterToDriverProjected = FVector::PointPlaneProject(FromCenterToDriver, RotationPlane);
	const FVector DriverProjected = Center + (FromCenterToDriverProjected - Center).GetSafeNormal() * Radius;
	// const FVector DriverProjected    = UKismetMathLibrary::ProjectPointOnToPlane(
	// 	FromCenterToDriver,
	// 	ArmLine.GetSafeNormal());

	// figure out the rotation between these two
	// float Angle = FMath::Clamp(UKismetMathLibrary::DegAcos((ArmDownDirection + Center).Dot(DriverProjected - Center)),
	// LowerAngleBound,
	// UpperAngleBound);

	const FVector CenterToDown            = (ArmDownPoint - Center).GetSafeNormal();
	const FVector CenterToProjectedDriver = (DriverProjected - Center).GetSafeNormal();
	float         Angle                   = UKismetMathLibrary::DegAcos(CenterToDown | CenterToProjectedDriver);
	const FVector Cross                   = CenterToDown ^ CenterToProjectedDriver;
	if (Cross.Dot(ArmLine.GetSafeNormal()) < 0.0f)
	{
		Angle = -Angle;
	}
	// Angle = UKismetMathLibrary::ClampAngle(Angle, LowerAngleBound, UpperAngleBound);
	Angle = FMath::Clamp(Angle, LowerAngleBound, UpperAngleBound);
	// Angle = bIsRightHand ? -Angle : Angle;

	// Angle = UKismetMathLibrary::DegAcos((ArmDownPoint - Center).GetSafeNormal().Dot((DriverProjected - Center).GetSafeNormal()));
	// Rotate the down vector by this angle to get the final, clamped target position
	// const FVector FinalJointTarget = DriverProjected.RotateAngleAxis(Angle, ArmLine.GetSafeNormal());
	const FVector FinalJointTargetPos = Center + UKismetMathLibrary::RotateAngleAxis(
		(ArmDownPoint - Center),
		Angle,
		ArmLine);

	FJointOrientation FinalJointTarget{FinalJointTargetPos, Angle};
	// In order to prevent flipping due to the clamping of angle when crossing negative/positive degrees, we check against previous angle, and if the difference is too big to be humanly possible, we return the previous value

	/*
	if (FMath::Abs(Angle - PreviousOrientation.ArmCircleAngle) >= (FMath::Abs(
		FMath::Abs(LowerAngleBound) - FMath::Abs(UpperAngleBound))))
	{
		FinalJointTarget = PreviousOrientation;
	}
	*/

	//track the state and prevent flipping here

	// First calculate state, for negative angles we remap it to a > 180 range
	if (FMath::Sign(Angle) == -1)
	{
		Angle += 360;
	}

	//Just in case?
	Angle = FMath::Clamp(Angle, 0, 360);
	// Since angle is always guaranteed to be within 0-360, this is safe to do
	ERotationState CalculatedState = StaticCast<ERotationState>(FMath::TruncToInt(Angle / 90.f));

	if (PreviousOrientation.RotationState == ERotationState::ENeutral || PreviousOrientation.RotationState ==
		CalculatedState)
	{
		// No additional checks needed here
		FinalJointTarget.RotationState = CalculatedState;
	}
	else
	{
		// We need to check if the transition is correct
		bool bIsStateTransitionValid = false;
		switch (PreviousOrientation.RotationState)
		{
		case ERotationState::ELowerLeft:
		{
			bIsStateTransitionValid = (CalculatedState == ERotationState::EUpperLeft || CalculatedState ==
				ERotationState::ELowerRight);
			break;
		}
		case ERotationState::ELowerRight:
		{
			bIsStateTransitionValid = (CalculatedState == ERotationState::EUpperRight || CalculatedState ==
				ERotationState::ELowerLeft);
			break;
		}
		case ERotationState::EUpperRight:
		{
			bIsStateTransitionValid = (CalculatedState == ERotationState::ELowerRight || CalculatedState ==
				ERotationState::EUpperLeft);
			break;
		}
		case ERotationState::EUpperLeft:
		{
			bIsStateTransitionValid = (CalculatedState == ERotationState::ELowerLeft || CalculatedState ==
				ERotationState::EUpperRight);
			break;
		}
		default: checkf(false, TEXT("%hs : Previous orientation contains an invalid state"), __FUNCTION__);
		}

		if (bIsStateTransitionValid)
		{
			FinalJointTarget.RotationState = CalculatedState;
		}
		else
		{
			FinalJointTarget = PreviousOrientation;
		}
	}
	// Draw some debug shapes to visualise all the moving parts
	if (bDrawDebug)
	{
		if (GEngine == nullptr)
		{
			return FinalJointTarget;
		}
		const UWorld *const World = GEngine->GameViewport->GetWorld();
		if (World == nullptr)
		{
			return FinalJointTarget;
		}

		// Draw Arm Line
		UKismetSystemLibrary::DrawDebugLine(World,
		                                    UpperArmBonePosition,
		                                    ControllerPosition,
		                                    FColor::White,
		                                    0.05f,
		                                    0.1f);

		// Draw reference circle of movement for the elbow
		UKismetSystemLibrary::DrawDebugCircle(World,
		                                      Center,
		                                      Radius,
		                                      16,
		                                      FColor::Magenta,
		                                      0.05f,
		                                      0.1f,
		                                      CalculateLocalYAxis(ArmLine),
		                                      CalculateLocalZAxis(ArmLine),
		                                      true);

		// Draw reference ball and line to it
		UKismetSystemLibrary::DrawDebugSphere(World, ArmDownPoint, 8, 4, FColor::Green, 0.05f, 0.1f);
		UKismetSystemLibrary::DrawDebugLine(World, Center, ArmDownPoint, FColor::Green, 0.05f, 0.1f);

		// Draw calculated point's ball and line to it
		UKismetSystemLibrary::DrawDebugLine(World, Center, FinalJointTarget.TargetPosition, FColor::Cyan, 0.05f, 0.1f);
		UKismetSystemLibrary::DrawDebugSphere(World, FinalJointTarget.TargetPosition, 8, 4, FColor::Cyan, 0.05f, 0.1f);

		// Draw the projected driver axis
		UKismetSystemLibrary::DrawDebugLine(World, Center, DriverProjected, FColor::Cyan, 0.05f, 0.1f);
		// UKismetSystemLibrary::DrawDebugLine(World, Center, FromCenterToDriver, FColor::Yellow, 0.05f, 0.1f);

		//Draw Controller Axes
		UKismetSystemLibrary::DrawDebugLine(World,
		                                    ControllerPosition,
		                                    ControllerPosition + (ControllerXAxis * 10),
		                                    FColor::Red,
		                                    0.05f,
		                                    0.1f);
		UKismetSystemLibrary::DrawDebugLine(World,
		                                    ControllerPosition,
		                                    ControllerPosition + (ControllerYAxis * 10),
		                                    FColor::Yellow,
		                                    0.05f,
		                                    0.1f);
		UKismetSystemLibrary::DrawDebugLine(World,
		                                    ControllerPosition,
		                                    ControllerPosition + (ControllerZAxis * 10),
		                                    FColor::Blue,
		                                    0.05f,
		                                    0.1f);

		GEngine->AddOnScreenDebugMessage(144, 0.1, FColor::Green, FString::Printf(TEXT("Angle: %f"), Angle), true);
	}

	return FinalJointTarget;
}

float UVRUtilsLibrary::PlayRandomMontageSection(UAnimMontage *          Montage,
                                                USkeletalMeshComponent *TargetSkeletalMesh,
                                                float                   InPlayRate)
{
	UAnimInstance *AnimInstance = (TargetSkeletalMesh) ? TargetSkeletalMesh->GetAnimInstance() : nullptr;

	if (Montage && AnimInstance)
	{
		int32 NumMontageSections = Montage->GetNumSections();
		if (NumMontageSections <= 0)
		{
			return 0.0f;
		}

		int32 RandomIdx         = FMath::RandRange(0, NumMontageSections - 1);
		FName RandomSectionName = Montage->GetSectionName(RandomIdx);
		AnimInstance->Montage_Play(Montage, InPlayRate);
		const float Duration = Montage->GetSectionLength(RandomIdx);

		if (Duration > 0.f)
		{
			// Start at a given Section.
			if (RandomSectionName != NAME_None)
			{
				AnimInstance->Montage_JumpToSection(RandomSectionName, Montage);
			}

			return Duration;
		}
	}

	return 0.0f;
}

float UVRUtilsLibrary::PlayMontageSection(UAnimMontage *          Montage,
                                          USkeletalMeshComponent *TargetSkeletalMesh,
                                          float                   InPlayRate,
                                          FName                   SectionName)
{
	UAnimInstance *AnimInstance = (TargetSkeletalMesh) ? TargetSkeletalMesh->GetAnimInstance() : nullptr;

	if (Montage && AnimInstance && SectionName.IsNone() == false)
	{
		const int32 SectionIdx = Montage->GetSectionIndex(SectionName);
		if (SectionIdx != INDEX_NONE)
		{
			AnimInstance->Montage_Play(Montage, InPlayRate);
			const float Duration = Montage->GetSectionLength(SectionIdx);

			if (Duration > 0.f)
			{
				// Start at a given Section.
				AnimInstance->Montage_JumpToSection(SectionName, Montage);
				return Duration;
			}
		}
	}
	return 0.0f;
}
