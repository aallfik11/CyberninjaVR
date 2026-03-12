// Fill out your copyright notice in the Description page of Project Settings.


// #include "SkeletalMeshSlicerComponent.h"
#include "../../Public/Components/SkeletalMeshSlicerComponent.h"

#include "GeomTools.h"
#include "Actors/SliceGib.h"
#include "Animation/GibAnimInstance.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Interfaces/PosableGib.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Subsystems/SkeletalMeshPoolSubsystem.h"
#include "ThirdParty/CDT/CDT.h"

#define SLICER_DEFAULT_COORD_TYPE double

struct FUtilSkinnedEdge3D : FUtilEdge3D
{
	FSkinWeightInfo V0SkinWeights;
	FSkinWeightInfo V1SkinWeights;
};

struct FUtilSkinnedEdge2D 
{
	FVector2f V0;
	FVector2f V1;
	FSkinWeightInfo V0SkinWeights;
	float V0Z = 0.0f;
	FSkinWeightInfo V1SkinWeights;
	float V1Z = 0.0f;

	bool operator==(const FUtilSkinnedEdge2D &Edge) const
	{
		return FVector2f::DistSquared(V0, Edge.V0) <= (1e-4 * 1e-4) && FVector2f::DistSquared(V1, Edge.V1) <= (1e-4 *
			1e-4);
	}

	friend uint32 GetTypeHash(const FUtilSkinnedEdge2D &Edge)
	{
		float V0x = FMath::RoundToFloat(Edge.V0.X * 10000.0f) / 10000.0f;
		float V0y = FMath::RoundToFloat(Edge.V0.Y * 10000.0f) / 10000.0f;
		float V1x = FMath::RoundToFloat(Edge.V1.X * 10000.0f) / 10000.0f;
		float V1y = FMath::RoundToFloat(Edge.V1.Y * 10000.0f) / 10000.0f;
		return HashCombine(GetTypeHash(FVector2f{V0x, V0y}), GetTypeHash(FVector2f{V1x, V1y}));
	}
};

struct FEdgeVert
{
	FVector2f       V          = {UE_BIG_NUMBER, UE_BIG_NUMBER};
	float          ZComponent = UE_BIG_NUMBER;
	FSkinWeightInfo SkinWeights;

	bool operator==(const FEdgeVert &Other) const
	{
		return V == Other.V && ZComponent == Other.ZComponent;
	};

	friend uint32 GetTypeHash(const FEdgeVert &Other)
	{
		float XRounded = FMath::RoundToFloat(Other.V.X * 10000.0f) / 10000.0f;
		float YRounded = FMath::RoundToFloat(Other.V.Y * 10000.0f) / 10000.0f;
		// uint32 Hash = GetTypeHash(Other.V);
		uint32 Hash = GetTypeHash(FVector2f{XRounded, YRounded});
		/*
		Hash        = HashCombine(Hash, Other.ZComponent);
		for (uint8 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES && Other.SkinWeights.InfluenceBones[
			     InfluenceIdx] != 0; ++InfluenceIdx)
		{
			Hash = HashCombineFast(GetTypeHash(Other.SkinWeights.InfluenceBones[InfluenceIdx]), Hash);
			Hash = HashCombineFast(GetTypeHash(Other.SkinWeights.InfluenceWeights[InfluenceIdx]), Hash);
		}
		*/
		return Hash;
	}
};


static void InterpolateVert(const FStaticMeshBuildVertex &V0,
                            const FSkinWeightInfo &       V0SkinWeightInfo,
                            const FStaticMeshBuildVertex &V1,
                            const FSkinWeightInfo &       V1SkinWeightInfo,
                            const float                   Alpha,
                            FStaticMeshBuildVertex &      OutVertex,
                            FSkinWeightInfo &             OutSkinWeightInfo)
{
	// Handle dodgy alpha
	if (FMath::IsNaN(Alpha) || !FMath::IsFinite(Alpha))
	{
		OutVertex         = V1;
		OutSkinWeightInfo = V1SkinWeightInfo;
		return;
	}

	OutVertex.Position = FMath::Lerp(V0.Position, V1.Position, Alpha);
	OutVertex.TangentX = FMath::Lerp(V0.TangentX, V1.TangentX, Alpha);
	OutVertex.TangentY = FMath::Lerp(V0.TangentY, V1.TangentY, Alpha);
	OutVertex.TangentZ = FMath::Lerp(V0.TangentZ, V1.TangentZ, Alpha); //normal

	for (int32 i = 0; i < MAX_STATIC_TEXCOORDS; ++i)
	{
		OutVertex.UVs[i] = FMath::Lerp(V0.UVs[i], V1.UVs[i], Alpha);
	}

	// TODO: I am not even supporting vertex colors, might want to get rid of this
	OutVertex.Color.R = FMath::Clamp(FMath::TruncToInt(FMath::Lerp<float>(V0.Color.R, V0.Color.R, Alpha)),
	                                 0,
	                                 255);
	OutVertex.Color.G = FMath::Clamp(FMath::TruncToInt(FMath::Lerp<float>(V0.Color.G, V0.Color.G, Alpha)),
	                                 0,
	                                 255);
	OutVertex.Color.B = FMath::Clamp(FMath::TruncToInt(FMath::Lerp<float>(V0.Color.B, V0.Color.B, Alpha)),
	                                 0,
	                                 255);
	OutVertex.Color.A = FMath::Clamp(FMath::TruncToInt(FMath::Lerp<float>(V0.Color.A, V0.Color.A, Alpha)),
	                                 0,
	                                 255);

	typedef TArray<FBoneIndexType, TFixedAllocator<MAX_TOTAL_INFLUENCES>> SkinBoneArray;
	typedef TArray<uint16, TFixedAllocator<MAX_TOTAL_INFLUENCES>>         InfluenceArray;
	typedef TPair<FBoneIndexType, uint16>                                 BoneInfluence;

	SkinBoneArray V0BoneIndices(V0SkinWeightInfo.InfluenceBones, MAX_TOTAL_INFLUENCES);
	InfluenceArray V0InfluenceWeights(V0SkinWeightInfo.InfluenceWeights, MAX_TOTAL_INFLUENCES);
	SkinBoneArray V1BoneIndices(V1SkinWeightInfo.InfluenceBones, MAX_TOTAL_INFLUENCES);
	InfluenceArray V1InfluenceWeights(V1SkinWeightInfo.InfluenceWeights, MAX_TOTAL_INFLUENCES);
	TSet<FBoneIndexType, DefaultKeyFuncs<FBoneIndexType>, TFixedSetAllocator<32>> JointIndices;

	//that allocator is worst case scenario, most likely will be <= MAX_TOTAL_INFLUENCES
	TArray<BoneInfluence, TFixedAllocator<MAX_TOTAL_INFLUENCES * 2>> OutInfluences;
	for (uint8 Index = 0; Index < MAX_TOTAL_INFLUENCES; ++Index)
	{
		JointIndices.Add(V0BoneIndices[Index]);
		JointIndices.Add(V1BoneIndices[Index]);
	}

	// todo: Check if clamping is actually necessary below
	for (FBoneIndexType BoneIndex : JointIndices)
	{
		int32 V0InfluenceIndex = V0BoneIndices.Find(BoneIndex);
		int32 V1InfluenceIndex = V1BoneIndices.Find(BoneIndex);

		if (V0InfluenceIndex != INDEX_NONE && V1InfluenceIndex != INDEX_NONE)
		{
			//Both vertices are influenced by this bone
			FBoneIndexType CommonBoneIndex = V0BoneIndices[V0InfluenceIndex];

			//arbitrary, we could use V1 indices with V1 index, too
			float  V0Influence     = (float)V0InfluenceWeights[V0InfluenceIndex];
			float  V1Influence     = (float)V1InfluenceWeights[V1InfluenceIndex];
			uint16 InterpInfluence = FMath::Clamp(FMath::TruncToInt(FMath::Lerp(V0Influence, V1Influence, Alpha)),
			                                      0,
			                                      UINT16_MAX);
			OutInfluences.Add({CommonBoneIndex, InterpInfluence});

		}
		else
		{
			if (V0InfluenceIndex != INDEX_NONE)
			{
				uint16 InterpInfluence = FMath::Clamp(
					FMath::TruncToInt(FMath::Lerp(float(V0InfluenceWeights[V0InfluenceIndex]), 0.0f, Alpha)),
					0,
					UINT16_MAX);
				FBoneIndexType V0BoneIndex = V0BoneIndices[V0InfluenceIndex];
				OutInfluences.Add({V0BoneIndex, InterpInfluence});
			}
			if (V1InfluenceIndex != INDEX_NONE)
			{
				uint16 InterpInfluence = FMath::Clamp(
					FMath::TruncToInt(FMath::Lerp(0.0f, float(V1InfluenceWeights[V1InfluenceIndex]), Alpha)),
					0,
					UINT16_MAX);
				FBoneIndexType V1BoneIndex = V1BoneIndices[V1InfluenceIndex];
				OutInfluences.Add({V1BoneIndex, InterpInfluence});
			}
		}
	}
	// Now we sort and resize the OutInfluences to only contain the MAX_TOTAL_INFLUENCES amount of elements with the most significant influences
	OutInfluences.Sort([](const TPair<FBoneIndexType, uint16> &Elem1,
	                      const TPair<FBoneIndexType, uint16> &Elem2) -> bool
	{
		return Elem1.Value > Elem2.Value;
	});
	OutInfluences.SetNumZeroed(MAX_TOTAL_INFLUENCES);

	uint16 TotalInfluenceSum = 0;
	for (const auto &Influence : OutInfluences)
	{
		TotalInfluenceSum += Influence.Value;
	}
	// If the influences don't sum up to the max influence, which is 65535 (Max uint16 value), we need to give them a little boost
	if (TotalInfluenceSum != UINT16_MAX)
	{
		float BoostValue  = TotalInfluenceSum / float(UINT16_MAX);
		TotalInfluenceSum = 0; //Reusing
		for (auto &Pair : OutInfluences)
		{
			Pair.Value = FMath::RoundToInt(float(Pair.Value) / BoostValue);
			TotalInfluenceSum += Pair.Value;
		}
		// If the total influence sum is still not 65535, we boost the biggest influence, based on my testing it will only be a small boost like +1
		// Though I haven't proven it mathematically, it seems that the sum will never overflow, so we're safe here and don't need to account for that
		if (TotalInfluenceSum != UINT16_MAX)
		{
			const uint16 Difference = UINT16_MAX - TotalInfluenceSum;
			// Out influences are sorted so the biggest influence is in the 0th index
			OutInfluences[0].Value += Difference;
		}
	}

	for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
	{
		OutSkinWeightInfo.InfluenceBones[InfluenceIndex]   = OutInfluences[InfluenceIndex].Key;
		OutSkinWeightInfo.InfluenceWeights[InfluenceIndex] = OutInfluences[InfluenceIndex].Value;
	}
}

inline void SkinVertex(FVector3f &                      InOutPosition,
                       FVector3f &                      InOutNormal,
                       const FSkinWeightInfo &          SkinWeights,
                       TArrayView<const FMatrix44f>     SkinningMatrices,
                       TArrayView<const FBoneIndexType> BoneMap)
{
	FVector3f SkinnedPosition = {0, 0, 0};
	FVector3f SkinnedNormal   = {0, 0, 0};
	for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
	{
		if (SkinWeights.InfluenceWeights[InfluenceIdx] == 0)
		{
			break;
		}

		const float          Weight = StaticCast<float>(SkinWeights.InfluenceWeights[InfluenceIdx]) / UINT16_MAX;
		const FBoneIndexType InfluenceBoneIdx = SkinWeights.InfluenceBones[InfluenceIdx];
		const FBoneIndexType SkeletonBoneIdx = BoneMap[InfluenceBoneIdx];

		SkinnedPosition += Weight * SkinningMatrices[SkeletonBoneIdx].TransformPosition(InOutPosition);
		SkinnedNormal += Weight * SkinningMatrices[SkeletonBoneIdx].TransformVector(InOutNormal);
	}

	InOutPosition = SkinnedPosition;
	InOutNormal   = SkinnedNormal;
}

inline void ReverseSkinning(FVector3f &                      InOutPosition,
                            FVector3f &                      InOutNormal,
                            const FSkinWeightInfo &          SkinWeights,
                            TArrayView<const FMatrix44f>     ReverseSkinningMatrices,
                            TArrayView<const FBoneIndexType> BoneMap)
{
	FVector3f UnskinnedPosition = {0, 0, 0};
	FVector3f UnskinnedNormal   = {0, 0, 0};
	for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
	{
		if (SkinWeights.InfluenceWeights[InfluenceIdx] == 0)
		{
			break;
		}

		const float          Weight = StaticCast<float>(SkinWeights.InfluenceWeights[InfluenceIdx]) / UINT16_MAX;
		const FBoneIndexType InfluenceBoneIdx = SkinWeights.InfluenceBones[InfluenceIdx];
		const FBoneIndexType SkeletonBoneIdx = BoneMap[InfluenceBoneIdx];

		UnskinnedPosition += Weight * ReverseSkinningMatrices[SkeletonBoneIdx].TransformPosition(InOutPosition);
		UnskinnedNormal += Weight * ReverseSkinningMatrices[SkeletonBoneIdx].TransformVector(InOutNormal);
	}

	InOutPosition = UnskinnedPosition;
	InOutNormal   = UnskinnedNormal;
}

static void SkinEdges(TArray<FUtilSkinnedEdge3D> &     Edges,
                      TArrayView<const FMatrix44f>     SkinningMatrices,
                      TArrayView<const FBoneIndexType> BoneMap)
{
	for (FUtilSkinnedEdge3D &Edge : Edges)
	{
		FVector3f Dummy{0, 0, 0};
		SkinVertex(Edge.V0, Dummy, Edge.V0SkinWeights, SkinningMatrices, BoneMap);
		SkinVertex(Edge.V1, Dummy, Edge.V1SkinWeights, SkinningMatrices, BoneMap);
	}
}


static void ProjectEdges(TArray<FUtilSkinnedEdge2D> &      Out2DEdges,
                         FMatrix &                         ToWorld,
                         const TArray<FUtilSkinnedEdge3D> &In3DEdges,
                         const FPlane &                    InPlane)
{
	// Build matrix to transform verts into plane space
	FVector BasisX, BasisY, BasisZ;
	BasisZ = InPlane;
	BasisZ.FindBestAxisVectors(BasisX, BasisY);
	ToWorld = FMatrix(BasisX, BasisY, InPlane, BasisZ * InPlane.W);

	Out2DEdges.SetNumUninitialized(In3DEdges.Num());
	for (int32 EdgeIdx = 0; EdgeIdx < In3DEdges.Num(); ++EdgeIdx)
	{
		FVector P                         = ToWorld.InverseTransformPosition(FVector(In3DEdges[EdgeIdx].V0));
		Out2DEdges[EdgeIdx].V0.X          = P.X;
		Out2DEdges[EdgeIdx].V0.Y          = P.Y;
		Out2DEdges[EdgeIdx].V0Z           = P.Z;
		Out2DEdges[EdgeIdx].V0SkinWeights = In3DEdges[EdgeIdx].V0SkinWeights;

		P                                 = ToWorld.InverseTransformPosition(FVector(In3DEdges[EdgeIdx].V1));
		Out2DEdges[EdgeIdx].V1.X          = P.X;
		Out2DEdges[EdgeIdx].V1.Y          = P.Y;
		Out2DEdges[EdgeIdx].V1Z           = P.Z;
		Out2DEdges[EdgeIdx].V1SkinWeights = In3DEdges[EdgeIdx].V1SkinWeights;
	}
}

static void FixEdgeHoles(TArray<FUtilSkinnedEdge2D> &Edges)
{
	TMap<FEdgeVert, TSet<FEdgeVert>, TInlineSetAllocator<4>> NeighborEdges;

	for (FUtilSkinnedEdge2D &Edge : Edges)
	{
		FEdgeVert V0{Edge.V0, Edge.V0Z, Edge.V0SkinWeights};
		FEdgeVert V1{Edge.V1, Edge.V1Z, Edge.V1SkinWeights};

		NeighborEdges.FindOrAdd(V0).Add(V1);
		NeighborEdges.FindOrAdd(V1).Add(V0);

	}

	// Edges.Reset();

	for (auto &Edge : NeighborEdges)
	{

		if (Edge.Value.Num() >= 2)
		{
			continue;
		}

		// Find the closest single edge vertex (if it exists)
		const FVector2f *CurrentVertex            = &Edge.Key.V;
		float           ClosestSingleDistSquared = UE_DOUBLE_BIG_NUMBER;
		float           ClosestDistSquared       = UE_DOUBLE_BIG_NUMBER;
		FEdgeVert *      ClosestVertex            = nullptr;
		FEdgeVert *      ClosestSingleVertex      = nullptr;
		for (auto &OtherEdge : NeighborEdges)
		{
			float DistSquared        = FVector2f::DistSquared(*CurrentVertex, OtherEdge.Key.V);
			bool   bIsCloser          = DistSquared < ClosestDistSquared;
			bool   bIsCloserSingle    = DistSquared < ClosestSingleDistSquared;
			bool   bIsTheSame         = OtherEdge.Key == Edge.Key;
			bool   bIsAlreadyNeighbor = Edge.Value.Contains(OtherEdge.Key);
			if (bIsTheSame == false && bIsAlreadyNeighbor == false)
			{
				if (bIsCloser)
				{
					ClosestDistSquared = DistSquared;
					ClosestVertex      = &OtherEdge.Key;
				}
				bool bIsSingle = OtherEdge.Value.Num() < 2;
				if (bIsSingle && bIsCloserSingle)
				{
					ClosestSingleDistSquared = DistSquared;
					ClosestSingleVertex      = &OtherEdge.Key;
				}
			}

		}

		FEdgeVert *ChosenVert = nullptr;
		if (ClosestSingleVertex)
		{
			Edge.Value.Add(*ClosestSingleVertex);
			NeighborEdges.Find(*ClosestSingleVertex)->Add(*ClosestSingleVertex);
			ChosenVert = ClosestSingleVertex;
		}
		/* Last resort */
		else if (ClosestVertex)
		{
			Edge.Value.Add(*ClosestVertex);
			NeighborEdges.Find(*ClosestVertex)->Add(*ClosestVertex);
			ChosenVert = ClosestVertex;
		}

		if (ChosenVert == nullptr)
		{
			// Somehow we failed to find any suitable vertices
			UE_LOG(LogTemp,
			       Error,
			       TEXT("%s : Failed to find a suitable vertex to fix the edge hole"),
			       TEXT(__FUNCTION__));
			continue;
		}
		FUtilSkinnedEdge2D NewEdge;
		NewEdge.V0 = Edge.Key.V;
		NewEdge.V1 = ChosenVert->V;

		NewEdge.V0Z = Edge.Key.ZComponent;
		NewEdge.V1Z = ChosenVert->ZComponent;

		NewEdge.V0SkinWeights = Edge.Key.SkinWeights;
		NewEdge.V1SkinWeights = ChosenVert->SkinWeights;

		Edges.Add(NewEdge);
	}
}

static void DelaunayTriangulate(TArray<FUtilSkinnedEdge2D> &InEdges,
                         TArray<uint32> &            OutIndices,
                         TArray<CDT::V2d<SLICER_DEFAULT_COORD_TYPE>> &  OutVertices)
{
	const auto Start = FPlatformTime::Seconds();
	if (InEdges.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Empty array of edges supplied to triangulator, returning"));
		return;
	}
	std::vector<CDT::V2d<SLICER_DEFAULT_COORD_TYPE>> Points;
	std::vector<CDT::Edge>        Edges;

	Points.reserve(InEdges.Num() * 2);
	Edges.reserve(InEdges.Num());

	FString PointsStr = TEXT("Points: {");
	FString EdgesStr = TEXT("Edges: ");
	for (int32 EdgeIdx = 0; EdgeIdx < InEdges.Num(); ++EdgeIdx)
	{
		const FUtilSkinnedEdge2D &Edge      = InEdges[EdgeIdx];
		const int32               EdgeV0Idx = Points.size();
		Points.emplace_back(CDT::V2d<SLICER_DEFAULT_COORD_TYPE>::make(FMath::RoundToFloat(Edge.V0.X * 10000)/10000, FMath::RoundToFloat(Edge.V0.Y*10000)/10000, Edge.V0Z, Edge.V0SkinWeights));
		const int32 EdgeV1Idx = Points.size();
		Points.emplace_back(CDT::V2d<SLICER_DEFAULT_COORD_TYPE>::make(FMath::RoundToFloat(Edge.V1.X*10000)/10000, FMath::RoundToFloat(Edge.V1.Y*10000)/10000, Edge.V1Z, Edge.V1SkinWeights));
		Edges.emplace_back(CDT::Edge(EdgeV0Idx, EdgeV1Idx));
		
		PointsStr += FString::Printf(TEXT("{%.9g,%.9g},"), Edge.V0.X, Edge.V0.Y);
		PointsStr += FString::Printf(TEXT("{%.9g,%.9g},"), Edge.V1.X, Edge.V1.Y);
		EdgesStr += FString::Printf(TEXT("%d,%d,"), EdgeV0Idx, EdgeV1Idx);
	}
		bool bDebugBool = false;
		if(bDebugBool)
		{
			PointsStr += TEXT("}");
			UE_LOG(LogTemp, Warning, TEXT("%s || %s"), *PointsStr, *EdgesStr);
		}

	try
	{

		CDT::Triangulation<SLICER_DEFAULT_COORD_TYPE> CDT(CDT::VertexInsertionOrder::Auto,
		                               CDT::IntersectingConstraintEdges::TryResolve,
		                               CDT::detail::defaults::minDistToConstraintEdge);
		const CDT::DuplicatesInfo DuplicatesInfo = CDT::RemoveDuplicatesAndRemapEdges(Points, Edges);
		CDT.insertVertices(Points);
		CDT.insertEdges(Edges);
		CDT.eraseOuterTriangles();
		if (CDT.triangles.empty())
		{
			UE_LOG(LogTemp, Error, TEXT("Triangulation failed - drawing debug vertices"));

			// if(GEngine)
			// GEngine->AddOnScreenDebugMessage(INDEX_NONE, 60.f, FColor::Red, TEXT("Triangulation failed miserably"));

			if(GWorld)
			{
				// for(const auto& Vertex : CDT.vertices)
				// {
					// UKismetSystemLibrary::DrawDebugPoint(GWorld, FVector{Vertex.x, Vertex.y, Vertex.z}, 5.f, FLinearColor::Red, 60.f);
				// }
			}
			
			return;
		}


		UE_LOG(LogTemp,
		       Display,
		       TEXT("%s : Triangulation resulted in %llu triangles"),
		       *FString(__FUNCTION__),
		       CDT.triangles.size())

		if (CDT.vertices.empty() == false)
		{
			OutVertices.Append(CDT.vertices.data(), CDT.vertices.size());
		}

		OutIndices.Empty();
		// OutIndices.Reserve(CDT.triangles.size() * 3);
		UE_LOG(LogTemp, Display, TEXT("VertCount %llu"), CDT.vertices.size());
		for (const CDT::Triangle &Triangle : CDT.triangles)
		{
			OutIndices.Add(Triangle.vertices[0]);
			OutIndices.Add(Triangle.vertices[1]);
			OutIndices.Add(Triangle.vertices[2]);
		}


		if (CDT.vertices.size() != Points.size())
		{
			UE_LOG(LogTemp, Warning, TEXT("Triangulator changed the vertex count"));
		}
	}

	catch (std::exception &e)
	{
		FString what = e.what();
		UE_LOG(LogTemp, Error, TEXT("%s"), *what);
	}

	const auto End = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("Constrained Delaunay Triangulation took %f seconds"), End - Start);
}

template <typename T>
static void ConvertV2DsTo3D(const TArray<CDT::V2d<T>> &     V2Ds,
                            const FMatrix &                 InMatrix,
                            TArray<FStaticMeshBuildVertex> &OutVertices,
                            TArray<FSkinWeightInfo> &       OutWeights)
{
	OutVertices.Empty();
	OutWeights.Empty();

	OutVertices.SetNumUninitialized(V2Ds.Num());
	OutWeights.SetNumUninitialized(V2Ds.Num());

	FVector3f PolyNormal   = (FVector3f)-InMatrix.GetUnitAxis(EAxis::Z);
	FVector3f PolyTangentX = (FVector3f)InMatrix.GetUnitAxis(EAxis::X);
	FVector3f PolyTangentY = (FVector3f)InMatrix.GetUnitAxis(EAxis::Y);

	for (int32 VertexIdx = 0; VertexIdx < V2Ds.Num(); ++VertexIdx)
	{
		const CDT::V2d<T> &     V2d    = V2Ds[VertexIdx];
		FStaticMeshBuildVertex &Vertex = OutVertices[VertexIdx];
		Vertex.Position                = FVector3f(FVector(InMatrix.TransformPosition({V2d.x, V2d.y, V2d.z})));
		Vertex.Color                   = FColor(255, 255, 255, 255); //dummy color, maybe later I'll add color support
		Vertex.TangentX                = PolyTangentX;
		Vertex.TangentY                = PolyTangentY;
		Vertex.TangentZ                = PolyNormal;
		FMemory::Memzero(Vertex.UVs);
		Vertex.UVs[0].X       = V2d.x / 64.f;
		Vertex.UVs[0].Y       = V2d.y / 64.f;
		OutWeights[VertexIdx] = V2d.SkinWeights;
	}
}

/** Util to slice a convex hull with a plane */
static void SliceConvexElem(const FKConvexElem &InConvex,
                            const FPlane &      SlicePlane,
                            TArray<FVector> &   OutAboveConvexVerts,
                            TArray<FVector> &   OutBelowConvexVerts)
{
	// Get set of planes that make up hull
	TArray<FPlane> ConvexPlanes;
	TArray<float>  Distances;
	Distances.SetNumZeroed(InConvex.VertexData.Num());

	OutAboveConvexVerts.Empty();
	OutBelowConvexVerts.Empty();

	InConvex.GetPlanes(ConvexPlanes);

	if (ConvexPlanes.Num() >= 4)
	{
		// Add on the slicing plane (need to flip as it culls geom in the opposite sense to Epic's geom culling code)
		ConvexPlanes.Add(SlicePlane.Flip());

		// Create output convex based on new set of planes
		FKConvexElem SlicedElem;
		bool         bSuccess = SlicedElem.HullFromPlanes(ConvexPlanes, InConvex.VertexData);
		if (bSuccess)
		{
			OutAboveConvexVerts = SlicedElem.VertexData;
		}

		SlicedElem.Reset();
		ConvexPlanes.RemoveAt(ConvexPlanes.Num() - 1, EAllowShrinking::No);
		ConvexPlanes.Add(SlicePlane);

		bSuccess = SlicedElem.HullFromPlanes(ConvexPlanes, InConvex.VertexData);
		if (bSuccess)
		{
			OutBelowConvexVerts = SlicedElem.VertexData;
		}
	}
}

// Sets default values for this component's properties
USkeletalMeshSlicerComponent::USkeletalMeshSlicerComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
	bIsCurrentlySlicing               = false;
	CurrentSkeletalMesh               = nullptr;
	CurrentPhysicsAsset               = nullptr;
	GibAnimInstance                   = nullptr;
	// ...
}


// Called when the game starts
void USkeletalMeshSlicerComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

}

void USkeletalMeshSlicerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	ReturnPooledAssets();
}

void USkeletalMeshSlicerComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}


// Called every frame
void USkeletalMeshSlicerComponent::TickComponent(float                        DeltaTime,
                                                 ELevelTick                   TickType,
                                                 FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

void USkeletalMeshSlicerComponent::SliceMesh(UMeshComponent *    MeshComponent,
                                             const FVector       SlicePoint,
                                             const FVector       SliceNormal,
                                             UMaterialInterface *CapMaterial,
                                             const FName         BoneName)
{
	//testing asan :D
	
	if (bIsCurrentlySlicing == true)
	{
		return;
	}
	USkeletalMeshComponent *InSkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent);
	if (InSkeletalMeshComponent == nullptr)
	{
		return;
	}

	if (false)
	{
		int32 *array = new int32[3];
		delete[] array;

		// __asan_poison_memory_region(array, sizeof(int32) * 3);

		array[3] = 1337;
		UE_LOG(LogTemp, Display, TEXT("Asan please work %d"), array[3]);
	}

	bIsCurrentlySlicing = true;
	TArray<TArray<int32>> BoneVertexMap;
	// WARNING: The bone indexes correspond to RefSkeleton ones, not the ones in SkinWeightInfos
	TArray<FBoneIndexType> MeshToRefSkelBoneMap; // Will be useful for skinning
	BoneVertexMap.SetNum(InSkeletalMeshComponent->GetNumBones());
	MeshToRefSkelBoneMap.SetNum(InSkeletalMeshComponent->GetNumBones());

	if(InSkeletalMeshComponent->GetSkeletalMeshRenderData() == nullptr || InSkeletalMeshComponent->GetSkeletalMeshRenderData()->LODRenderData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Supplied mesh has no LOD render data"), *FString(__FUNCTION__));
		bIsCurrentlySlicing = false;
		return;
	}
	const FSkeletalMeshLODRenderData &RenderData = InSkeletalMeshComponent->GetSkeletalMeshRenderData()->LODRenderData[0];

	TArray<FSkinWeightInfo> SkinWeightInfos;
	RenderData.SkinWeightVertexBuffer.GetSkinWeights(SkinWeightInfos);

	for (int32 SectionIdx = 0; SectionIdx < RenderData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection &Section = RenderData.RenderSections[SectionIdx];
		for (int32 SectionVertexIdx = 0; SectionVertexIdx < Section.GetNumVertices() && SectionVertexIdx <
		     SkinWeightInfos.Num(); ++SectionVertexIdx)
		{
			const int32          GlobalVertexIdx = SectionVertexIdx + Section.BaseVertexIndex;
			const FBoneIndexType MeshBoneIdx     = SkinWeightInfos[GlobalVertexIdx].InfluenceBones[0];
			check(MeshBoneIdx < Section.BoneMap.Num());
			const int32 RefSkeletonBoneIdx = Section.BoneMap[MeshBoneIdx];
			BoneVertexMap[RefSkeletonBoneIdx].Add(GlobalVertexIdx);
			MeshToRefSkelBoneMap[MeshBoneIdx] = RefSkeletonBoneIdx;
		}
	}

	auto SlicingData                           = MakeShared<FSlicingData>();
	SlicingData->bShouldTaskAbort              = false;
	SlicingData->OriginalSkeletalMeshComponent = InSkeletalMeshComponent;
	SlicingData->WorldSlicePlanePos            = SlicePoint;
	SlicingData->WorldSlicePlaneNormal         = SliceNormal;
	SlicingData->CapMaterial                   = CapMaterial;
	SlicingData->ComponentSpaceTransforms      = InSkeletalMeshComponent->GetComponentSpaceTransforms();
	SlicingData->ComponentToWorldTransform     = InSkeletalMeshComponent->GetComponentToWorld();
	SlicingData->BoneVertexMap                 = MoveTemp(BoneVertexMap);
	SlicingData->MeshToRefSkelBoneMap          = MoveTemp(MeshToRefSkelBoneMap);
	SlicingData->OriginalSkinWeights           = MoveTemp(SkinWeightInfos);

	const bool bSuccess = DivideMeshIntoTwoSections(InSkeletalMeshComponent,
	                          SlicePoint,
	                          SliceNormal,
	                          BoneName,
	                          SlicingData->BoneVertexMap,
	                          SlicingData->BonesBelow,
	                          SlicingData->BonesAbove,
	                          SlicingData->SlicedBones);

	//If no bones were sliced, nothing should happen
	if (SlicingData->SlicedBones.IsEmpty() || bSuccess == false)
	{
		bIsCurrentlySlicing = false;
		return;
	}

	USkeletalMeshPoolSubsystem *MeshPool = GetWorld()->GetSubsystem<USkeletalMeshPoolSubsystem>();
	if(MeshPool == nullptr)
	{
		bIsCurrentlySlicing = false;
		return;
	}

	SlicingData->AboveSkeletalMesh = MeshPool->GetPooledSkeletalMesh();
	SlicingData->AbovePhysicsAsset = MeshPool->GetPooledPhysicsAsset();
	SlicingData->BelowSkeletalMesh = MeshPool->GetPooledSkeletalMesh();
	SlicingData->BelowPhysicsAsset = MeshPool->GetPooledPhysicsAsset();
	if (SlicingData->AboveSkeletalMesh == nullptr || SlicingData->AbovePhysicsAsset == nullptr || SlicingData->
		BelowSkeletalMesh == nullptr || SlicingData->BelowPhysicsAsset == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s : Acquisition of Pool Items failed"), TEXT(__FUNCTION__));
		bIsCurrentlySlicing = false;
		return;
	}
	
	SlicingData->SlicingStartTime = FPlatformTime::Seconds();

	FGraphEventRef PerVertexTask = FFunctionGraphTask::CreateAndDispatchWhenReady([SlicingData, this]()
		{
			TASKUSAGE_PerformVertexLevelMeshDivision(SlicingData);
		},
		TStatId{},
		nullptr,
		ENamedThreads::AnyBackgroundThreadNormalTask);

	FGraphEventRef AssetFillTask = FFunctionGraphTask::CreateAndDispatchWhenReady([SlicingData, this]()
		{
			TASKUSAGE_FillNewAssets(SlicingData);
		},
		TStatId{},
		PerVertexTask,
		ENamedThreads::AnyBackgroundThreadNormalTask);

	FGraphEventRef FinalizeSliceTask = FFunctionGraphTask::CreateAndDispatchWhenReady([SlicingData, this]()
		{
			TASKUSAGE_FinalizeNewAssetsCreation(SlicingData);
		},
		TStatId{},
		AssetFillTask,
		ENamedThreads::GameThread);
}

void USkeletalMeshSlicerComponent::DEBUG_Triangulate()
{
	using namespace CDT;
	std::vector<V2d<SLICER_DEFAULT_COORD_TYPE>> OtherPoints = {};
	//std::vector<V2d<SLICER_DEFAULT_COORD_TYPE>> Points = {};
	std::vector<V2d<SLICER_DEFAULT_COORD_TYPE>> Points = {};
	for(int32 i = 0; i < Points.size() && i < OtherPoints.size(); ++i)
	{
		if(Points[i] != OtherPoints[i])
		{
			//UE_LOG(LogTemp, Error, TEXT("Points at index %d different -> {%.9g, %.9g}, {%.9g, %.9g}"),i, Points[i].x, Points[i].y, OtherPoints[i].x, OtherPoints[i].y);
		}
	}
	std::vector<Edge> Edges;
														  
	for (std::size_t i = 0; i < Points.size(); i++)
	{
		Edges.emplace_back(Edge(i, i + 1));
		++i;
	}
		Triangulation<SLICER_DEFAULT_COORD_TYPE> CDT(VertexInsertionOrder::Auto,
		                               IntersectingConstraintEdges::TryResolve,
		                               detail::defaults::minDistToConstraintEdge);
		const DuplicatesInfo DuplicatesInfo = RemoveDuplicatesAndRemapEdges(Points, Edges);
		CDT.insertVertices(Points);
		CDT.insertEdges(Edges);
		// CDT.eraseSuperTriangle();
		CDT.eraseOuterTriangles();

	for(const auto& Triangle : CDT.triangles)
	{
		const auto& V1Idx = Triangle.vertices[0];
		const auto& V2Idx = Triangle.vertices[1];
		const auto& V3Idx = Triangle.vertices[2];

		const auto& V1 = CDT.vertices[V1Idx];
		const auto& V2 = CDT.vertices[V2Idx];
		const auto& V3 = CDT.vertices[V3Idx];
		
		// UKismetSystemLibrary::DrawDebugLine(GetWorld(), FVector(V1.x, V1.y, 0), FVector(V2.x, V2.y, 0), FColor::Red, 120.f, 0.06f);
		// UKismetSystemLibrary::DrawDebugLine(GetWorld(), FVector(V2.x, V2.y, 0), FVector(V3.x, V3.y, 0), FColor::Red, 120.f, 0.06f);
		// UKismetSystemLibrary::DrawDebugLine(GetWorld(), FVector(V3.x, V3.y, 0), FVector(V1.x, V1.y, 0), FColor::Red, 120.f, 0.06f);
	}
}

bool USkeletalMeshSlicerComponent::DivideMeshIntoTwoSections(const USkeletalMeshComponent *HitSkeletalMeshComponent,
                                                             const FVector &               PlanePosition,
                                                             const FVector &               PlaneNormal,
                                                             const FName                   HitBoneName,
                                                             const TArray<TArray<int32>> & BoneVertexMap,
                                                             TArray<int32> &               BonesBelowPlane,
                                                             TArray<int32> &               BonesAbovePlane,
                                                             TArray<int32> &               SlicedBones)
{
	if(HitBoneName == NAME_None)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Attempting to slice at NAME_None Bone"), TEXT(__FUNCTION__));
		return false;
	}
	if(HitSkeletalMeshComponent == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Mesh is nullptr"), TEXT(__FUNCTION__));
		return false;
	}
	//todo: make this a set maybe, so lookups are faster
	BonesBelowPlane.Empty();
	BonesAbovePlane.Empty();
	SlicedBones.Empty();

	TSet<FName>      SlicedBoneSet;
	TArray<FName>    SliceBoneStack;
	TArray<FVector>  TracePointsStack;
	TArray<AActor *> IgnoreActors;

	SlicedBoneSet.Add(HitBoneName);
	SliceBoneStack.Add(HitBoneName);
	TracePointsStack.Add(PlanePosition);


	const FReferenceSkeleton &ReferenceSkeleton = HitSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
	TArray<TArray<int32>>     BoneChildren;
	BoneChildren.SetNum(ReferenceSkeleton.GetNum());
	for (int32 BoneIdx = 1; BoneIdx < ReferenceSkeleton.GetNum(); ++BoneIdx)
	{
		BoneChildren[ReferenceSkeleton.GetParentIndex(BoneIdx)].Add(BoneIdx);
	}


	TArray<FHitResult> OutHits; //reusable
	TArray<FName>      ChildBoneNames;

	FPlane Plane(PlanePosition, PlaneNormal.GetSafeNormal());

	constexpr int32 HorizontalThreshold = 45.0f;

	TMap<FName, FVector> TraceBoxes;
	//todo: add validation here in case there is no physics asset
	const UPhysicsAsset *PhysicsAsset = HitSkeletalMeshComponent->GetPhysicsAsset();
	if(PhysicsAsset == nullptr)
	{
		return false;
	}

	if(HitSkeletalMeshComponent->GetSkeletalMeshRenderData() == nullptr || HitSkeletalMeshComponent->GetSkeletalMeshRenderData()->LODRenderData.IsEmpty())
	{
		return false;
	}
	
	const FSkeletalMeshLODRenderData& RenderData = HitSkeletalMeshComponent->GetSkeletalMeshRenderData()->LODRenderData[0];
	
	for (const USkeletalBodySetup *BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if(BodySetup == nullptr || BodySetup->IsValidLowLevelFast() == false)
		{
			//something must be terribly wrong here, assuming it's even possible. The slicer taught me that it's better to be safe than sorry
			return false;
		}
		
		if (BodySetup->AggGeom.BoxElems.IsEmpty() == false)
		{
			const FKBoxElem &BoxElem = BodySetup->AggGeom.BoxElems[0]; // Assuming only one per bone
			TraceBoxes.Add(BodySetup->BoneName, FVector(BoxElem.X, BoxElem.Y, BoxElem.Z));
		}
		else
		{
			UE_LOG(LogTemp,
			       Warning,
			       TEXT("%s : Box elem for body %s not found"),
			       *FString(__FUNCTION__),
			       *BodySetup->BoneName.ToString())
		}
	}
	
	bool bDebugWasFirstCut = true;

	while (SliceBoneStack.IsEmpty() == false)
	{
		OutHits.Reset();
		const FName   BoneName   = SliceBoneStack.Pop();
		const FVector TracePoint = TracePointsStack.Pop();
		const int32   BoneIdx    = (BoneName != NAME_None) ? HitSkeletalMeshComponent->GetBoneIndex(BoneName) : INDEX_NONE;
		
		if(BoneChildren.IsValidIndex(BoneIdx) == false)
		{
			UE_LOG(LogTemp, Error, TEXT("%s : BoneIdx %d is not valid"), *FString(__FUNCTION__), BoneIdx)
			continue;
		}
		
		ChildBoneNames.SetNum(BoneChildren[BoneIdx].Num());

		for (int32 Idx = 0; const uint32 ChildBoneIdx : BoneChildren[BoneIdx])
		{
			ChildBoneNames[Idx] = HitSkeletalMeshComponent->GetBoneName(ChildBoneIdx);
			++Idx;
		}

		//check whether vertical or horizontal slice, this determines the next trace point's final location
		if(BoneVertexMap.IsValidIndex(BoneIdx) == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s : BoneIdx %d does not appear in BoneVertexMap"), TEXT(__FUNCTION__), BoneIdx);
			continue;
		}
		
		const FTransform BoneTransform = HitSkeletalMeshComponent->GetBoneTransform(BoneName);
		const FVector    BoneDirection = BoneTransform.GetRotation().RotateVector({1, 0, 0});
		const float      Dot           = FVector::DotProduct(BoneDirection, PlaneNormal);
		float            Denominator   = 0.0f;
		if (Dot < 0.0f)
		{
			Denominator = FVector::DotProduct(-BoneDirection, PlaneNormal);
		}
		else
		{
			Denominator = Dot;
		}

		FVector TracePosition;
		if (FMath::RadiansToDegrees(FMath::Acos(Denominator)) < HorizontalThreshold && false)
		{
			float t       = FVector::DotProduct(PlanePosition - BoneTransform.GetLocation(), PlaneNormal) / Denominator;
			TracePosition = BoneTransform.GetLocation() + (t * BoneDirection);
		}
		else
		{
			//seems like this is actually working quite well even without all the fuss involved with the whole projection etc.
			TracePosition = TracePoint;
		}

		//checking if the point is FOR SURE on the plane
		const float Distance = FVector::DotProduct(TracePosition - PlanePosition, PlaneNormal);
		if (FMath::Abs(Distance) > KINDA_SMALL_NUMBER)
		{
			TracePosition = TracePosition - (Distance * PlaneNormal);
		}

		if (TraceBoxes.Contains(BoneName) == false)
		{
			continue;
		}

		const FVector BoxExtents = TraceBoxes[BoneName];
		const float   Size       = FMath::Max3(BoxExtents.X, BoxExtents.Y, BoxExtents.Z);
		const FVector TraceSize(2.7f, Size, Size);
		UKismetSystemLibrary::BoxTraceMulti(GetWorld(),
		                                    TracePosition,
		                                    TracePosition,
		                                    TraceSize,
		                                    PlaneNormal.ToOrientationRotator(),
		                                    TraceTypeQuery1,
		                                    false,
		                                    IgnoreActors,
		                                    EDrawDebugTrace::None,
		                                    OutHits,
		                                    false,
		                                    FLinearColor::Red,
		                                    bDebugWasFirstCut ? FLinearColor::Blue : FLinearColor::Green);
		bDebugWasFirstCut = false;

		SlicedBones.Add(BoneIdx);
		for (const FHitResult &HitResult : OutHits)
		{
			if (HitResult.GetActor() != GetOwner() || Cast<USkeletalMeshComponent>(HitResult.Component.Get()) !=
				HitSkeletalMeshComponent) //todo: make it better? I think this is wildly inefficient
			{
				continue;
			}
			if (SlicedBoneSet.Contains(HitResult.BoneName) || HitResult.BoneName == NAME_None)
			{
				continue;
			}

			if (HitSkeletalMeshComponent->GetParentBone(BoneName) == HitResult.BoneName || ChildBoneNames.Contains(
				HitResult.BoneName))
			{
				SlicedBoneSet.Add(HitResult.BoneName);
				SliceBoneStack.Push(HitResult.BoneName);
				TracePointsStack.Push(HitResult.ImpactPoint);
			}
		}
	}
	//now divide the skeleton into two sections
	TArray<FName> NonSliceBoneStack;
	for (const FName &BoneName : SlicedBoneSet)
	{
		if(BoneName == NAME_None)
		{
			continue;
		}
		
		const int32          BoneIdx    = HitSkeletalMeshComponent->GetBoneIndex(BoneName);
		
		if(BoneIdx == INDEX_NONE)
		{
			continue;
		}
		
		const TArray<int32> &ChildBones = BoneChildren[BoneIdx];
		for (const int32 ChildBoneIdx : ChildBones)
		{
			if (SlicedBones.Contains(ChildBoneIdx))
			{
				continue;
			}

			const FName ChildBoneName = HitSkeletalMeshComponent->GetBoneName(ChildBoneIdx);
			if(ChildBoneName == NAME_None)
			{
				continue;
			}
			const float Distance      = Plane.PlaneDot(HitSkeletalMeshComponent->GetBoneLocation(ChildBoneName));
			if (Distance < 0) // Bone is below the plane
			{
				BonesBelowPlane.Add(ChildBoneIdx);
			}
			else if (Distance > 0)
			{
				BonesAbovePlane.Add(ChildBoneIdx);
				NonSliceBoneStack.Push(ChildBoneName);
			}
		}

		// Now check the parent bone

		if (BoneIdx != 0) //obviously, root bone has no parent
		{
			const FName ParentName = HitSkeletalMeshComponent->GetParentBone(BoneName);
			const int32 ParentIdx  = HitSkeletalMeshComponent->GetBoneIndex(ParentName);
			if (SlicedBones.Contains(ParentIdx) == false)
			{
				const float Distance = Plane.PlaneDot(HitSkeletalMeshComponent->GetBoneLocation(ParentName));
				if (Distance < 0)
				{
					BonesBelowPlane.Add(ParentIdx);
				}
				else if (Distance > 0)
				{
					BonesAbovePlane.Add(ParentIdx);
					NonSliceBoneStack.Push(ParentName);
				}
			}
		}
	}

	while (NonSliceBoneStack.IsEmpty() == false)
	{
		const FName          BoneName   = NonSliceBoneStack.Pop();
		const int32          BoneIdx    = (BoneName != NAME_None) ? HitSkeletalMeshComponent->GetBoneIndex(BoneName) : INDEX_NONE;
		if(BoneIdx == INDEX_NONE)
		{
			continue;
		}
		const TArray<int32> &ChildBones = BoneChildren[BoneIdx];
		for (const int32 ChildBoneIdx : ChildBones)
		{
			const bool bIsNotSliced          = SlicedBones.Contains(ChildBoneIdx) == false;
			const bool bIsNotDeterminedAbove = BonesAbovePlane.Contains(ChildBoneIdx) == false;
			const bool bIsNotDeterminedBelow = BonesBelowPlane.Contains(ChildBoneIdx) == false;
			if (bIsNotSliced && bIsNotDeterminedAbove && bIsNotDeterminedBelow)
			{
				const FName ChildBoneName = HitSkeletalMeshComponent->GetBoneName(ChildBoneIdx);
				NonSliceBoneStack.Push(ChildBoneName);
				BonesAbovePlane.Add(ChildBoneIdx);
			}
		}
		if (BoneIdx != 0)
		{
			const FName ParentName            = HitSkeletalMeshComponent->GetParentBone(BoneName);
			const int32 ParentIdx             = HitSkeletalMeshComponent->GetBoneIndex(ParentName);
			const bool  bIsNotSliced          = SlicedBones.Contains(ParentIdx) == false;
			const bool  bIsNotDeterminedAbove = BonesAbovePlane.Contains(ParentIdx) == false;
			const bool  bIsNotDeterminedBelow = BonesBelowPlane.Contains(ParentIdx) == false;
			if (bIsNotSliced && bIsNotDeterminedAbove && bIsNotDeterminedBelow)
			{
				NonSliceBoneStack.Push(ParentName);
				BonesAbovePlane.Add(ParentIdx);
			}
		}
	}

	for (int32 BoneIdx = 0; BoneIdx < ReferenceSkeleton.GetNum(); ++BoneIdx)
	{
		if (SlicedBones.Contains(BoneIdx) == false && BonesAbovePlane.Contains(BoneIdx) == false)
		{
			BonesBelowPlane.AddUnique(BoneIdx);
		}
	}

	return true;
}

void USkeletalMeshSlicerComponent::TASKUSAGE_PerformVertexLevelMeshDivision(TSharedRef<FSlicingData> SlicingData)
{
	if (SlicingData->bShouldTaskAbort)
	{
		return;
	}

	SlicingData->LocalSlicePlanePos = SlicingData->ComponentToWorldTransform.InverseTransformPosition(
		SlicingData->WorldSlicePlanePos);
	SlicingData->LocalSlicePlaneNormal = SlicingData->ComponentToWorldTransform.InverseTransformVectorNoScale(
		SlicingData->WorldSlicePlaneNormal);

	FPlane SlicePlane(SlicingData->LocalSlicePlanePos, SlicingData->LocalSlicePlaneNormal);

	const FSkeletalMeshLODRenderData &RenderData = SlicingData->OriginalSkeletalMeshComponent->
	                                                            GetSkeletalMeshRenderData()->LODRenderData[0];

	TMap<int32, float> SlicedDistances;
	TArray<int32>      SlicedVertices;
	
	// Just in case
	SlicingData->SlicedBones.RemoveAll([&SlicingData](int32 BoneIdx)
	{
		return BoneIdx == INDEX_NONE || SlicingData->BoneVertexMap.IsValidIndex(BoneIdx) == false;
	});
	
	for (const int32 BoneIdx : SlicingData->SlicedBones)
	{
		SlicedVertices.Append(SlicingData->BoneVertexMap[BoneIdx]);
	}

	if (SlicedVertices.IsEmpty())
	{
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	const TArray<FMatrix44f> &BindPoseTransforms = SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->
	                                                      GetRefBasesInvMatrix();
	if (SlicingData->ComponentSpaceTransforms.Num() != BindPoseTransforms.Num())
	{
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	// Calculate skinning matrices at the moment the slice initially occured
	TArray<FMatrix44f> SkinningMatrices;
	SkinningMatrices.SetNumUninitialized(BindPoseTransforms.Num());
	for (int32 BoneIdx = 0; BoneIdx < BindPoseTransforms.Num(); ++BoneIdx)
	{
		SkinningMatrices[BoneIdx] = BindPoseTransforms[BoneIdx] * (FMatrix44f)SlicingData->ComponentSpaceTransforms[
			BoneIdx].ToMatrixWithScale();
	}

	TMap<int32, int32>    BaseToAboveVertIndex;
	TMap<int32, int32>    BaseToBelowVertIndex;
	TArray<TArray<int32>> AbovePerSectionVertexIndices;
	TArray<TArray<int32>> BelowPerSectionVertexIndices;
	AbovePerSectionVertexIndices.SetNum(RenderData.RenderSections.Num());
	BelowPerSectionVertexIndices.SetNum(RenderData.RenderSections.Num());

	// Indexed by vertex index, the element is the render section number
	TArray<int32> VerticesToSectionMap;
	VerticesToSectionMap.SetNumUninitialized(RenderData.GetNumVertices());

	SlicingData->AboveRenderSectionBoneMaps.Empty();
	SlicingData->AboveRenderSectionBoneMaps.Reserve(RenderData.RenderSections.Num() + 1);
	SlicingData->BelowRenderSectionBoneMaps.Empty();
	SlicingData->BelowRenderSectionBoneMaps.Reserve(RenderData.RenderSections.Num() + 1);
	for (int32 SectionIdx = 0; SectionIdx < RenderData.RenderSections.Num(); ++SectionIdx)
	{
		const FSkelMeshRenderSection &RenderSection      = RenderData.RenderSections[SectionIdx];
		const int32                   BaseIdx            = RenderSection.BaseVertexIndex;
		const int32                   NumSectionVertices = RenderSection.NumVertices;
		for (int32 VertexIdx = BaseIdx; VertexIdx < BaseIdx + NumSectionVertices; ++VertexIdx)
		{
			VerticesToSectionMap[VertexIdx] = SectionIdx;
			SlicingData->AboveRenderSectionBoneMaps.Add(RenderSection.BoneMap);
			SlicingData->BelowRenderSectionBoneMaps.Add(RenderSection.BoneMap);
		}
	}

	TArray<FVector3f> SkinnedPositions;
	TArray<FVector3f> SkinnedNormals;
	SkinnedPositions.Reserve(SlicedVertices.Num());
	SkinnedNormals.Reserve(SlicedVertices.Num());
	const FPositionVertexBuffer &  PositionVertexBuffer   = RenderData.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer &StaticMeshVertexBuffer = RenderData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const int32                    MaxInfluences          = RenderData.SkinWeightVertexBuffer.GetMaxBoneInfluences();
	SlicingData->MaxBoneInfluences                        = MaxInfluences;

	// Calculate the skinned positions and normals of the vertices
	for (const int32 VertexIdx : SlicedVertices)
	{
		if(VertexIdx < 0 || StaticCast<uint64>(VertexIdx) >= PositionVertexBuffer.GetNumVertices())
		{
			continue;
		}
		
		FVector3f VertexPosition = PositionVertexBuffer.VertexPosition(VertexIdx);
		FVector3f VertexNormal   = StaticMeshVertexBuffer.VertexTangentZ(VertexIdx);
		FVector3f SkinnedPosition(0.0f, 0.0f, 0.0f);
		FVector3f SkinnedNormal(0.0f, 0.0f, 0.0f);
		for (int32 InfluenceIdx = 0; InfluenceIdx < MaxInfluences; ++InfluenceIdx)
		{
			if (SlicingData->OriginalSkinWeights[VertexIdx].InfluenceWeights[InfluenceIdx] == 0)
			{
				break; // Influences are stored in descending order, no need to check any further influences if we hit 0
			}

			const float Weight = StaticCast<float>(
				SlicingData->OriginalSkinWeights[VertexIdx].InfluenceWeights[InfluenceIdx]) / UINT16_MAX;
			const FBoneIndexType InfluenceBoneIdx = SlicingData->OriginalSkinWeights[VertexIdx].InfluenceBones[
				InfluenceIdx];
			const uint32 SkeletonBoneIdx = SlicingData->MeshToRefSkelBoneMap[InfluenceBoneIdx];
			SkinnedPosition += Weight * SkinningMatrices[SkeletonBoneIdx].TransformPosition(VertexPosition);
			SkinnedNormal += Weight * SkinningMatrices[SkeletonBoneIdx].TransformVector(VertexNormal);
		}
		SkinnedPositions.Add(SkinnedPosition);
		SkinnedNormals.Add(SkinnedNormal.GetSafeNormal());
	}

	int32 AboveVertexCount = 0;
	int32 BelowVertexCount = 0;

	//Now check which vertices are above and which are below the plane and add them accordingly
	SlicedDistances.Reserve(SlicedVertices.Num());
	for (int32 SkinnedIdx = 0; const int32 VertexIdx : SlicedVertices)
	{
		const float Distance = SlicePlane.PlaneDot(FVector(SkinnedPositions[SkinnedIdx]));
		SlicedDistances.Add(VertexIdx, Distance);
		++SkinnedIdx;

		const int32 SectionIdx = VerticesToSectionMap[VertexIdx];
		if (Distance > 0.0f)
		{
			AbovePerSectionVertexIndices[SectionIdx].Add(VertexIdx);
			++AboveVertexCount;
		}
		else
		{
			BelowPerSectionVertexIndices[SectionIdx].Add(VertexIdx);
			++BelowVertexCount;
		}
	}

	if (AboveVertexCount == 0 || BelowVertexCount == 0)
	{
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	for (const int32 BoneIdx : SlicingData->BonesAbove)
	{
		if(BoneIdx == INDEX_NONE || SlicingData->BoneVertexMap.IsValidIndex(BoneIdx) == false)
		{
			continue;
		}
		
		for (const int32 VertexIdx : SlicingData->BoneVertexMap[BoneIdx])
		{
			if(VertexIdx == INDEX_NONE || VerticesToSectionMap.IsValidIndex(VertexIdx) == false)
			{
				continue;
			}
			const int32 SectionIdx = VerticesToSectionMap[VertexIdx];
			AbovePerSectionVertexIndices[SectionIdx].Add(VertexIdx);
			++AboveVertexCount;
		}
	}

	for (const int32 BoneIdx : SlicingData->BonesBelow)
	{
		if(BoneIdx == INDEX_NONE || SlicingData->BoneVertexMap.IsValidIndex(BoneIdx) == false)
		{
			continue;
		}
		
		for (const int32 VertexIdx : SlicingData->BoneVertexMap[BoneIdx])
		{
			if(VertexIdx == INDEX_NONE || VerticesToSectionMap.IsValidIndex(VertexIdx) == false)
			{
				continue;
			}
			const int32 SectionIdx = VerticesToSectionMap[VertexIdx];
			BelowPerSectionVertexIndices[SectionIdx].Add(VertexIdx);
			++BelowVertexCount;
		}
	}

	TArray<TArray<FStaticMeshBuildVertex>> AbovePerSectionVertices;
	TArray<TArray<FSkinWeightInfo>>        AbovePerSectionSkinWeights;

	TArray<TArray<FStaticMeshBuildVertex>> BelowPerSectionVertices;
	TArray<TArray<FSkinWeightInfo>>        BelowPerSectionSkinWeights;

	AbovePerSectionVertices.SetNum(RenderData.RenderSections.Num());
	AbovePerSectionSkinWeights.SetNum(RenderData.RenderSections.Num());
	BelowPerSectionVertices.SetNum(RenderData.RenderSections.Num());
	BelowPerSectionSkinWeights.SetNum(RenderData.RenderSections.Num());

	const FColorVertexBuffer &ColorVertexBuffer = RenderData.StaticVertexBuffers.ColorVertexBuffer;
	bool                      bUseVertexColors  = ColorVertexBuffer.GetNumVertices() != 0;
	const uint8               NumTexCoords      = StaticMeshVertexBuffer.GetNumTexCoords();
	SlicingData->MaxTexCoords                   = NumTexCoords;

	for (int32 SectionIdx = 0; SectionIdx < RenderData.RenderSections.Num(); ++SectionIdx)
	{
		FStaticMeshBuildVertex Vertex; //reusable
		FMemory::Memzero(Vertex.UVs);
		for (const int32 VertexIdx : AbovePerSectionVertexIndices[SectionIdx])
		{
			Vertex.Position = PositionVertexBuffer.VertexPosition(VertexIdx);
			//todo: implement color handling
			Vertex.TangentX = StaticMeshVertexBuffer.VertexTangentX(VertexIdx);
			Vertex.TangentY = StaticMeshVertexBuffer.VertexTangentY(VertexIdx);
			Vertex.TangentZ = StaticMeshVertexBuffer.VertexTangentZ(VertexIdx);
			for (uint8 UVIdx = 0; UVIdx < NumTexCoords; ++UVIdx)
			{
				Vertex.UVs[UVIdx] = StaticMeshVertexBuffer.GetVertexUV(VertexIdx, UVIdx);
			}
			AbovePerSectionVertices[SectionIdx].Add(Vertex);
			AbovePerSectionSkinWeights[SectionIdx].Add(SlicingData->OriginalSkinWeights[VertexIdx]);
			const int32 NewVertexIdx = BaseToAboveVertIndex.Num();
			BaseToAboveVertIndex.Add(VertexIdx, NewVertexIdx);
		}
		for (const int32 VertexIdx : BelowPerSectionVertexIndices[SectionIdx])
		{
			Vertex.Position = PositionVertexBuffer.VertexPosition(VertexIdx);
			//todo: implement color handling
			Vertex.TangentX = StaticMeshVertexBuffer.VertexTangentX(VertexIdx);
			Vertex.TangentY = StaticMeshVertexBuffer.VertexTangentY(VertexIdx);
			Vertex.TangentZ = StaticMeshVertexBuffer.VertexTangentZ(VertexIdx);
			for (uint8 UVIdx = 0; UVIdx < NumTexCoords; ++UVIdx)
			{
				Vertex.UVs[UVIdx] = StaticMeshVertexBuffer.GetVertexUV(VertexIdx, UVIdx);
			}
			BelowPerSectionVertices[SectionIdx].Add(Vertex);
			BelowPerSectionSkinWeights[SectionIdx].Add(SlicingData->OriginalSkinWeights[VertexIdx]);
			const int32 NewVertexIdx = BaseToBelowVertIndex.Num();
			BaseToBelowVertIndex.Add(VertexIdx, NewVertexIdx);
		}
	}

	if (BaseToAboveVertIndex.IsEmpty() || BaseToBelowVertIndex.IsEmpty())
	//no vertices on one of the sides, no need to slice anything
	{
		UE_LOG(LogTemp, Display, TEXT("No vertices found to create one of the slice sides, aborting"));
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	TArray<uint32> BaseIndexBuffer;
	RenderData.MultiSizeIndexContainer.GetIndexBuffer(BaseIndexBuffer);

	TArray<TArray<uint32>> AbovePerSectionIndexBuffers;
	TArray<TArray<uint32>> BelowPerSectionIndexBuffers;
	AbovePerSectionIndexBuffers.SetNum(RenderData.RenderSections.Num());
	BelowPerSectionIndexBuffers.SetNum(RenderData.RenderSections.Num());

	TArray<int32> AboveSectionVertexOffsets;
	TArray<int32> BelowSectionVertexOffsets;
	AboveSectionVertexOffsets.SetNumZeroed(RenderData.RenderSections.Num());
	BelowSectionVertexOffsets.SetNumZeroed(RenderData.RenderSections.Num());

	for (int32 SectionIdx = 1, AboveVertexSum = 0, BelowVertexSum = 0; SectionIdx < RenderData.RenderSections.Num(); ++
	     SectionIdx)
	{
		AboveVertexSum += AbovePerSectionVertices[SectionIdx - 1].Num();
		BelowVertexSum += BelowPerSectionVertices[SectionIdx - 1].Num();

		AboveSectionVertexOffsets[SectionIdx] = AboveVertexSum;
		BelowSectionVertexOffsets[SectionIdx] = BelowVertexSum;
	}

	TArray<FUtilSkinnedEdge3D> ClipEdges;

	// Iterating over base triangles, (3 indices at a time)
	for (int32 BaseIndex = 0; BaseIndex < BaseIndexBuffer.Num(); BaseIndex += 3)
	{
		int32  BaseV[3] {INT32_MAX, INT32_MAX, INT32_MAX};        // Triangle vert indices in original mesh
		int32 *SlicedBelowV[3] {nullptr, nullptr, nullptr}; // Pointers to vert indices in new 'below' v buffer
		int32 *SlicedAboveV[3] {nullptr, nullptr, nullptr}; // Pointers to vert indices in new 'other half' / 'above'  v buffer
		int32  SectionIndices[3] {INT32_MAX, INT32_MAX, INT32_MAX};

		// For each vertex..
		for (int32 i = 0; i < 3; ++i)
		{
			// Get triangle vert idx
			BaseV[i]          = BaseIndexBuffer[BaseIndex + i];
			SectionIndices[i] = VerticesToSectionMap[BaseV[i]];
			// Look up in our sliced buffers
			SlicedBelowV[i] = BaseToBelowVertIndex.Find(BaseV[i]);
			SlicedAboveV[i] = BaseToAboveVertIndex.Find(BaseV[i]);
			// Each base vert _must_ exist in either Above or Below section
			check((SlicedAboveV[i] != nullptr) != (SlicedBelowV[i] != nullptr))
		}

		// If all verts stay above the plane, triangle goes to 'above' section
		if (SlicedAboveV[0] != nullptr && SlicedAboveV[1] != nullptr && SlicedAboveV[2] != nullptr)
		{
			check(SectionIndices[0] == SectionIndices[1] &&SectionIndices[0] == SectionIndices[2]);
			const int32 SectionIdx    = SectionIndices[0];
			const int32 ToLocalOffset = AboveSectionVertexOffsets[SectionIdx];
			AbovePerSectionIndexBuffers[SectionIdx].Add((*SlicedAboveV[0]) - ToLocalOffset);
			AbovePerSectionIndexBuffers[SectionIdx].Add((*SlicedAboveV[1]) - ToLocalOffset);
			AbovePerSectionIndexBuffers[SectionIdx].Add((*SlicedAboveV[2]) - ToLocalOffset);
		}
		// Analogically, except the triangle ends up in 'below' section
		else if (SlicedBelowV[0] != nullptr && SlicedBelowV[1] != nullptr && SlicedBelowV[2] != nullptr)
		{
			check(SectionIndices[0] == SectionIndices[1] &&SectionIndices[0] == SectionIndices[2]);
			const int32 SectionIdx    = SectionIndices[0];
			const int32 ToLocalOffset = BelowSectionVertexOffsets[SectionIdx];
			BelowPerSectionIndexBuffers[SectionIdx].Add((*SlicedBelowV[0]) - ToLocalOffset);
			BelowPerSectionIndexBuffers[SectionIdx].Add((*SlicedBelowV[1]) - ToLocalOffset);
			BelowPerSectionIndexBuffers[SectionIdx].Add((*SlicedBelowV[2]) - ToLocalOffset);
		}
		// If partially culled, clip to create 1 or 2 new triangles
		else
		{
			int32 BelowFinalVerts[4] {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
			int32 NumBelowFinalVerts = 0;
			int32 BelowVertSections[4] {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};

			int32 AboveFinalVerts[4] {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
			int32 NumAboveFinalVerts = 0;
			int32 AboveVertSections[4] {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};


			FUtilSkinnedEdge3D NewClipEdge;
			int32              ClippedEdges = 0;

			float PlaneDist[3] {UE_BIG_NUMBER, UE_BIG_NUMBER, UE_BIG_NUMBER};

			int32 VerticesToSkin[3]{-1, -1, -1};
			if (SlicedDistances.Contains(BaseV[0]) == false)
			{
				VerticesToSkin[0] = BaseV[0];
			}
			if (SlicedDistances.Contains(BaseV[1]) == false)
			{
				VerticesToSkin[1] = BaseV[1];
			}
			if (SlicedDistances.Contains(BaseV[2]) == false)
			{
				VerticesToSkin[2] = BaseV[2];
			}
			for (int32 i = 0; i < 3; ++i)
			{
				const int32 VertexIdx = VerticesToSkin[i];
				if (VerticesToSkin[i] == -1)
				{
					continue;
				}

				FVector3f VertexPosition = PositionVertexBuffer.VertexPosition(VertexIdx);
				FVector3f SkinnedPosition(0.0f, 0.0f, 0.0f);
				for (int32 InfluenceIdx = 0; InfluenceIdx < MaxInfluences; ++InfluenceIdx)
				{
					if (SlicingData->OriginalSkinWeights[VertexIdx].InfluenceWeights[InfluenceIdx] == 0)
					{
						break;
						// Influences are stored in descending order, no need to check any further influences if we hit 0
					}

					const float Weight = StaticCast<float>(
						SlicingData->OriginalSkinWeights[VertexIdx].InfluenceWeights[InfluenceIdx]) / UINT16_MAX;
					const FBoneIndexType InfluenceBoneIdx = SlicingData->OriginalSkinWeights[VertexIdx].InfluenceBones[
						InfluenceIdx];
					const int32 SkeletonBoneIdx = SlicingData->MeshToRefSkelBoneMap[InfluenceBoneIdx];
					SkinnedPosition += Weight * SkinningMatrices[SkeletonBoneIdx].TransformPosition(VertexPosition);
				}
				const float Distance = SlicePlane.PlaneDot(FVector(SkinnedPosition));
				SlicedDistances.Add(VertexIdx, Distance);
			}

			PlaneDist[0] = SlicedDistances[BaseV[0]];
			PlaneDist[1] = SlicedDistances[BaseV[1]];
			PlaneDist[2] = SlicedDistances[BaseV[2]];

			for (int32 EdgeIdx = 0; EdgeIdx < 3; ++EdgeIdx)
			{
				int32       ThisVert           = EdgeIdx;
				const int32 ThisVertSectionIdx = VerticesToSectionMap[BaseV[ThisVert]];

				// Depending on whether the vert is below or above, add it to appropriate verts
				if (SlicedAboveV[ThisVert] != nullptr)
				{
					check(NumAboveFinalVerts < 4);
					const int32 ToLocalOffset             = AboveSectionVertexOffsets[ThisVertSectionIdx];
					AboveVertSections[NumAboveFinalVerts] = ThisVertSectionIdx;
					AboveFinalVerts[NumAboveFinalVerts++] = *SlicedAboveV[ThisVert] - ToLocalOffset;
				}
				else
				{
					check(NumBelowFinalVerts < 4);
					const int32 ToLocalOffset             = BelowSectionVertexOffsets[ThisVertSectionIdx];
					BelowVertSections[NumBelowFinalVerts] = ThisVertSectionIdx;
					BelowFinalVerts[NumBelowFinalVerts++] = *SlicedBelowV[ThisVert] - ToLocalOffset;
				}

				// If start and next vert are opposite sides, add intersection
				int32       NextVert           = (EdgeIdx + 1) % 3;

				if ((SlicedAboveV[EdgeIdx] != nullptr) != (SlicedAboveV[NextVert] != nullptr))
				{
					// Find distance along edge that plane is
					float Alpha = FMath::Clamp(-PlaneDist[ThisVert] / (PlaneDist[NextVert] - PlaneDist[ThisVert]),
					                           0.0f,
					                           1.0f);

					//Interpolate vertex to that point

					int32 *                 V0Idx        = BaseToAboveVertIndex.Find(BaseV[ThisVert]);
					int32 *                 V1Idx        = BaseToBelowVertIndex.Find(BaseV[NextVert]);
					FStaticMeshBuildVertex *V0           = nullptr;
					FStaticMeshBuildVertex *V1           = nullptr;
					const int32             V0SectionIdx = VerticesToSectionMap[BaseV[ThisVert]];
					const int32             V1SectionIdx = VerticesToSectionMap[BaseV[NextVert]];
					if (V0Idx != nullptr)
					{
						const int32 LocalV0Idx = *V0Idx - AboveSectionVertexOffsets[V0SectionIdx];
						check(LocalV0Idx >= 0 && LocalV0Idx < AbovePerSectionVertices[V0SectionIdx].Num())
						V0                                    = &AbovePerSectionVertices[V0SectionIdx][LocalV0Idx];
						AboveVertSections[NumAboveFinalVerts] = V0SectionIdx;
					}
					else
					{
						V0Idx = BaseToBelowVertIndex.Find(BaseV[ThisVert]);

						// The index MUST exist in one or the other section
						check(V0Idx != nullptr);
						const int32 LocalV0Idx = *V0Idx - BelowSectionVertexOffsets[V0SectionIdx];
						check(LocalV0Idx >= 0 && LocalV0Idx < BelowPerSectionVertices[V0SectionIdx].Num())
						V0                                    = &BelowPerSectionVertices[V0SectionIdx][LocalV0Idx];
						BelowVertSections[NumBelowFinalVerts] = V0SectionIdx;
					}

					if (V1Idx != nullptr)
					{
						const int32 LocalV1Idx = *V1Idx - BelowSectionVertexOffsets[V1SectionIdx];
						check(LocalV1Idx >= 0 && LocalV1Idx < BelowPerSectionVertices[V1SectionIdx].Num())
						V1                                    = &BelowPerSectionVertices[V1SectionIdx][LocalV1Idx];
						BelowVertSections[NumBelowFinalVerts] = V1SectionIdx;
					}
					else
					{
						V1Idx = BaseToAboveVertIndex.Find(BaseV[NextVert]);
						check(V1Idx != nullptr)
						const int32 LocalV1Idx = *V1Idx - AboveSectionVertexOffsets[V1SectionIdx];
						check(LocalV1Idx >= 0 && LocalV1Idx < AbovePerSectionVertices[V1SectionIdx].Num())
						V1                                    = &AbovePerSectionVertices[V1SectionIdx][LocalV1Idx];
						AboveVertSections[NumAboveFinalVerts] = V1SectionIdx;
					}

					FStaticMeshBuildVertex InterpVert;
					FSkinWeightInfo        InterpSkinWeights;
					InterpolateVert(*V0,
					                SlicingData->OriginalSkinWeights[BaseV[ThisVert]],
					                *V1,
					                SlicingData->OriginalSkinWeights[BaseV[NextVert]],
					                Alpha,
					                InterpVert,
					                InterpSkinWeights);


					// Add the new vertex to both sections
					check(V0SectionIdx == V1SectionIdx);
					int32                 AboveInterpVertIdx = AbovePerSectionVertices[V0SectionIdx].Add(InterpVert);
					AbovePerSectionSkinWeights[V0SectionIdx].Add(InterpSkinWeights);
					++AboveVertexCount;


					// Save vert index for this poly
					check(NumAboveFinalVerts < 4);
					AboveFinalVerts[NumAboveFinalVerts++] = AboveInterpVertIdx;

					int32 BelowInterpVertIdx = BelowVertexCount;
					BelowInterpVertIdx       = BelowPerSectionVertices[V1SectionIdx].Add(InterpVert);
					BelowPerSectionSkinWeights[V1SectionIdx].Add(InterpSkinWeights);
					++BelowVertexCount;
					check(NumBelowFinalVerts < 4);
					BelowFinalVerts[NumBelowFinalVerts++] = BelowInterpVertIdx;

					// When we make a new edge on the surface of the clip plane, save it off.
					check(ClippedEdges < 2);
					if (ClippedEdges == 0)
					{
						NewClipEdge.V0            = InterpVert.Position;
						NewClipEdge.V0SkinWeights = InterpSkinWeights;
					}
					else
					{
						NewClipEdge.V1            = InterpVert.Position;
						NewClipEdge.V1SkinWeights = InterpSkinWeights;
					}

					++ClippedEdges;
				}
			}

			// Triangulate the clipped polygon.
			for (int32 VertexIdx = 2; VertexIdx < NumAboveFinalVerts; ++VertexIdx)
			{
				check(AboveVertSections[0] == AboveVertSections[1] && AboveVertSections[0] == AboveVertSections[2]);
				//triangles need to belong to a single section
				const int32 SectionIdx = AboveVertSections[0];
				AbovePerSectionIndexBuffers[SectionIdx].Add(AboveFinalVerts[0]);
				AbovePerSectionIndexBuffers[SectionIdx].Add(AboveFinalVerts[VertexIdx - 1]);
				AbovePerSectionIndexBuffers[SectionIdx].Add(AboveFinalVerts[VertexIdx]);
			}

			for (int32 VertexIdx = 2; VertexIdx < NumBelowFinalVerts; ++VertexIdx)
			{
				check(BelowVertSections[0] == BelowVertSections[1] && BelowVertSections[0] == BelowVertSections[2])
				const int32 SectionIdx = BelowVertSections[0];
				BelowPerSectionIndexBuffers[SectionIdx].Add(BelowFinalVerts[0]);
				BelowPerSectionIndexBuffers[SectionIdx].Add(BelowFinalVerts[VertexIdx - 1]);
				BelowPerSectionIndexBuffers[SectionIdx].Add(BelowFinalVerts[VertexIdx]);
			}

			check(ClippedEdges != 1); // Should never clip just one edge of the triangle

			// If we created a new edge, save that off here as well
			if (ClippedEdges == 2)
			{
				ClipEdges.Add(NewClipEdge);
			}
		}
	}

	// Create cap geometry
	TArray<int32>                  AboveCapVertexIndices;
	TArray<FStaticMeshBuildVertex> AboveCapVertices;
	TArray<uint32>                 AboveCapIndexBuffer;
	TArray<FSkinWeightInfo>        AboveCapSkinWeights;
	int32                          AboveCapSection = -1;

	TArray<int32>                  BelowCapVertexIndices;
	TArray<FStaticMeshBuildVertex> BelowCapVertices;
	TArray<uint32>                 BelowCapIndexBuffer;
	TArray<FSkinWeightInfo>        BelowCapSkinWeights;
	int32                          BelowCapSection = -1;

	TArray<UMaterialInterface *> AboveMaterials = SlicingData->OriginalSkeletalMeshComponent->GetMaterials();
	TArray<UMaterialInterface *> BelowMaterials = SlicingData->OriginalSkeletalMeshComponent->GetMaterials();;


	if (int32 SectionIndex = AboveMaterials.Find(SlicingData->CapMaterial); SectionIndex == INDEX_NONE)
	{
		AboveCapSection = AboveMaterials.AddUnique(SlicingData->CapMaterial);

		AbovePerSectionVertices.SetNum(RenderData.RenderSections.Num() + 1);
		BelowPerSectionVertices.SetNum(RenderData.RenderSections.Num() + 1);

		AbovePerSectionIndexBuffers.SetNum(RenderData.RenderSections.Num() + 1);
		BelowPerSectionIndexBuffers.SetNum(RenderData.RenderSections.Num() + 1);

		AbovePerSectionSkinWeights.SetNum(RenderData.RenderSections.Num() + 1);
		BelowPerSectionSkinWeights.SetNum(RenderData.RenderSections.Num() + 1);

		AboveSectionVertexOffsets.Add(0);
		BelowSectionVertexOffsets.Add(0);

		BelowCapSection = BelowMaterials.Add(SlicingData->CapMaterial);

		// In this case, we need a new bone map for both sections, since they will be created
		// These are the full bone maps, meaning they map the whole skeleton between mesh and Reference Skeleton Bone indices
		SlicingData->AboveRenderSectionBoneMaps.Add(SlicingData->MeshToRefSkelBoneMap);
		SlicingData->BelowRenderSectionBoneMaps.Add(SlicingData->MeshToRefSkelBoneMap);
	}
	else
	{
		AboveCapSection = SectionIndex;
		BelowCapSection = SectionIndex;
	}

	if (ClipEdges.Num() > 0)
	{
		// SkinEdges(ClipEdges, SkinningMatrices, SlicingData->MeshToRefSkelBoneMap);
		// Project 3D Edges onto slice plane to form 2D Edges;
		TArray<FUtilSkinnedEdge2D> Edges2D;
		FMatrix                    CapToWorld;
		ProjectEdges(Edges2D, CapToWorld, ClipEdges, SlicePlane);

		// Remember starting point for vert and index buffer before adding and cap geom
		int32 CapVertBase  = AboveCapVertices.Num();
		int32 CapIndexBase = AboveCapIndexBuffer.Num();


		FixEdgeHoles(Edges2D);
		TArray<uint32>           DebugIndices;
		TArray<CDT::V2d<SLICER_DEFAULT_COORD_TYPE>> CapV2Ds;
		DelaunayTriangulate(Edges2D, DebugIndices, CapV2Ds);
		ConvertV2DsTo3D(CapV2Ds, CapToWorld, AboveCapVertices, AboveCapSkinWeights);
		AboveCapIndexBuffer = MoveTemp(DebugIndices);
		BelowCapSkinWeights = AboveCapSkinWeights;

		/*
		AsyncTask(ENamedThreads::GameThread, [SlicingData, AboveCapVertices, AboveCapIndexBuffer, Edges2D, CapToWorld]
		{
			for(int32 TriIdx = 0; TriIdx < AboveCapIndexBuffer.Num(); TriIdx += 3)
			{
				const auto& V0 = SlicingData->ComponentToWorldTransform.TransformPosition(FVector(AboveCapVertices[AboveCapIndexBuffer[TriIdx]].Position));
				const auto& V1 = SlicingData->ComponentToWorldTransform.TransformPosition(FVector(AboveCapVertices[AboveCapIndexBuffer[TriIdx + 1]].Position));
				const auto& V2 = SlicingData->ComponentToWorldTransform.TransformPosition(FVector(AboveCapVertices[AboveCapIndexBuffer[TriIdx + 2]].Position));
				UKismetSystemLibrary::DrawDebugLine(GWorld, V0, V1, FLinearColor::Blue, 120.f, 0.03f);
				UKismetSystemLibrary::DrawDebugLine(GWorld, V1, V2, FLinearColor::Blue, 120.f, 0.03f);
				UKismetSystemLibrary::DrawDebugLine(GWorld, V2, V0, FLinearColor::Blue, 120.f, 0.03f);
			}

			for(const auto& Edge : Edges2D)
			{
				FVector V0 = CapToWorld.TransformPosition(FVector{Edge.V0.X, Edge.V0.Y, Edge.V0Z});
				FVector V1 = CapToWorld.TransformPosition(FVector{Edge.V1.X, Edge.V1.Y, Edge.V0Z});
				V0 = SlicingData->ComponentToWorldTransform.TransformPosition(V0);
				V1 = SlicingData->ComponentToWorldTransform.TransformPosition(V1);

				UKismetSystemLibrary::DrawDebugLine(GWorld, V0, V1, FColor::Magenta, 120.f, 0.05f);
			}
		});
		*/

		// "Unskin" the vertices, so they are in bind pose again

		TArray<FMatrix44f> ReverseSkinningMatrices;
		ReverseSkinningMatrices.Reserve(BindPoseTransforms.Num());

		for(int32 BoneIdx = 0; BoneIdx < BindPoseTransforms.Num(); BoneIdx++)
		{
			// FMatrix44f Matrix = BindPoseTransforms[BoneIdx].Inverse() * (FMatrix44f) SlicingData->ComponentSpaceTransforms[BoneIdx].ToInverseMatrixWithScale();
			// ReverseSkinningMatrices.Add(Matrix);
			ReverseSkinningMatrices.Add(SkinningMatrices[BoneIdx].Inverse());
		}
		
		for(int32 VertexIdx = 0; FStaticMeshBuildVertex& Vertex : AboveCapVertices)
		{
			// ReverseSkinning(Vertex.Position, Vertex.TangentZ, AboveCapSkinWeights[VertexIdx], ReverseSkinningMatrices, SlicingData->MeshToRefSkelBoneMap);
			++VertexIdx;
		}
		
		BelowCapVertexIndices.Reserve(AboveCapVertices.Num());
		// Copy verts from 'above' cap (both caps should be identical after all)
		for (int32 VertexIdx = CapVertBase; VertexIdx < AboveCapVertices.Num(); ++VertexIdx)
		{
			FStaticMeshBuildVertex BelowCapVert = AboveCapVertices[VertexIdx];

			BelowCapVert.TangentZ *= -1.0f;
			BelowCapVert.TangentX *= -1.0f;

			BelowCapVertices.Add(BelowCapVert);
		}

		BelowCapIndexBuffer = AboveCapIndexBuffer;

		for (int32 IndexIdx = CapIndexBase; IndexIdx < BelowCapIndexBuffer.Num(); IndexIdx += 3)
		{
			AboveCapIndexBuffer[IndexIdx + 0] = BelowCapIndexBuffer[IndexIdx + 0];
			AboveCapIndexBuffer[IndexIdx + 1] = BelowCapIndexBuffer[IndexIdx + 2];
			AboveCapIndexBuffer[IndexIdx + 2] = BelowCapIndexBuffer[IndexIdx + 1];
		}
	}

	//converting to Section-local indices
	for (int32 IndexIdx = 0; IndexIdx < AboveCapIndexBuffer.Num(); ++IndexIdx)
	//both caps have the same vertices, no need to do 2 loops
	{
		AboveCapIndexBuffer[IndexIdx] += AbovePerSectionVertices[AboveCapSection].Num();
		BelowCapIndexBuffer[IndexIdx] += BelowPerSectionVertices[BelowCapSection].Num();
	}

	AbovePerSectionVertices[AboveCapSection].Append(AboveCapVertices);
	AbovePerSectionIndexBuffers[AboveCapSection].Append(AboveCapIndexBuffer);
	AbovePerSectionSkinWeights[AboveCapSection].Append(AboveCapSkinWeights);

	BelowPerSectionVertices[BelowCapSection].Append(BelowCapVertices);
	BelowPerSectionIndexBuffers[BelowCapSection].Append(BelowCapIndexBuffer);
	BelowPerSectionSkinWeights[BelowCapSection].Append(BelowCapSkinWeights);

	TMap<FName, TArray<FKConvexElem>> &AboveSlicedConvexElems = SlicingData->AboveConvexElems;
	TMap<FName, TArray<FKConvexElem>> &BelowSlicedConvexElems = SlicingData->BelowConvexElems;

	AboveSlicedConvexElems.Empty();
	BelowSlicedConvexElems.Empty();

	UPhysicsAsset *SkelMeshPhysAsset = SlicingData->OriginalSkeletalMeshComponent->GetPhysicsAsset();

	if (SkelMeshPhysAsset)
	{
		for (int32 SkelBodySetupIdx = 0; SkelBodySetupIdx < SkelMeshPhysAsset->SkeletalBodySetups.Num(); ++
		     SkelBodySetupIdx)
		{
			const USkeletalBodySetup *SkeletalBodySetup = SkelMeshPhysAsset->SkeletalBodySetups[SkelBodySetupIdx];
			const FName &             BoneName = SkeletalBodySetup->BoneName;
			const int32               BoneIdx = SlicingData->OriginalSkeletalMeshComponent->GetBoneIndex(BoneName);
			if (SlicingData->BonesAbove.Contains(BoneIdx))
			{
				AboveSlicedConvexElems.Add(BoneName, SkeletalBodySetup->AggGeom.ConvexElems);
			}
			else if (SlicingData->BonesBelow.Contains(BoneIdx))
			{
				BelowSlicedConvexElems.Add(BoneName, SkeletalBodySetup->AggGeom.ConvexElems);
			}
			else // Bone is in sliced bones, need to do some slicing
			{
				if (AboveSlicedConvexElems.Contains(BoneName) == false)
				{
					AboveSlicedConvexElems.Add(BoneName, TArray<FKConvexElem>());
				}

				if (BelowSlicedConvexElems.Contains(BoneName) == false)
				{
					BelowSlicedConvexElems.Add(BoneName, TArray<FKConvexElem>());
				}

				FTransform InvBoneTransform = SlicingData->OriginalSkeletalMeshComponent->GetBoneTransform(BoneName).
				                                           Inverse();
				FVector BoneSpacePlanePoint  = InvBoneTransform.TransformPosition(SlicingData->WorldSlicePlanePos);
				FVector BoneSpacePlaneNormal = InvBoneTransform.TransformVector(SlicingData->WorldSlicePlaneNormal).
				                                                GetSafeNormal();
				FPlane                      BoneSpacePlane(BoneSpacePlanePoint, BoneSpacePlaneNormal);
				const TArray<FKConvexElem> &ConvexElems = SkeletalBodySetup->AggGeom.ConvexElems;
				TArray<FVector>             AboveSlicedConvexVerts;
				TArray<FVector>             BelowSlicedConvexVerts;
				FKConvexElem                NewConvexElem;
				for (int32 ConvexIdx = 0; ConvexIdx < ConvexElems.Num(); ++ConvexIdx)
				{
					const FKConvexElem &BaseElem = ConvexElems[ConvexIdx];
					SliceConvexElem(BaseElem, BoneSpacePlane, AboveSlicedConvexVerts, BelowSlicedConvexVerts);
					if (AboveSlicedConvexVerts.Num() >= 4)
					{
						NewConvexElem.VertexData = AboveSlicedConvexVerts;
						NewConvexElem.ElemBox    = FBox(AboveSlicedConvexVerts);
						AboveSlicedConvexElems[BoneName].Add(NewConvexElem);
					}
					if (BelowSlicedConvexVerts.Num() >= 4)
					{
						NewConvexElem.VertexData = BelowSlicedConvexVerts;
						NewConvexElem.ElemBox    = FBox(BelowSlicedConvexVerts);
						BelowSlicedConvexElems[BoneName].Add(NewConvexElem);
					}
				}
			}
		}
	}


	//Get rid of any empty sections now
	//todo: FIXME: for some reason some materials end up appearing twice sometimes???
	if ((AbovePerSectionVertices.Num() == AbovePerSectionIndexBuffers.Num() && AbovePerSectionVertices.Num() ==
		AbovePerSectionSkinWeights.Num() && AbovePerSectionVertices.Num() == AboveMaterials.Num()) == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Mismatch between section numbers, AbovePerSectionVertices : %d, AbovePerSectionIndexBuffers %d, AbovePerSectionSkinWeights %d, AboveMaterials %d"), *FString(__FUNCTION__), AbovePerSectionVertices.Num(), AbovePerSectionIndexBuffers.Num(),AbovePerSectionSkinWeights.Num(), AboveMaterials.Num());
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	for (int32 SectionIdx = AbovePerSectionVertices.Num() - 1; SectionIdx >= 0; --SectionIdx)
	{
		if (AbovePerSectionVertices[SectionIdx].IsEmpty() || AbovePerSectionIndexBuffers[SectionIdx].IsEmpty())
		{
			AboveSectionVertexOffsets.RemoveAt(SectionIdx);
			AbovePerSectionVertices.RemoveAt(SectionIdx);
			AbovePerSectionIndexBuffers.RemoveAt(SectionIdx);
			AbovePerSectionSkinWeights.RemoveAt(SectionIdx);
			AboveMaterials.RemoveAt(SectionIdx);
		}
	}


	if((BelowPerSectionVertices.Num() == BelowPerSectionIndexBuffers.Num() && BelowPerSectionVertices.Num() ==
		BelowPerSectionSkinWeights.Num() && BelowPerSectionVertices.Num() == BelowMaterials.Num()) == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Mismatch between section numbers, BelowPerSectionVertices : %d, BelowPerSectionIndexBuffers %d, BelowPerSectionSkinWeights %d, BelowMaterials %d"), *FString(__FUNCTION__), BelowPerSectionVertices.Num(), BelowPerSectionIndexBuffers.Num(),BelowPerSectionSkinWeights.Num(), BelowMaterials.Num());
		SlicingData->bShouldTaskAbort = true;
		return;
	}

	for (int32 SectionIdx = BelowPerSectionVertices.Num() - 1; SectionIdx >= 0; --SectionIdx)
	{
		if (BelowPerSectionVertices[SectionIdx].IsEmpty() || BelowPerSectionIndexBuffers[SectionIdx].IsEmpty())
		{
			BelowSectionVertexOffsets.RemoveAt(SectionIdx);
			BelowPerSectionVertices.RemoveAt(SectionIdx);
			BelowPerSectionIndexBuffers.RemoveAt(SectionIdx);
			BelowPerSectionSkinWeights.RemoveAt(SectionIdx);
			BelowMaterials.RemoveAt(SectionIdx);
		}
	}

	//Now fix the index buffers and prepare the offsets
	TArray<int32> AboveSectionIndexOffsets;
	TArray<int32> BelowSectionIndexOffsets;

	AboveSectionIndexOffsets.SetNumZeroed(AboveSectionVertexOffsets.Num());
	BelowSectionIndexOffsets.SetNumZeroed(BelowSectionVertexOffsets.Num());
	{
		int32 AboveVertexSum = 0;
		int32 BelowVertexSum = 0;
		int32 AboveIndexSum  = 0;
		int32 BelowIndexSum  = 0;
		for (int32 SectionIdx = 1; SectionIdx < AbovePerSectionVertices.Num(); ++SectionIdx)
		{
			AboveVertexSum += AbovePerSectionVertices[SectionIdx - 1].Num();
			AboveIndexSum += AbovePerSectionIndexBuffers[SectionIdx - 1].Num();
			AboveSectionVertexOffsets[SectionIdx] = AboveVertexSum;
			AboveSectionIndexOffsets[SectionIdx]  = AboveIndexSum;

			for (int32 IndexIdx = 0; IndexIdx < AbovePerSectionIndexBuffers[SectionIdx].Num(); ++IndexIdx)
			{
				AbovePerSectionIndexBuffers[SectionIdx][IndexIdx] += AboveVertexSum;
			}
		}

		for (int32 SectionIdx = 1; SectionIdx < BelowPerSectionVertices.Num(); ++SectionIdx)
		{
			BelowVertexSum += BelowPerSectionVertices[SectionIdx - 1].Num();
			BelowIndexSum += BelowPerSectionIndexBuffers[SectionIdx - 1].Num();
			BelowSectionVertexOffsets[SectionIdx] = BelowVertexSum;
			BelowSectionIndexOffsets[SectionIdx]  = BelowIndexSum;

			for (int32 IndexIdx = 0; IndexIdx < BelowPerSectionIndexBuffers[SectionIdx].Num(); ++IndexIdx)
			{
				BelowPerSectionIndexBuffers[SectionIdx][IndexIdx] += BelowVertexSum;
			}
		}
	}

	SlicingData->AboveRenderSectionVertexOffsets = MoveTemp(AboveSectionVertexOffsets);
	SlicingData->AboveRenderSectionIndexOffsets  = MoveTemp(AboveSectionIndexOffsets);
	SlicingData->BelowRenderSectionVertexOffsets = MoveTemp(BelowSectionVertexOffsets);
	SlicingData->BelowRenderSectionIndexOffsets  = MoveTemp(BelowSectionIndexOffsets);

	int32 AboveNumVertices = 0;
	int32 AboveNumIndices  = 0;
	int32 BelowNumVertices = 0;
	int32 BelowNumIndices  = 0;

	for (int32 SectionIdx = 0; SectionIdx < AbovePerSectionVertices.Num(); ++SectionIdx)
	{
		AboveNumVertices += AbovePerSectionVertices[SectionIdx].Num();
		AboveNumIndices += AbovePerSectionIndexBuffers[SectionIdx].Num();
	}

	for (int32 SectionIdx = 0; SectionIdx < BelowPerSectionVertices.Num(); ++SectionIdx)
	{
		BelowNumVertices += BelowPerSectionVertices[SectionIdx].Num();
		BelowNumIndices += BelowPerSectionIndexBuffers[SectionIdx].Num();
	}

	// Just to be extra sure
	SlicingData->AboveVertexBuffer.Empty();
	SlicingData->AboveIndexBuffer.Empty();
	SlicingData->AboveSkinWeights.Empty();
	SlicingData->BelowVertexBuffer.Empty();
	SlicingData->BelowIndexBuffer.Empty();
	SlicingData->BelowSkinWeights.Empty();

	SlicingData->AboveVertexBuffer.Reserve(AboveNumVertices);
	SlicingData->AboveIndexBuffer.Reserve(AboveNumIndices);
	SlicingData->AboveSkinWeights.Reserve(AboveNumVertices);

	SlicingData->BelowVertexBuffer.Reserve(BelowNumVertices);
	SlicingData->BelowIndexBuffer.Reserve(BelowNumIndices);
	SlicingData->BelowSkinWeights.Reserve(BelowNumVertices);

	for (int32 SectionIdx = 0; SectionIdx < AbovePerSectionVertices.Num(); ++SectionIdx)
	{
		SlicingData->AboveVertexBuffer.Append(MoveTemp(AbovePerSectionVertices[SectionIdx]));
		SlicingData->AboveIndexBuffer.Append(MoveTemp(AbovePerSectionIndexBuffers[SectionIdx]));
		SlicingData->AboveSkinWeights.Append(MoveTemp(AbovePerSectionSkinWeights[SectionIdx]));
	}

	for (int32 SectionIdx = 0; SectionIdx < BelowPerSectionVertices.Num(); ++SectionIdx)
	{
		SlicingData->BelowVertexBuffer.Append(MoveTemp(BelowPerSectionVertices[SectionIdx]));
		SlicingData->BelowIndexBuffer.Append(MoveTemp(BelowPerSectionIndexBuffers[SectionIdx]));
		SlicingData->BelowSkinWeights.Append(MoveTemp(BelowPerSectionSkinWeights[SectionIdx]));
	}


	for (int32 SectionIdx = 0; SectionIdx < AboveSectionVertexOffsets.Num(); ++SectionIdx)
	{
		SlicingData->AboveRenderSectionBoneMaps.Add(SlicingData->MeshToRefSkelBoneMap);
	}

	for (int32 SectionIdx = 0; SectionIdx < BelowSectionVertexOffsets.Num(); ++SectionIdx)
	{
		SlicingData->BelowRenderSectionBoneMaps.Add(SlicingData->MeshToRefSkelBoneMap);
	}

	SlicingData->AboveMaterials = MoveTemp(AboveMaterials);
	SlicingData->BelowMaterials = MoveTemp(BelowMaterials);

	SlicingData->bNeedsCPUAccess      = RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess();
	SlicingData->bUseFullPrecisionUVs = RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs();
}

void USkeletalMeshSlicerComponent::TASKUSAGE_FillNewAssets(TSharedRef<FSlicingData> SlicingData)
{
	if (SlicingData->bShouldTaskAbort)
	{
		return;
	}

	TArray<FBoneIndexType> RequiredNActiveBones = SlicingData->OriginalSkeletalMeshComponent->
	                                                           GetSkeletalMeshRenderData()->LODRenderData[0].
		RequiredBones;
	CreateMeshSlice(SlicingData->AboveSkeletalMesh,
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetSkeleton(),
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetImportedBounds(),
	                SlicingData->AboveSkinWeights,
	                SlicingData->AboveIndexBuffer,
	                SlicingData->AboveVertexBuffer,
	                SlicingData->AboveRenderSectionVertexOffsets,
	                SlicingData->AboveRenderSectionIndexOffsets,
	                SlicingData->AboveMaterials,
	                SlicingData->AboveRenderSectionBoneMaps,
	                SlicingData->MaxBoneInfluences,
	                SlicingData->MaxTexCoords,
	                RequiredNActiveBones,
	                RequiredNActiveBones,
	                SlicingData->bNeedsCPUAccess,
	                SlicingData->bUseFullPrecisionUVs);

	CreateMeshSlice(SlicingData->BelowSkeletalMesh,
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetSkeleton(),
	                SlicingData->OriginalSkeletalMeshComponent->GetSkeletalMeshAsset()->GetImportedBounds(),
	                SlicingData->BelowSkinWeights,
	                SlicingData->BelowIndexBuffer,
	                SlicingData->BelowVertexBuffer,
	                SlicingData->BelowRenderSectionVertexOffsets,
	                SlicingData->BelowRenderSectionIndexOffsets,
	                SlicingData->BelowMaterials,
	                SlicingData->BelowRenderSectionBoneMaps,
	                SlicingData->MaxBoneInfluences,
	                SlicingData->MaxTexCoords,
	                RequiredNActiveBones,
	                RequiredNActiveBones,
	                SlicingData->bNeedsCPUAccess,
	                SlicingData->bUseFullPrecisionUVs);

	CreateMeshSlicePhysicsAsset(SlicingData, true);
	CreateMeshSlicePhysicsAsset(SlicingData, false);
	SlicingData->AboveSkeletalMesh->SetPhysicsAsset(SlicingData->AbovePhysicsAsset);
	SlicingData->BelowSkeletalMesh->SetPhysicsAsset(SlicingData->BelowPhysicsAsset);
}

void USkeletalMeshSlicerComponent::TASKUSAGE_FinalizeNewAssetsCreation(TSharedRef<FSlicingData> SlicingData)
{
	if (SlicingData->bShouldTaskAbort)
	{
		bIsCurrentlySlicing = false;
		return;
	}

	USkeletalMesh *AboveMesh         = SlicingData->AboveSkeletalMesh;
	UPhysicsAsset *AbovePhysicsAsset = SlicingData->AbovePhysicsAsset;

	//Sanity check since weird crashes occured
	if(AboveMesh == nullptr || AboveMesh->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Above Mesh is nullptr/invalid"), TEXT(__FUNCTION__));
	}
	
	if(AbovePhysicsAsset == nullptr || AbovePhysicsAsset->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Above PhysAsset is nullptr/invalid"), TEXT(__FUNCTION__));
	}

	AboveMesh->InitResources();
	FPoseSnapshot PoseSnapshot;
	SlicingData->OriginalSkeletalMeshComponent->SnapshotPose(PoseSnapshot);
	// IPosableGib* OriginalActor = Cast<IPosableGib>(SlicingData->OriginalSkeletalMeshComponent->GetOwner());
	// OriginalActor->SetGibPose_Implementation(PoseSnapshot);
	SlicingData->OriginalSkeletalMeshComponent->SetSkeletalMesh(AboveMesh);
	// SlicingData->OriginalSkeletalMeshComponent->SetPhysicsAsset(AbovePhysicsAsset);
	/*
	AsyncTask(ENamedThreads::GameThread, [SlicingData]()
	{
		SlicingData->OriginalSkeletalMeshComponent->SetPhysicsAsset(SlicingData->AbovePoolItem->PhysicsAsset);
	});
	*/
	ReturnPooledAssets();
	CurrentSkeletalMesh = AboveMesh;
	CurrentPhysicsAsset = AbovePhysicsAsset;

	USkeletalMesh *BelowMesh         = SlicingData->BelowSkeletalMesh;
	UPhysicsAsset *BelowPhysicsAsset = SlicingData->BelowPhysicsAsset;
	
	if(BelowMesh == nullptr || BelowMesh->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : BelowMesh is nullptr/invalid"), TEXT(__FUNCTION__));
	}
	
	if(BelowPhysicsAsset == nullptr || BelowPhysicsAsset->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : Below PhysAsset is nullptr/invalid"), TEXT(__FUNCTION__));
	}

	BelowMesh->InitResources();

	FVector    SpawnLocation = SlicingData->OriginalSkeletalMeshComponent->GetComponentLocation();
	FRotator   SpawnRotation = SlicingData->OriginalSkeletalMeshComponent->GetComponentRotation();
	ASliceGib *SlicedGib     = Cast<ASliceGib>(
		GetWorld()->SpawnActor(ASliceGib::StaticClass(), &SpawnLocation, &SpawnRotation));
	SlicedGib->TeleportTo(SpawnLocation, SpawnRotation, false, true);

	USkeletalMeshSlicerComponent *GibSlicerComponent = SlicedGib->GetMeshSlicer();

	GibSlicerComponent->CurrentSkeletalMesh = BelowMesh;
	GibSlicerComponent->CurrentPhysicsAsset = BelowPhysicsAsset;
	SlicedGib->SetGibPose_Implementation(PoseSnapshot);
	SlicedGib->GetMesh()->SetSkeletalMesh(BelowMesh);


	SlicingData->OriginalSkeletalMeshComponent->SetAnimInstanceClass(GibAnimInstance);
	SlicedGib->GetMesh()->SetAnimInstanceClass(GibAnimInstance);


	UGibAnimInstance *AnimInstance = Cast<UGibAnimInstance>(SlicedGib->GetMesh()->GetAnimInstance());
	if (AnimInstance)
	{
		AnimInstance->PoseSnapshot = PoseSnapshot;
	}

	AnimInstance = Cast<UGibAnimInstance>(SlicingData->OriginalSkeletalMeshComponent->GetAnimInstance());
	if (AnimInstance)
	{
		AnimInstance->ResetPhysicsFlag();
		AnimInstance->PoseSnapshot = PoseSnapshot;
	}

	//Clear async creation flags
	if (AbovePhysicsAsset)
	{
		for (USkeletalBodySetup *BodySetup : AbovePhysicsAsset->SkeletalBodySetups)
		{
			if (BodySetup)
			{
				BodySetup->ClearInternalFlags(EInternalObjectFlags::Async);
			}
		}
	}

	if (BelowPhysicsAsset)
	{
		for (USkeletalBodySetup *BodySetup : BelowPhysicsAsset->SkeletalBodySetups)
		{
			if (BodySetup && BodySetup->HasAnyInternalFlags(EInternalObjectFlags::Async))
			{
				BodySetup->ClearInternalFlags(EInternalObjectFlags::Async);
			}
		}
	}

#if WITH_EDITOR
	if(AboveMesh->GetLODSettings() == nullptr || AboveMesh->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : AboveMesh LODSettings are nullptr/invalid"), TEXT(__FUNCTION__));
	}
	else if (AboveMesh->GetLODSettings()->HasAnyInternalFlags(EInternalObjectFlags::Async))
	{
		AboveMesh->GetLODSettings()->ClearInternalFlags(EInternalObjectFlags::Async);
	}
	
	if(BelowMesh->GetLODSettings() == nullptr || BelowMesh->IsValidLowLevel() == false)
	{
		UE_LOG(LogTemp, Error, TEXT("%s : BelowMesh LODSettings are nullptr/invalid"), TEXT(__FUNCTION__));
	}
	else if (BelowMesh->GetLODSettings()->HasAnyInternalFlags(EInternalObjectFlags::Async))
	{
		BelowMesh->GetLODSettings()->ClearInternalFlags(EInternalObjectFlags::Async);
	}
#endif


	//Pass the anim instance to the newly sliced gib's Slicer component
	SlicedGib->GetMeshSlicer()->GibAnimInstance = GibAnimInstance;

	double TotalSliceTime = FPlatformTime::Seconds() - SlicingData->SlicingStartTime;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9370,
		                                 10.f,
		                                 FColor::Red,
		                                 FString::Printf(TEXT("Done Slicing in %f seconds"), TotalSliceTime));
	}
	bIsCurrentlySlicing = false;
}

void USkeletalMeshSlicerComponent::CreateMeshSlice(USkeletalMesh *                       OutSkeletalMesh,
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
                                                   const bool                            bUseFullPrecisionUVs)
{

	//Heavily inspired by https://github.com/rdeioris/glTFRuntime and the way it creates skeletal meshes at runtime

	check(OutSkeletalMesh);
	OutSkeletalMesh->SetRefSkeleton(ReferenceSkeleton);
	OutSkeletalMesh->SetSkeleton(const_cast<USkeleton *>(Skeleton));
	OutSkeletalMesh->SetEnablePerPolyCollision(false); //todo: add arg to control that
	OutSkeletalMesh->NeverStream = true;
	OutSkeletalMesh->ResetLODInfo();
	OutSkeletalMesh->AllocateResourceForRendering();

	FSkeletalMeshLODRenderData *LODRenderData = nullptr;
	if(OutSkeletalMesh->GetResourceForRendering()->LODRenderData.IsEmpty())
	{
		LODRenderData = new FSkeletalMeshLODRenderData();
		OutSkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LODRenderData);
	}
	else
	{
		LODRenderData = &OutSkeletalMesh->GetResourceForRendering()->LODRenderData[0];
	}
	constexpr int32                       LODIndex = 0;
	const uint32                NumRenderSections = PerSectionVertexOffsets.Num();

	LODRenderData->RenderSections.SetNum(NumRenderSections);

	LODRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(VertexBuffer, bNeedsCPUAccess);
	LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(bUseFullPrecisionUVs);
	//todo: add color support
	LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(VertexBuffer, MaxTexCoords, bNeedsCPUAccess);

	int32 NumBones                   = ReferenceSkeleton.GetNum();
	LODRenderData->RequiredBones     = RequiredBones;
	LODRenderData->ActiveBoneIndices = ActiveBones;

	for (uint32 RenderSectionIdx = 0; RenderSectionIdx < NumRenderSections; ++RenderSectionIdx)
	{
		FSkelMeshRenderSection &MeshSection = LODRenderData->RenderSections[RenderSectionIdx];

		MeshSection.MaterialIndex   = RenderSectionIdx;
		MeshSection.BaseVertexIndex = PerSectionVertexOffsets[RenderSectionIdx];
		MeshSection.BaseIndex       = PerSectionIndexOffsets[RenderSectionIdx];
		if (RenderSectionIdx < NumRenderSections - 1)
		{
			MeshSection.NumVertices = PerSectionVertexOffsets[RenderSectionIdx + 1] - PerSectionVertexOffsets[
				RenderSectionIdx];
			MeshSection.NumTriangles = (PerSectionIndexOffsets[RenderSectionIdx + 1] - PerSectionIndexOffsets[
				RenderSectionIdx]) / 3;
		}
		else
		{
			MeshSection.NumVertices  = VertexBuffer.Num() - PerSectionVertexOffsets[RenderSectionIdx];
			MeshSection.NumTriangles = (IndexBuffer.Num() - PerSectionIndexOffsets[RenderSectionIdx]) / 3;

		}
		MeshSection.MaxBoneInfluences = FMath::Min(MaxBoneInfluences, (uint32)MAX_TOTAL_INFLUENCES);

		TMap<int32, TArray<int32>> OverlappingVertices;
		MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);

		MeshSection.BoneMap = PerSectionBoneMaps[RenderSectionIdx];
	}

	LODRenderData->SkinWeightVertexBuffer.SetNeedsCPUAccess(bNeedsCPUAccess);
	LODRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(MaxBoneInfluences > 0 ? MaxBoneInfluences : 1);
	LODRenderData->SkinWeightVertexBuffer.SetUse16BitBoneIndex(NumBones > MAX_uint8);
	LODRenderData->SkinWeightVertexBuffer.SetUse16BitBoneWeight(true);
	//todo: Make the interpolation function take into account 8 bit bone weights and weight accordingly
	LODRenderData->SkinWeightVertexBuffer = InSkinWeights;
	LODRenderData->SkinWeightVertexBuffer.RebuildLookupVertexBuffer();

	const int32 NumIndices = IndexBuffer.Num();
	LODRenderData->MultiSizeIndexContainer.RebuildIndexBuffer(NumIndices > MAX_uint16 ? sizeof(uint32) : sizeof(uint16),
	                                                          IndexBuffer);

	
	if(OutSkeletalMesh->GetLODInfoArray().IsEmpty())
	{
		OutSkeletalMesh->AddLODInfo();
	}
	
	FSkeletalMeshLODInfo& LODInfo = OutSkeletalMesh->GetLODInfoArray()[0];
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage      = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage   = 0.0f;
	LODInfo.BuildSettings.bRecomputeNormals            = false;
	LODInfo.BuildSettings.bRecomputeTangents           = false;
	LODInfo.BuildSettings.bUseFullPrecisionUVs         = bUseFullPrecisionUVs;
	LODInfo.LODHysteresis                              = 0.02f;
	LODInfo.ScreenSize                                 = 1.0f;

#if WITH_EDITOR
	FSkeletalMeshModel *ImportedResource = OutSkeletalMesh->GetImportedModel();
	if(ImportedResource->LODModels.IsEmpty())
	{
		ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
	}
	for (uint32 RenderSectionIndex = 0; RenderSectionIndex < NumRenderSections; ++RenderSectionIndex)
	{
		ImportedResource->LODModels[LODIndex].Sections.AddDefaulted();
		ImportedResource->LODModels[LODIndex].Sections[RenderSectionIndex].OriginalDataSectionIndex =
			RenderSectionIndex;
		ImportedResource->LODModels[LODIndex].UserSectionsData.Add(RenderSectionIndex);
	}
#endif
	//For now, I don't want to care about morph targets. If they become necessary later, I'll change it
	//todo: implement morph target support if necessary

	for (uint32 MatIndex = 0; MatIndex < NumRenderSections; ++MatIndex)
	{
		LODInfo.LODMaterialMap.Add(MatIndex);
		TArray<FSkeletalMaterial> &SkeletalMaterials = OutSkeletalMesh->GetMaterials();
		int32                      NewMatIndex       = SkeletalMaterials.Add(Materials[MatIndex]);

		SkeletalMaterials[NewMatIndex].UVChannelData.bInitialized = true;
		SkeletalMaterials[NewMatIndex].MaterialSlotName           = FName(
			FString::Printf(TEXT("LOD_%d_Section_%d_%s"), LODIndex, MatIndex, *Materials[MatIndex]->GetName()));
	}

#if WITH_EDITOR
	// USkeletalMeshLODSettings *LODSettings = NewObject<USkeletalMeshLODSettings>();
	USkeletalMeshLODSettings *LODSettings = OutSkeletalMesh->GetLODSettings();

	if (LODSettings == nullptr)
	{
		UE_LOG(LogTemp,
		       Warning,
		       TEXT("%s : Supplied mesh does not contain valid Pre-created LOD Settings"),
		       TEXT(__FUNCTION__));
		FGCScopeGuard GCGuard;
		LODSettings = NewObject<USkeletalMeshLODSettings>();
	}

	LODSettings->SetLODSettingsFromMesh(OutSkeletalMesh);
	OutSkeletalMesh->SetLODSettings(LODSettings);

	const FSkeletalMeshLODGroupSettings &SkeletalMeshLODGroupSettings = OutSkeletalMesh->GetLODSettings()->
		GetSettingsForLODLevel(LODIndex);
	OutSkeletalMesh->GetLODInfo(LODIndex)->BuildGUID = OutSkeletalMesh->GetLODInfo(LODIndex)->ComputeDeriveDataCacheKey(
		&SkeletalMeshLODGroupSettings);
	ImportedResource->LODModels[LODIndex].BuildStringID = ImportedResource->LODModels[LODIndex].
		GetLODModelDeriveDataKey();
#endif
	OutSkeletalMesh->CalculateInvRefMatrices();

	//original code does some shifting by root bone checks here, I don't think it applies to me, tho :D

	OutSkeletalMesh->SetImportedBounds(ImportedBounds);
	//original code sets the skeleton here (it creates a new one basically, I think), might want to reconsider that placement at the very top in case something goes bad
}

void USkeletalMeshSlicerComponent::CreateMeshSlicePhysicsAsset(TSharedRef<FSlicingData> SlicingData, bool bIsAboveMesh)
{
	if (SlicingData->bShouldTaskAbort)
	{
		return;
	}

	USkeletalMesh *SkeletalMesh = (bIsAboveMesh) ? SlicingData->AboveSkeletalMesh : SlicingData->BelowSkeletalMesh;
	UPhysicsAsset *PhysAsset = (bIsAboveMesh) ? SlicingData->AbovePhysicsAsset : SlicingData->BelowPhysicsAsset;
	UPhysicsAsset const *const OriginalPhysAsset = (SlicingData->OriginalSkeletalMeshComponent)
		? SlicingData->OriginalSkeletalMeshComponent->GetPhysicsAsset()
		: nullptr;
	TMap<FName, TArray<FKConvexElem>> &ConvexElems = (bIsAboveMesh)
		? SlicingData->AboveConvexElems
		: SlicingData->BelowConvexElems;
	if (!SkeletalMesh || !PhysAsset || !OriginalPhysAsset)
	{
		SlicingData->bShouldTaskAbort = true;
		return;
	}
	if (ConvexElems.IsEmpty())
	{
		// We do not signal to abort other tasks here, maybe some uses won't require a physics asset for the slice
		// return;
	}

	TMap<uint32, uint32> BaseToNewBodySetupIndexMap;
	PhysAsset->SkeletalBodySetups.Empty();
	PhysAsset->SkeletalBodySetups.Reserve(ConvexElems.Num());
	for (int32 BaseBodySetupIdx = 0; const USkeletalBodySetup *SourceBodySetup : OriginalPhysAsset->SkeletalBodySetups)
	{
		if (SourceBodySetup && ConvexElems.Contains(SourceBodySetup->BoneName))
		{
			// Don't forget to clean this up later, as this is most likely going to be done in an async task
			USkeletalBodySetup *NewBodySetup;
			{
				FGCScopeGuard GCGuard;
				NewBodySetup = NewObject<USkeletalBodySetup>();
			}
			NewBodySetup->CollisionTraceFlag = SourceBodySetup->CollisionTraceFlag;
			NewBodySetup->PhysicsType        = SourceBodySetup->PhysicsType;
			NewBodySetup->BoneName           = SourceBodySetup->BoneName;
			NewBodySetup->bConsiderForBounds = SourceBodySetup->bConsiderForBounds;
			// NewBodySetup->AggGeom.EmptyElements();
			//the physics convex elems should only be copied for the sliced parts, query will be copied for all
			{
				NewBodySetup->AggGeom.ConvexElems = ConvexElems[SourceBodySetup->BoneName];
				for (FKConvexElem &ConvexElem : NewBodySetup->AggGeom.ConvexElems)
				{
					ConvexElem.SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
				}
			}
			NewBodySetup->AggGeom.BoxElems = SourceBodySetup->AggGeom.BoxElems;
			for (int32 BoxElemIdx = 0; FKBoxElem &BoxElem : NewBodySetup->AggGeom.BoxElems)
			{
				BoxElem.SetCollisionEnabled(SourceBodySetup->AggGeom.BoxElems[BoxElemIdx].GetCollisionEnabled());
				// same as above :/
				BoxElem.SetContributeToMass(SourceBodySetup->AggGeom.BoxElems[BoxElemIdx].GetContributeToMass());
				++BoxElemIdx;
			}
			NewBodySetup->CollisionReponse        = SourceBodySetup->CollisionReponse;
			NewBodySetup->PhysicsType             = SourceBodySetup->PhysicsType;
			NewBodySetup->bMeshCollideAll         = SourceBodySetup->bMeshCollideAll;
			NewBodySetup->bSkipScaleFromAnimation = SourceBodySetup->bSkipScaleFromAnimation;
			NewBodySetup->CollisionTraceFlag      = SourceBodySetup->CollisionTraceFlag;
			int32 NewBodySetupIdx = PhysAsset->SkeletalBodySetups.Add(NewBodySetup);
			BaseToNewBodySetupIndexMap.Add(BaseBodySetupIdx, NewBodySetupIdx);

		}
		++BaseBodySetupIdx;
	}

	PhysAsset->ConstraintSetup.Empty();
	PhysAsset->ConstraintSetup.Reserve(OriginalPhysAsset->ConstraintSetup.Num());

	for (const UPhysicsConstraintTemplate *SourceConstraintTemplate : OriginalPhysAsset->ConstraintSetup)
	{
		const FConstraintInstance & SourceConstraintInstance = SourceConstraintTemplate->DefaultInstance;
		UPhysicsConstraintTemplate *NewConstraintTemplate    = NewObject<UPhysicsConstraintTemplate>();
		NewConstraintTemplate->DefaultInstance               = SourceConstraintInstance;
		NewConstraintTemplate->ProfileHandles                = SourceConstraintTemplate->ProfileHandles;
#if WITH_EDITOR
		NewConstraintTemplate->SetDefaultProfile(NewConstraintTemplate->DefaultInstance);
#endif
		PhysAsset->ConstraintSetup.Add(NewConstraintTemplate);
	}

	// Make sure no physics is running when this is called, CollisionDisableTable shouldn't be modified when physics is running!
	// Constructing the new collision disable table
	PhysAsset->CollisionDisableTable.Empty();
	//todo: reserve some space for the table to avoid unnecessary copying
	for (const auto &CollisionDisable : OriginalPhysAsset->CollisionDisableTable)
	{
		const FRigidBodyIndexPair &BodySetupIndexPair = CollisionDisable.Key;
		const bool                 bIsDisabled        = CollisionDisable.Value;

		// If only one/none of the body setups exist in the new physics asset, don't copy them
		if (BaseToNewBodySetupIndexMap.Contains(BodySetupIndexPair.Indices[0]) && BaseToNewBodySetupIndexMap.Contains(
			BodySetupIndexPair.Indices[1]))
		{
			int32 NewBodySetupIndexA = BaseToNewBodySetupIndexMap[BodySetupIndexPair.Indices[0]];
			int32 NewBodySetupIndexB = BaseToNewBodySetupIndexMap[BodySetupIndexPair.Indices[1]];

			PhysAsset->CollisionDisableTable.Add({NewBodySetupIndexA, NewBodySetupIndexB}, bIsDisabled);
		}
	}

	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
#if WITH_EDITOR
	PhysAsset->PreviewSkeletalMesh = (bIsAboveMesh) ? SlicingData->AboveSkeletalMesh : SlicingData->BelowSkeletalMesh;
#endif
}

void USkeletalMeshSlicerComponent::ReturnPooledAssets()
{
	if (USkeletalMeshPoolSubsystem *MeshPool = GetWorld()->GetSubsystem<USkeletalMeshPoolSubsystem>())
	{
		MeshPool->ReturnPooledSkeletalMesh(CurrentSkeletalMesh);
		MeshPool->ReturnPooledPhysicsAsset(CurrentPhysicsAsset);
		CurrentSkeletalMesh = nullptr;
		CurrentPhysicsAsset = nullptr;
	}
}
