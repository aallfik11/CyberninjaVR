// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystems/SkeletalMeshPoolSubsystem.h"

#include "Engine/SkeletalMeshLODSettings.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/SkinnedAssetCommon.h"

void USkeletalMeshPoolSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	SkeletalMeshPool.Reserve(PoolSize);
	PhysicsAssetPool.Reserve(PoolSize);
	for (int32 PoolIdx = 0; PoolIdx < PoolSize; PoolIdx++)
	{
		SkeletalMeshPool.Add(CreatePooledSkeletalMesh(), EPoolFlag::PF_Free);
		PhysicsAssetPool.Add(CreatePooledPhysicsAsset(), EPoolFlag::PF_Free);
	}
}

void USkeletalMeshPoolSubsystem::OnWorldBeginPlay(UWorld &InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FTimerHandle DummyHandle;
	InWorld.GetTimerManager().SetTimer(DummyHandle, this, &USkeletalMeshPoolSubsystem::PoolCleanup, 5, true);
}

void USkeletalMeshPoolSubsystem::Deinitialize()
{
	Super::Deinitialize();
	FScopeLock AccessLock(&PoolAccessLock);
	for (auto &[Mesh, Flag] : SkeletalMeshPool)
	{
#if WITH_EDITOR
		if (Mesh->GetLODSettings())
		{
			Mesh->GetLODSettings()->ClearInternalFlags(EInternalObjectFlags::Async);
			Mesh->GetLODSettings()->MarkAsGarbage();
		}
#endif
		Mesh->ReleaseResources();
		Mesh->ReleaseCPUResources();
		Mesh->MarkAsGarbage();
		Flag = EPoolFlag::PF_None;
	}

	for (auto &[PhysAsset, Flag] : PhysicsAssetPool)
	{
		PhysAsset->ClearAllPhysicsMeshes();
		PhysAsset->MarkAsGarbage();
		Flag = EPoolFlag::PF_None;
	}

	SkeletalMeshPool.Empty();
	PhysicsAssetPool.Empty();
}

USkeletalMesh *USkeletalMeshPoolSubsystem::GetPooledSkeletalMesh()
{
	FScopeLock     AccessLock(&PoolAccessLock);
	USkeletalMesh *Mesh = nullptr;
	for (auto &[SkeletalMesh, Flag] : SkeletalMeshPool)
	{
		if (Flag == EPoolFlag::PF_Free)
		{
			Mesh = SkeletalMesh;
			Flag = EPoolFlag::PF_None;
			break;
		}
	}

	//All pooled meshes were used up, need to increase the pool size
	if (Mesh == nullptr)
	{
		Mesh = CreatePooledSkeletalMesh();
		SkeletalMeshPool.Add(Mesh, EPoolFlag::PF_None);
		if (IsInGameThread() == false)
		{
			AsyncTask(ENamedThreads::GameThread,
			          [Mesh]()
			          {
				          Mesh->ClearInternalFlags(EInternalObjectFlags::Async);
#if WITH_EDITOR
				          if (Mesh->GetLODSettings())
				          {
					          Mesh->GetLODSettings()->ClearInternalFlags(EInternalObjectFlags::Async);
				          }
#endif
			          });
		}
	}

	return Mesh;
}

void USkeletalMeshPoolSubsystem::ReturnPooledSkeletalMesh(const USkeletalMesh *PooledMesh)
{
	FScopeLock AccessLock(&PoolAccessLock);
	if (EPoolFlag *Flag = SkeletalMeshPool.Find(PooledMesh))
	{
		*Flag = EPoolFlag::PF_MarkedForCleanup;
	}
}

void USkeletalMeshPoolSubsystem::ReturnPooledPhysicsAsset(const UPhysicsAsset *PooledPhysicsAsset)
{
	FScopeLock AccessLock(&PoolAccessLock);
	if (EPoolFlag *Flag = PhysicsAssetPool.Find(PooledPhysicsAsset))
	{
		*Flag = EPoolFlag::PF_Free;
	}
}

UPhysicsAsset *USkeletalMeshPoolSubsystem::GetPooledPhysicsAsset()
{
	FScopeLock     AccessLock(&PoolAccessLock);
	UPhysicsAsset *PhysicsAsset = nullptr;

	for (auto &[PhysAsset, Flag] : PhysicsAssetPool)
	{
		if (Flag == EPoolFlag::PF_Free)
		{
			PhysicsAsset = PhysAsset;
			Flag         = EPoolFlag::PF_None;
		}
	}

	//All pooled physics assets were used up, need to increase the pool size
	if (PhysicsAsset == nullptr)
	{
		PhysicsAsset = CreatePooledPhysicsAsset();
		PhysicsAssetPool.Add(PhysicsAsset, EPoolFlag::PF_None);
		if (IsInGameThread() == false)
		{
			AsyncTask(ENamedThreads::GameThread,
			          [PhysicsAsset]()
			          {
				          PhysicsAsset->ClearInternalFlags(EInternalObjectFlags::Async);
			          });
		}
	}

	return PhysicsAsset;
}

void USkeletalMeshPoolSubsystem::PoolCleanup()
{
	//This should run in game thread (I think)
	FScopeLock AccessLock(&PoolAccessLock);
	check(IsInGameThread());
	for (auto &[Mesh, Flag] : SkeletalMeshPool)
	{
		if (Flag == EPoolFlag::PF_PendingResourceRelease && Mesh->ReleaseResourcesFence.IsFenceComplete())
		{
			Mesh->GetMaterials().Empty();
#if WITH_EDITORONLY_DATA
			for (auto &LODModel : Mesh->GetImportedModel()->LODModels)
			{
				LODModel.Empty();
			}
#endif
			for (auto &LODRenderData : Mesh->GetResourceForRendering()->LODRenderData)
			{
				LODRenderData.RenderSections.Empty();
				LODRenderData.ActiveBoneIndices.Empty();
				LODRenderData.RequiredBones.Empty();
			}

			Flag = EPoolFlag::PF_Free;
		}
		else if (Flag == EPoolFlag::PF_MarkedForCleanup)
		{
			Mesh->ReleaseResources();
			Flag = EPoolFlag::PF_PendingResourceRelease;
		}
	}
}

USkeletalMesh *USkeletalMeshPoolSubsystem::CreatePooledSkeletalMesh()
{
	USkeletalMesh *NewMesh = NewObject<USkeletalMesh>(GetTransientPackage(),
	                                                  MakeUniqueObjectName(
		                                                  GetTransientPackage(),
		                                                  USkeletalMesh::StaticClass(),
		                                                  TEXT("SlicedSkeletalMesh")),
	                                                  RF_Transient | RF_TextExportTransient | RF_DuplicateTransient |
	                                                  RF_Standalone);
#if WITH_EDITOR
	NewMesh->SetLODSettings(NewObject<USkeletalMeshLODSettings>(GetTransientPackage(),
	                                                            MakeUniqueObjectName(
		                                                            GetTransientPackage(),
		                                                            USkeletalMesh::StaticClass(),
		                                                            TEXT("SlicedSkeletalMesh")),
	                                                            RF_Transient | RF_TextExportTransient |
	                                                            RF_DuplicateTransient | RF_Standalone));
#endif
	return NewMesh;
}

UPhysicsAsset *USkeletalMeshPoolSubsystem::CreatePooledPhysicsAsset()
{
	UPhysicsAsset *NewPhysAsset = NewObject<UPhysicsAsset>(GetTransientPackage(),
	                                                       MakeUniqueObjectName(
		                                                       GetTransientPackage(),
		                                                       UPhysicsAsset::StaticClass(),
		                                                       TEXT("SlicedPhysicsAsset")),
	                                                       RF_Transient | RF_TextExportTransient | RF_DuplicateTransient
	                                                       | RF_Standalone);
	return NewPhysAsset;
}
