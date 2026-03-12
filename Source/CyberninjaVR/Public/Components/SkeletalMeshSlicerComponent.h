// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SlicerComponentBase.h"
#include "SkeletalMeshSlicerComponent.generated.h"


class UGibAnimInstance;
struct FKConvexElem;
struct FSlicingData;
struct FSkinWeightInfo;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CYBERNINJAVR_API USkeletalMeshSlicerComponent : public USlicerComponentBase
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USkeletalMeshSlicerComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

public:
	// Called every frame
	virtual void TickComponent(float                        DeltaTime,
	                           ELevelTick                   TickType,
	                           FActorComponentTickFunction *ThisTickFunction) override;

	virtual void SliceMesh(UMeshComponent *MeshComponent, const FVector SlicePoint, const FVector SliceNormal, UMaterialInterface *CapMaterial, const FName BoneName) override;

	UFUNCTION(BlueprintCallable)
	void DEBUG_Triangulate();

private:
	bool DivideMeshIntoTwoSections(const USkeletalMeshComponent *HitSkeletalMeshComponent,
	                               const FVector &               PlanePosition,
	                               const FVector &               PlaneNormal,
	                               const FName                   HitBoneName,
	                               const TArray<TArray<int32>> & BoneVertexMap,
	                               TArray<int32> &               BonesBelowPlane,
	                               TArray<int32> &               BonesAbovePlane,
	                               TArray<int32> &               SlicedBones);
	
	void TASKUSAGE_PerformVertexLevelMeshDivision(TSharedRef<FSlicingData> SlicingData);
	
	void TASKUSAGE_FillNewAssets(TSharedRef<FSlicingData> SlicingData);
	
	void TASKUSAGE_FinalizeNewAssetsCreation(TSharedRef<FSlicingData> SlicingData);
	
	void CreateMeshSlice(USkeletalMesh *                       OutSkeletalMesh,
	                     const FReferenceSkeleton &            ReferenceSkeleton,
	                     const USkeleton *                     Skeleton,
	                     const FBoxSphereBounds                ImportedBounds,
	                     const TArray<FSkinWeightInfo> &       InSkinWeights,
	                     const TArray<uint32> &                IndexBuffer,
	                     const TArray<FStaticMeshBuildVertex> &VertexBuffer,
	                     const TArray<int32> &                 PerSectionVertexOffsets,
	                     const TArray<int32> &                 PerSectionIndexOffsets,
	                     const TArray<UMaterialInterface *> &  Materials,
	                     const TArray<TArray<FBoneIndexType>> &PerSectionBoneMaps,
	                     const uint32                          MaxBoneInfluences,
	                     const uint32                          MaxTexCoords,
	                     const TArray<FBoneIndexType> &        RequiredBones,
	                     const TArray<FBoneIndexType> &        ActiveBones,
	                     const bool                            bNeedsCPUAccess,
	                     const bool                            bUseFullPrecisionUVs);
	
	void CreateMeshSlicePhysicsAsset(TSharedRef<FSlicingData> SlicingData, bool bIsAboveMesh);

	void ReturnPooledAssets();

	UPROPERTY()
	USkeletalMesh* CurrentSkeletalMesh;
	UPROPERTY()
	UPhysicsAsset* CurrentPhysicsAsset;
	UPROPERTY(EditAnywhere)
	TSubclassOf<UGibAnimInstance> GibAnimInstance;

	UPROPERTY(VisibleAnywhere)
	bool bIsCurrentlySlicing;
};

struct FSlicingData
{
	//Control bool that prevents the tasks from doing anything, in case anything goes wrong on some previous step
	bool bShouldTaskAbort = true;
	//Initial Input data
	USkeletalMeshComponent *OriginalSkeletalMeshComponent = nullptr;
	FVector                 WorldSlicePlanePos;
	FVector                 WorldSlicePlaneNormal;
	FVector                 LocalSlicePlanePos;
	FVector                 LocalSlicePlaneNormal;
	UMaterialInterface *    CapMaterial;

	USkeletalMesh* AboveSkeletalMesh = nullptr;
	UPhysicsAsset*           AbovePhysicsAsset = nullptr;
	USkeletalMesh* BelowSkeletalMesh = nullptr;
	UPhysicsAsset* BelowPhysicsAsset = nullptr;
	// FSkeletalMeshPoolItem* AbovePoolItem = nullptr;
	// FSkeletalMeshPoolItem* BelowPoolItem = nullptr;

	// Transforms at the moment the slice started
	TArray<FTransform> ComponentSpaceTransforms;
	FTransform         ComponentToWorldTransform;

	// Data from the bone division step
	TArray<int32>                  BonesAbove;
	TArray<int32>                  BonesBelow;
	TArray<int32>                  SlicedBones;
	TArray<TArray<int32>>          BoneVertexMap;
	TArray<FBoneIndexType>         MeshToRefSkelBoneMap;
	TArray<struct FSkinWeightInfo> OriginalSkinWeights;

	// Divided Mesh Data
	TArray<struct FSkinWeightInfo> AboveSkinWeights;
	TArray<FSkinWeightInfo>        BelowSkinWeights;
	TArray<uint32>                 AboveIndexBuffer;
	TArray<uint32>                 BelowIndexBuffer;
	TArray<FStaticMeshBuildVertex> AboveVertexBuffer;
	TArray<FStaticMeshBuildVertex> BelowVertexBuffer;
	TArray<int32>                  AboveRenderSectionVertexOffsets;
	TArray<int32>                  BelowRenderSectionVertexOffsets;
	TArray<int32>                  AboveRenderSectionIndexOffsets;
	TArray<int32>                  BelowRenderSectionIndexOffsets;
	TArray<UMaterialInterface *>   AboveMaterials;
	TArray<UMaterialInterface *>   BelowMaterials;
	TArray<TArray<FBoneIndexType>> AboveRenderSectionBoneMaps;
	TArray<TArray<FBoneIndexType>> BelowRenderSectionBoneMaps;
	TMap<FName, TArray<FKConvexElem>> AboveConvexElems;
	TMap<FName, TArray<FKConvexElem>> BelowConvexElems;
	uint32                         MaxBoneInfluences    = MAX_TOTAL_INFLUENCES;
	uint32                         MaxTexCoords         = MAX_TEXCOORDS;
	bool                           bNeedsCPUAccess      = true;
	bool                           bUseFullPrecisionUVs = false;


	class ASliceGib *SliceGib          = nullptr;
	// USkeletalMesh *  AboveMesh         = nullptr;
	// USkeletalMesh *  BelowMesh         = nullptr;
	// UPhysicsAsset *  AbovePhysicsAsset = nullptr;
	// UPhysicsAsset *  BelowPhysicsAsset = nullptr;

	// For Performance Measurement Purposes
	double SlicingStartTime = 0.0l;
};
