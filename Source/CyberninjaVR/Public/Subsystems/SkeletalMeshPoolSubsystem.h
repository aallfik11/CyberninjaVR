// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletalMeshPoolSubsystem.generated.h"

/**
 * 
 */

UENUM()
enum class EPoolFlag
{
	PF_None                   = 0x000,
	PF_Free                   = 0x001,
	PF_MarkedForCleanup       = 0x002,
	PF_PendingResourceRelease = 0x004
};

UCLASS()
class CYBERNINJAVR_API USkeletalMeshPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;

	virtual void OnWorldBeginPlay(UWorld &InWorld) override;

	virtual void Deinitialize() override;

	USkeletalMesh *GetPooledSkeletalMesh();
	/*
	 * Warning: Do not use the mesh after returning
	 */
	void ReturnPooledSkeletalMesh(const USkeletalMesh *PooledMesh);

	UPhysicsAsset *GetPooledPhysicsAsset();
	/*
	 * Warning: Do not use the physics asset after returning
	 */
	void ReturnPooledPhysicsAsset(const UPhysicsAsset *PooledPhysicsAsset);

private:
	FCriticalSection        PoolAccessLock;
	static constexpr uint32 PoolSize = 128;

	void           PoolCleanup();
	USkeletalMesh *CreatePooledSkeletalMesh();
	UPhysicsAsset *CreatePooledPhysicsAsset();

	UPROPERTY()
	TMap<USkeletalMesh *, EPoolFlag> SkeletalMeshPool;
	UPROPERTY()
	TMap<UPhysicsAsset *, EPoolFlag> PhysicsAssetPool;

};
