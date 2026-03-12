// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/PoseSnapshot.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PosableGib.h"
#include "Interfaces/Slicable.h"
#include "SliceGib.generated.h"

class USkeletalMeshSlicerComponent;
class UCapsuleComponent;

UCLASS(NotBlueprintable)
class CYBERNINJAVR_API ASliceGib : public AActor, public IPosableGib, public ISlicable
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASliceGib();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual FPoseSnapshot GetGibPose_Implementation() override { return Pose; }
	virtual void SetGibPose_Implementation(const FPoseSnapshot &NewPose) override { Pose = NewPose; }

	USkeletalMeshComponent* GetMesh() { return Mesh; }
	USkeletalMeshSlicerComponent* GetMeshSlicer() { return SkeletalMeshSlicer; };
	virtual void SliceMesh_Implementation(UMeshComponent *MeshComponent, const FVector SlicePoint, const FVector SliceNormal, UMaterialInterface *CapMaterial, const FName BoneName) override;

private:

	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<USkeletalMeshSlicerComponent> SkeletalMeshSlicer;
	
	FPoseSnapshot Pose;
};
