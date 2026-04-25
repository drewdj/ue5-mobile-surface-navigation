#include "MobileSurfaceNavigationBuilder.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CompGeom/Delaunay2.h"
#include "Curve/PlanarComplex.h"
#include "Engine/StaticMesh.h"
#include "Logging/LogMacros.h"
#include "Polygon2.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfaceNavigationBuilder, Log, All);

namespace MobileSurfaceNavigation::Builder
{
	using namespace UE::Geometry;

	struct FVertexKey
	{
		int64 X = 0;
		int64 Y = 0;
		int64 Z = 0;

		bool operator==(const FVertexKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}

		friend uint32 GetTypeHash(const FVertexKey& Key)
		{
			return HashCombine(HashCombine(::GetTypeHash(Key.X), ::GetTypeHash(Key.Y)), ::GetTypeHash(Key.Z));
		}
	};

	struct FSourceTriangle
	{
		FIntVector VertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FIntVector NeighborTriangleIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FVector Normal = FVector::UpVector;
		FVector Center = FVector::ZeroVector;
		int32 RegionId = INDEX_NONE;
	};

	struct FBoundaryEdge
	{
		int32 StartVertex = INDEX_NONE;
		int32 EndVertex = INDEX_NONE;
	};

	struct FBoundaryLoopTemp
	{
		TArray<int32> VertexIndices;
		bool bClosed = false;
	};

	struct FRegionBoundaryEdgeInfo
	{
		int32 StartVertex = INDEX_NONE;
		int32 EndVertex = INDEX_NONE;
		bool bIsPhysicalBoundary = false;
	};

	struct FPlanarRegion
	{
		TArray<int32> TriangleIndices;
		TArray<int32> VertexIndices;
		FVector Normal = FVector::UpVector;
		FVector Origin = FVector::ZeroVector;
	};

	static FVector ComputeReferenceUpVector(const UStaticMeshComponent* SourceComponent, const USceneComponent* TargetSpaceComponent)
	{
		if (!SourceComponent || !TargetSpaceComponent)
		{
			return FVector::UpVector;
		}

		const FVector ReferenceUp = TargetSpaceComponent->GetComponentTransform().InverseTransformVectorNoScale(
			SourceComponent->GetComponentTransform().TransformVectorNoScale(FVector::UpVector));

		return ReferenceUp.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}

	static FVertexKey MakeVertexKey(const FVector& LocalPosition, const float Tolerance)
	{
		const float SafeTolerance = FMath::Max(Tolerance, KINDA_SMALL_NUMBER);
		return {
			FMath::RoundToInt64(LocalPosition.X / SafeTolerance),
			FMath::RoundToInt64(LocalPosition.Y / SafeTolerance),
			FMath::RoundToInt64(LocalPosition.Z / SafeTolerance)
		};
	}

	static void SetNeighbor(FIntVector& Neighbors, const int32 CornerIndex, const int32 NeighborTriangleIndex)
	{
		switch (CornerIndex)
		{
		case 0:
			Neighbors.X = NeighborTriangleIndex;
			break;
		case 1:
			Neighbors.Y = NeighborTriangleIndex;
			break;
		case 2:
			Neighbors.Z = NeighborTriangleIndex;
			break;
		default:
			break;
		}
	}

	static int32 GetTriangleVertexIndex(const FIntVector& TriangleVertexIndices, const int32 CornerIndex)
	{
		switch (CornerIndex)
		{
		case 0:
			return TriangleVertexIndices.X;
		case 1:
			return TriangleVertexIndices.Y;
		case 2:
			return TriangleVertexIndices.Z;
		default:
			return INDEX_NONE;
		}
	}

	static FIntPoint MakeSortedEdgeKey(const int32 VertexA, const int32 VertexB)
	{
		return (VertexA <= VertexB) ? FIntPoint(VertexA, VertexB) : FIntPoint(VertexB, VertexA);
	}

	static uint64 MakeDirectedEdgeKey(const int32 VertexA, const int32 VertexB)
	{
		return (static_cast<uint64>(static_cast<uint32>(VertexA)) << 32) | static_cast<uint32>(VertexB);
	}

	static FVector ComputeAverageNormal(const TArray<FSourceTriangle>& Triangles, const TArray<int32>& TriangleIndices)
	{
		FVector Accumulated = FVector::ZeroVector;
		for (const int32 TriangleIndex : TriangleIndices)
		{
			Accumulated += Triangles[TriangleIndex].Normal;
		}
		return Accumulated.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}

	static FVector ComputeAverageCenter(const TArray<FSourceTriangle>& Triangles, const TArray<int32>& TriangleIndices)
	{
		FVector Center = FVector::ZeroVector;
		for (const int32 TriangleIndex : TriangleIndices)
		{
			Center += Triangles[TriangleIndex].Center;
		}

		return TriangleIndices.Num() > 0 ? Center / static_cast<double>(TriangleIndices.Num()) : FVector::ZeroVector;
	}

	static bool IsTriangleCompatibleWithRegion(
		const FSourceTriangle& Triangle,
		const FPlanarRegion& Region,
		const TArray<FMobileSurfaceNavVertex>& Vertices,
		const FMobileSurfaceNavBuildSettings& Settings)
	{
		const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(Settings.PlanarRegionAngleToleranceDegrees));
		if (FVector::DotProduct(Triangle.Normal, Region.Normal) < CosThreshold)
		{
			return false;
		}

		const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
		for (const int32 VertexIndex : TriangleVertices)
		{
			const double DistanceToPlane = FMath::Abs(FVector::DotProduct(
				Vertices[VertexIndex].LocalPosition - Region.Origin,
				Region.Normal));
			if (DistanceToPlane > Settings.PlanarRegionDistanceTolerance)
			{
				return false;
			}
		}

		return true;
	}

	static void ExtractSourceTriangles(
		const UStaticMeshComponent* SourceComponent,
		const USceneComponent* TargetSpaceComponent,
		const FMobileSurfaceNavBuildSettings& Settings,
		FMobileSurfaceNavData& OutNavData,
		TArray<FSourceTriangle>& OutTriangles,
		FString& OutError)
	{
		const UStaticMesh* StaticMesh = SourceComponent->GetStaticMesh();
		const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;
		if (!RenderData || RenderData->LODResources.IsEmpty())
		{
			OutError = TEXT("Static mesh render data is unavailable.");
			return;
		}

		const FStaticMeshLODResources& LODResources = RenderData->LODResources[0];
		const int32 IndexCount = LODResources.IndexBuffer.GetNumIndices();
		if (IndexCount < 3)
		{
			OutError = TEXT("Static mesh contains no triangle indices in LOD 0.");
			return;
		}

		const FTransform SourceToTarget = TargetSpaceComponent
			? SourceComponent->GetComponentTransform().GetRelativeTransform(TargetSpaceComponent->GetComponentTransform())
			: FTransform::Identity;
		const FVector ReferenceUp = ComputeReferenceUpVector(SourceComponent, TargetSpaceComponent);

		TMap<FVertexKey, int32> VertexLookup;
		TMap<int32, int32> RawToUniqueVertexIndex;
		TMap<FIntPoint, int32> EdgeLookup;

		const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
		for (uint32 RawVertexIndex = 0; RawVertexIndex < PositionVertexBuffer.GetNumVertices(); ++RawVertexIndex)
		{
			const FVector SourceLocalPosition = static_cast<FVector>(PositionVertexBuffer.VertexPosition(RawVertexIndex));
			const FVector TargetLocalPosition = SourceToTarget.TransformPosition(SourceLocalPosition);
			const FVertexKey VertexKey = MakeVertexKey(TargetLocalPosition, Settings.VertexWeldTolerance);

			int32 UniqueVertexIndex = INDEX_NONE;
			if (const int32* ExistingIndex = VertexLookup.Find(VertexKey))
			{
				UniqueVertexIndex = *ExistingIndex;
			}
			else
			{
				FMobileSurfaceNavVertex& Vertex = OutNavData.Vertices.AddDefaulted_GetRef();
				Vertex.LocalPosition = TargetLocalPosition;
				UniqueVertexIndex = OutNavData.Vertices.Num() - 1;
				VertexLookup.Add(VertexKey, UniqueVertexIndex);
				OutNavData.LocalBounds += TargetLocalPosition;
			}

			RawToUniqueVertexIndex.Add(static_cast<int32>(RawVertexIndex), UniqueVertexIndex);
		}

		for (int32 IndexCursor = 0; IndexCursor + 2 < IndexCount; IndexCursor += 3)
		{
			const int32 UniqueIndex0 = RawToUniqueVertexIndex.FindChecked(static_cast<int32>(LODResources.IndexBuffer.GetIndex(IndexCursor)));
			int32 UniqueIndex1 = RawToUniqueVertexIndex.FindChecked(static_cast<int32>(LODResources.IndexBuffer.GetIndex(IndexCursor + 1)));
			int32 UniqueIndex2 = RawToUniqueVertexIndex.FindChecked(static_cast<int32>(LODResources.IndexBuffer.GetIndex(IndexCursor + 2)));

			if (UniqueIndex0 == UniqueIndex1 || UniqueIndex1 == UniqueIndex2 || UniqueIndex2 == UniqueIndex0)
			{
				continue;
			}

			const FVector Position0 = OutNavData.Vertices[UniqueIndex0].LocalPosition;
			const FVector Position1 = OutNavData.Vertices[UniqueIndex1].LocalPosition;
			const FVector Position2 = OutNavData.Vertices[UniqueIndex2].LocalPosition;
			FVector TriangleNormal = FVector::CrossProduct(Position1 - Position0, Position2 - Position0);
			if (TriangleNormal.SizeSquared() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			if (FVector::DotProduct(TriangleNormal, ReferenceUp) < 0.0f)
			{
				Swap(UniqueIndex1, UniqueIndex2);
				TriangleNormal *= -1.0f;
			}

			FSourceTriangle& Triangle = OutTriangles.AddDefaulted_GetRef();
			const int32 TriangleIndex = OutTriangles.Num() - 1;
			Triangle.VertexIndices = FIntVector(UniqueIndex0, UniqueIndex1, UniqueIndex2);
			Triangle.Normal = TriangleNormal.GetSafeNormal();
			Triangle.Center = (OutNavData.Vertices[UniqueIndex0].LocalPosition + OutNavData.Vertices[UniqueIndex1].LocalPosition + OutNavData.Vertices[UniqueIndex2].LocalPosition) / 3.0f;

			const int32 TriangleVertices[3] = { UniqueIndex0, UniqueIndex1, UniqueIndex2 };
			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const FIntPoint EdgeKey = MakeSortedEdgeKey(TriangleVertices[CornerIndex], TriangleVertices[(CornerIndex + 1) % 3]);
				if (int32* ExistingTriangleIndex = EdgeLookup.Find(EdgeKey))
				{
					const int32 OtherTriangleIndex = *ExistingTriangleIndex;
					FSourceTriangle& OtherTriangle = OutTriangles[OtherTriangleIndex];
					for (int32 OtherCornerIndex = 0; OtherCornerIndex < 3; ++OtherCornerIndex)
					{
						const int32 OtherA = GetTriangleVertexIndex(OtherTriangle.VertexIndices, OtherCornerIndex);
						const int32 OtherB = GetTriangleVertexIndex(OtherTriangle.VertexIndices, (OtherCornerIndex + 1) % 3);
						if ((OtherA == EdgeKey.X && OtherB == EdgeKey.Y) || (OtherA == EdgeKey.Y && OtherB == EdgeKey.X))
						{
							SetNeighbor(OtherTriangle.NeighborTriangleIndices, OtherCornerIndex, TriangleIndex);
							break;
						}
					}
					SetNeighbor(Triangle.NeighborTriangleIndices, CornerIndex, OtherTriangleIndex);
				}
				else
				{
					EdgeLookup.Add(EdgeKey, TriangleIndex);
				}
			}
		}
	}

	static void BuildPlanarRegions(
		TArray<FSourceTriangle>& Triangles,
		const TArray<FMobileSurfaceNavVertex>& Vertices,
		const FMobileSurfaceNavBuildSettings& Settings,
		TArray<FPlanarRegion>& OutRegions)
	{
		TArray<bool> Visited;
		Visited.Init(false, Triangles.Num());

		for (int32 SeedTriangleIndex = 0; SeedTriangleIndex < Triangles.Num(); ++SeedTriangleIndex)
		{
			if (Visited[SeedTriangleIndex])
			{
				continue;
			}

			FPlanarRegion& Region = OutRegions.AddDefaulted_GetRef();
			const int32 RegionId = OutRegions.Num() - 1;
			Region.Normal = Triangles[SeedTriangleIndex].Normal;
			Region.Origin = Triangles[SeedTriangleIndex].Center;

			TSet<int32> RegionVertices;
			TArray<int32> PendingTriangles;
			PendingTriangles.Add(SeedTriangleIndex);
			Visited[SeedTriangleIndex] = true;

			while (PendingTriangles.Num() > 0)
			{
				const int32 TriangleIndex = PendingTriangles.Pop(EAllowShrinking::No);
				FSourceTriangle& Triangle = Triangles[TriangleIndex];
				Triangle.RegionId = RegionId;
				Region.TriangleIndices.Add(TriangleIndex);
				RegionVertices.Add(Triangle.VertexIndices.X);
				RegionVertices.Add(Triangle.VertexIndices.Y);
				RegionVertices.Add(Triangle.VertexIndices.Z);

				const int32 NeighborIndices[3] =
				{
					Triangle.NeighborTriangleIndices.X,
					Triangle.NeighborTriangleIndices.Y,
					Triangle.NeighborTriangleIndices.Z
				};

				for (const int32 NeighborIndex : NeighborIndices)
				{
					if (NeighborIndex == INDEX_NONE || Visited[NeighborIndex])
					{
						continue;
					}

					if (IsTriangleCompatibleWithRegion(Triangles[NeighborIndex], Region, Vertices, Settings))
					{
						Visited[NeighborIndex] = true;
						PendingTriangles.Add(NeighborIndex);
					}
				}
			}

			for (const int32 VertexIndex : RegionVertices)
			{
				Region.VertexIndices.Add(VertexIndex);
			}
			Region.VertexIndices.Sort();
			Region.Normal = ComputeAverageNormal(Triangles, Region.TriangleIndices);
			Region.Origin = ComputeAverageCenter(Triangles, Region.TriangleIndices);
		}
	}

	static void BuildLoopsFromEdges(const TArray<FBoundaryEdge>& BoundaryEdges, TArray<FBoundaryLoopTemp>& OutLoops)
	{
		TMap<int32, TArray<int32>> OutgoingEdgesByVertex;
		for (int32 EdgeIndex = 0; EdgeIndex < BoundaryEdges.Num(); ++EdgeIndex)
		{
			OutgoingEdgesByVertex.FindOrAdd(BoundaryEdges[EdgeIndex].StartVertex).Add(EdgeIndex);
		}

		TArray<bool> UsedEdges;
		UsedEdges.Init(false, BoundaryEdges.Num());

		for (int32 EdgeIndex = 0; EdgeIndex < BoundaryEdges.Num(); ++EdgeIndex)
		{
			if (UsedEdges[EdgeIndex])
			{
				continue;
			}

			FBoundaryLoopTemp& Loop = OutLoops.AddDefaulted_GetRef();
			const int32 StartVertex = BoundaryEdges[EdgeIndex].StartVertex;
			int32 CurrentEdgeIndex = EdgeIndex;

			while (CurrentEdgeIndex != INDEX_NONE && !UsedEdges[CurrentEdgeIndex])
			{
				UsedEdges[CurrentEdgeIndex] = true;
				const FBoundaryEdge& Edge = BoundaryEdges[CurrentEdgeIndex];
				Loop.VertexIndices.Add(Edge.StartVertex);

				if (Edge.EndVertex == StartVertex)
				{
					Loop.bClosed = true;
					break;
				}

				CurrentEdgeIndex = INDEX_NONE;
				if (TArray<int32>* Candidates = OutgoingEdgesByVertex.Find(Edge.EndVertex))
				{
					for (const int32 CandidateIndex : *Candidates)
					{
						if (!UsedEdges[CandidateIndex])
						{
							CurrentEdgeIndex = CandidateIndex;
							break;
						}
					}
				}
			}

			if (!Loop.bClosed || Loop.VertexIndices.Num() < 3)
			{
				OutLoops.Pop(EAllowShrinking::No);
			}
		}
	}

	static double DistancePointToSegment2D(const FVector2d& Point, const FVector2d& SegmentStart, const FVector2d& SegmentEnd)
	{
		const FVector2d Segment = SegmentEnd - SegmentStart;
		const double SegmentLengthSquared = Segment.SquaredLength();
		if (SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Distance(Point, SegmentStart);
		}

		const double T = FMath::Clamp(FVector2d::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSquared, 0.0, 1.0);
		const FVector2d ClosestPoint = SegmentStart + Segment * T;
		return Distance(Point, ClosestPoint);
	}

	static bool SimplifyLoopInPlace(
		FBoundaryLoopTemp& Loop,
		const TArray<FVector2d>& RegionVertices2D,
		const float SimplificationTolerance)
	{
		if (Loop.VertexIndices.Num() < 3 || SimplificationTolerance <= 0.0f)
		{
			return false;
		}

		bool bChanged = false;
		bool bRemovedAnyVertex = true;

		while (bRemovedAnyVertex && Loop.VertexIndices.Num() > 3)
		{
			bRemovedAnyVertex = false;

			for (int32 VertexIndex = 0; VertexIndex < Loop.VertexIndices.Num(); ++VertexIndex)
			{
				const int32 PrevIndex = (VertexIndex - 1 + Loop.VertexIndices.Num()) % Loop.VertexIndices.Num();
				const int32 NextIndex = (VertexIndex + 1) % Loop.VertexIndices.Num();

				const FVector2d PrevPoint = RegionVertices2D[Loop.VertexIndices[PrevIndex]];
				const FVector2d CurrentPoint = RegionVertices2D[Loop.VertexIndices[VertexIndex]];
				const FVector2d NextPoint = RegionVertices2D[Loop.VertexIndices[NextIndex]];

				const double DistanceToSegment = DistancePointToSegment2D(CurrentPoint, PrevPoint, NextPoint);
				if (DistanceToSegment <= SimplificationTolerance)
				{
					Loop.VertexIndices.RemoveAt(VertexIndex);
					bRemovedAnyVertex = true;
					bChanged = true;
					break;
				}
			}
		}

		return bChanged;
	}

	static TArray<FBoundaryEdge> CollectRegionBoundaryEdges(
		const FPlanarRegion& Region,
		const int32 RegionId,
		const TArray<FSourceTriangle>& Triangles,
		const TMap<int32, int32>& GlobalToRegionVertex)
	{
		TArray<FBoundaryEdge> BoundaryEdges;

		for (const int32 TriangleIndex : Region.TriangleIndices)
		{
			const FSourceTriangle& Triangle = Triangles[TriangleIndex];
			const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
			const int32 NeighborIndices[3] =
			{
				Triangle.NeighborTriangleIndices.X,
				Triangle.NeighborTriangleIndices.Y,
				Triangle.NeighborTriangleIndices.Z
			};

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 NeighborTriangleIndex = NeighborIndices[CornerIndex];
				const bool bIsPatchBoundary = NeighborTriangleIndex == INDEX_NONE || Triangles[NeighborTriangleIndex].RegionId != RegionId;
				if (!bIsPatchBoundary)
				{
					continue;
				}

				const int32* StartVertex = GlobalToRegionVertex.Find(TriangleVertices[CornerIndex]);
				const int32* EndVertex = GlobalToRegionVertex.Find(TriangleVertices[(CornerIndex + 1) % 3]);
				if (!StartVertex || !EndVertex)
				{
					continue;
				}

				FBoundaryEdge& Edge = BoundaryEdges.AddDefaulted_GetRef();
				Edge.StartVertex = *StartVertex;
				Edge.EndVertex = *EndVertex;
			}
		}

		return BoundaryEdges;
	}

	static void CollectRegionBoundaryEdgeInfosFromNavData(
		const FMobileSurfaceNavData& NavData,
		const int32 RegionId,
		TArray<FRegionBoundaryEdgeInfo>& OutBoundaryEdges)
	{
		OutBoundaryEdges.Reset();

		if (!NavData.Regions.IsValidIndex(RegionId))
		{
			return;
		}

		for (const int32 TriangleIndex : NavData.Regions[RegionId].TriangleIndices)
		{
			if (!NavData.Triangles.IsValidIndex(TriangleIndex))
			{
				continue;
			}

			const FMobileSurfaceNavTriangle& Triangle = NavData.Triangles[TriangleIndex];
			const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
			const int32 NeighborIndices[3] =
			{
				Triangle.NeighborTriangleIndices.X,
				Triangle.NeighborTriangleIndices.Y,
				Triangle.NeighborTriangleIndices.Z
			};

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 NeighborTriangleIndex = NeighborIndices[CornerIndex];
				const bool bIsRegionBoundary = NeighborTriangleIndex == INDEX_NONE ||
					(!NavData.Triangles.IsValidIndex(NeighborTriangleIndex) || NavData.Triangles[NeighborTriangleIndex].RegionId != RegionId);
				if (!bIsRegionBoundary)
				{
					continue;
				}

				FRegionBoundaryEdgeInfo& EdgeInfo = OutBoundaryEdges.AddDefaulted_GetRef();
				EdgeInfo.StartVertex = TriangleVertices[CornerIndex];
				EdgeInfo.EndVertex = TriangleVertices[(CornerIndex + 1) % 3];
				EdgeInfo.bIsPhysicalBoundary = NeighborTriangleIndex == INDEX_NONE;
			}
		}
	}

	static void BuildGlobalBoundaryLoops(const TArray<FSourceTriangle>& Triangles, FMobileSurfaceNavData& OutNavData)
	{
		TArray<FBoundaryEdge> BoundaryEdges;

		for (const FSourceTriangle& Triangle : Triangles)
		{
			const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
			const int32 NeighborIndices[3] =
			{
				Triangle.NeighborTriangleIndices.X,
				Triangle.NeighborTriangleIndices.Y,
				Triangle.NeighborTriangleIndices.Z
			};

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (NeighborIndices[CornerIndex] != INDEX_NONE)
				{
					continue;
				}

				FBoundaryEdge& Edge = BoundaryEdges.AddDefaulted_GetRef();
				Edge.StartVertex = TriangleVertices[CornerIndex];
				Edge.EndVertex = TriangleVertices[(CornerIndex + 1) % 3];
			}
		}

		TArray<FBoundaryLoopTemp> Loops;
		BuildLoopsFromEdges(BoundaryEdges, Loops);

		for (const FBoundaryLoopTemp& Loop : Loops)
		{
			FMobileSurfaceNavBoundaryLoop& BoundaryLoop = OutNavData.BoundaryLoops.AddDefaulted_GetRef();
			BoundaryLoop.VertexIndices = Loop.VertexIndices;
			BoundaryLoop.bClosed = Loop.bClosed;
			BoundaryLoop.Kind = EMobileSurfaceBoundaryKind::Outer;
		}
	}

	static float ComputeBoundaryClearance(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition)
	{
		double BestDistanceSquared = TNumericLimits<double>::Max();
		bool bHasBoundary = false;

		for (const FMobileSurfaceNavEdge& BoundaryEdge : NavData.Edges)
		{
			if (!BoundaryEdge.bIsBoundary)
			{
				continue;
			}

			bHasBoundary = true;
			const FVector A = NavData.Vertices[BoundaryEdge.VertexIndices.X].LocalPosition;
			const FVector B = NavData.Vertices[BoundaryEdge.VertexIndices.Y].LocalPosition;
			BestDistanceSquared = FMath::Min(BestDistanceSquared, static_cast<double>(FMath::PointDistToSegmentSquared(LocalPosition, A, B)));
		}

		return bHasBoundary ? static_cast<float>(FMath::Sqrt(BestDistanceSquared)) : TNumericLimits<float>::Max();
	}

	static float ComputeTriangleUsableBoundaryClearance(const FMobileSurfaceNavData& NavData, const FMobileSurfaceNavTriangle& Triangle)
	{
		const FVector A = NavData.Vertices[Triangle.VertexIndices.X].LocalPosition;
		const FVector B = NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition;
		const FVector C = NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition;
		const FVector Centroid = Triangle.Center;
		const FVector EdgeMidAB = (A + B) * 0.5f;
		const FVector EdgeMidBC = (B + C) * 0.5f;
		const FVector EdgeMidCA = (C + A) * 0.5f;

		// Sample only inside the triangle. Using boundary vertices/midpoints makes every CDT triangle
		// look invalid because those points often lie exactly on the mesh border.
		const FVector InteriorSamples[] =
		{
			Centroid,
			FMath::Lerp(Centroid, A, 0.35f),
			FMath::Lerp(Centroid, B, 0.35f),
			FMath::Lerp(Centroid, C, 0.35f),
			FMath::Lerp(Centroid, EdgeMidAB, 0.5f),
			FMath::Lerp(Centroid, EdgeMidBC, 0.5f),
			FMath::Lerp(Centroid, EdgeMidCA, 0.5f)
		};

		float MinClearance = TNumericLimits<float>::Max();
		for (const FVector& Sample : InteriorSamples)
		{
			MinClearance = FMath::Min(MinClearance, ComputeBoundaryClearance(NavData, Sample));
		}

		return MinClearance;
	}

	static void BuildGlobalBoundaryLoopsFromNavTriangles(FMobileSurfaceNavData& OutNavData)
	{
		TArray<FBoundaryEdge> BoundaryEdges;

		for (const FMobileSurfaceNavTriangle& Triangle : OutNavData.Triangles)
		{
			const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
			const int32 NeighborIndices[3] =
			{
				Triangle.NeighborTriangleIndices.X,
				Triangle.NeighborTriangleIndices.Y,
				Triangle.NeighborTriangleIndices.Z
			};

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (NeighborIndices[CornerIndex] != INDEX_NONE)
				{
					continue;
				}

				FBoundaryEdge& Edge = BoundaryEdges.AddDefaulted_GetRef();
				Edge.StartVertex = TriangleVertices[CornerIndex];
				Edge.EndVertex = TriangleVertices[(CornerIndex + 1) % 3];
			}
		}

		TArray<FBoundaryLoopTemp> Loops;
		BuildLoopsFromEdges(BoundaryEdges, Loops);

		OutNavData.BoundaryLoops.Reset();
		for (const FBoundaryLoopTemp& Loop : Loops)
		{
			FMobileSurfaceNavBoundaryLoop& BoundaryLoop = OutNavData.BoundaryLoops.AddDefaulted_GetRef();
			BoundaryLoop.VertexIndices = Loop.VertexIndices;
			BoundaryLoop.bClosed = Loop.bClosed;
			BoundaryLoop.Kind = EMobileSurfaceBoundaryKind::Outer;
		}
	}

	static bool IntersectLines2D(
		const FVector2d& PointA,
		const FVector2d& DirA,
		const FVector2d& PointB,
		const FVector2d& DirB,
		FVector2d& OutIntersection)
	{
		const double Determinant = (DirA.X * DirB.Y) - (DirA.Y * DirB.X);
		if (FMath::Abs(Determinant) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector2d Delta = PointB - PointA;
		const double T = ((Delta.X * DirB.Y) - (Delta.Y * DirB.X)) / Determinant;
		OutIntersection = PointA + (DirA * T);
		return true;
	}

	static void RebuildFinalEdgesAndAdjacency(FMobileSurfaceNavData& OutNavData)
	{
		OutNavData.Edges.Reset();
		OutNavData.Portals.Reset();
		OutNavData.TriangleAdjacency.Reset();
		OutNavData.TriangleBounds.Reset();
		OutNavData.TriangleAdjacency.SetNum(OutNavData.Triangles.Num());
		OutNavData.TriangleBounds.SetNum(OutNavData.Triangles.Num());
		for (FMobileSurfaceNavTriangle& Triangle : OutNavData.Triangles)
		{
			Triangle.NeighborTriangleIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		}

		TMap<FIntPoint, int32> EdgeLookup;

		for (int32 TriangleIndex = 0; TriangleIndex < OutNavData.Triangles.Num(); ++TriangleIndex)
		{
			FMobileSurfaceNavTriangle& Triangle = OutNavData.Triangles[TriangleIndex];
			const int32 TriangleVertices[3] = { Triangle.VertexIndices.X, Triangle.VertexIndices.Y, Triangle.VertexIndices.Z };
			FBox TriangleBounds(EForceInit::ForceInit);
			for (const int32 VertexIndex : TriangleVertices)
			{
				if (OutNavData.Vertices.IsValidIndex(VertexIndex))
				{
					TriangleBounds += OutNavData.Vertices[VertexIndex].LocalPosition;
				}
			}
			OutNavData.TriangleBounds[TriangleIndex].LocalBounds = TriangleBounds;

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 EdgeVertexA = TriangleVertices[CornerIndex];
				const int32 EdgeVertexB = TriangleVertices[(CornerIndex + 1) % 3];
				const FIntPoint EdgeKey = MakeSortedEdgeKey(EdgeVertexA, EdgeVertexB);

				if (int32* ExistingEdgeIndex = EdgeLookup.Find(EdgeKey))
				{
					FMobileSurfaceNavEdge& Edge = OutNavData.Edges[*ExistingEdgeIndex];
					const int32 OtherTriangleIndex = Edge.TriangleIndices.X;
					Edge.TriangleIndices.Y = TriangleIndex;
					SetNeighbor(Triangle.NeighborTriangleIndices, CornerIndex, OtherTriangleIndex);

					if (OtherTriangleIndex != INDEX_NONE)
					{
						for (int32 OtherCornerIndex = 0; OtherCornerIndex < 3; ++OtherCornerIndex)
						{
							const FMobileSurfaceNavTriangle& OtherTriangle = OutNavData.Triangles[OtherTriangleIndex];
							const int32 OtherA = GetTriangleVertexIndex(OtherTriangle.VertexIndices, OtherCornerIndex);
							const int32 OtherB = GetTriangleVertexIndex(OtherTriangle.VertexIndices, (OtherCornerIndex + 1) % 3);
							if ((OtherA == EdgeKey.X && OtherB == EdgeKey.Y) || (OtherA == EdgeKey.Y && OtherB == EdgeKey.X))
							{
								SetNeighbor(OutNavData.Triangles[OtherTriangleIndex].NeighborTriangleIndices, OtherCornerIndex, TriangleIndex);
								break;
							}
						}
					}
				}
				else
				{
					FMobileSurfaceNavEdge& Edge = OutNavData.Edges.AddDefaulted_GetRef();
					Edge.VertexIndices = FIntPoint(EdgeVertexA, EdgeVertexB);
					Edge.TriangleIndices = FIntPoint(TriangleIndex, INDEX_NONE);
					EdgeLookup.Add(EdgeKey, OutNavData.Edges.Num() - 1);
				}
			}
		}

		for (FMobileSurfaceNavEdge& Edge : OutNavData.Edges)
		{
			Edge.bIsBoundary = (Edge.TriangleIndices.X == INDEX_NONE || Edge.TriangleIndices.Y == INDEX_NONE);
		}

		for (FMobileSurfaceNavEdge& Edge : OutNavData.Edges)
		{
			if (!Edge.bIsBoundary)
			{
				const int32 PortalIndex = OutNavData.Portals.Num();
				FMobileSurfaceNavPortal& Portal = OutNavData.Portals.AddDefaulted_GetRef();
				Portal.TriangleA = Edge.TriangleIndices.X;
				Portal.TriangleB = Edge.TriangleIndices.Y;
				Portal.VertexIndices = Edge.VertexIndices;
				Portal.LeftPoint = OutNavData.Vertices[Edge.VertexIndices.X].LocalPosition;
				Portal.RightPoint = OutNavData.Vertices[Edge.VertexIndices.Y].LocalPosition;
				Portal.Center = (Portal.LeftPoint + Portal.RightPoint) * 0.5f;
				Portal.Width = FVector::Distance(Portal.LeftPoint, Portal.RightPoint);
				const float PortalBoundaryClearance = ComputeBoundaryClearance(OutNavData, Portal.Center);

				if (OutNavData.Triangles.IsValidIndex(Portal.TriangleA) && OutNavData.Triangles.IsValidIndex(Portal.TriangleB))
				{
					FMobileSurfaceTriangleAdjacency& AdjacencyA = OutNavData.TriangleAdjacency[Portal.TriangleA].Neighbors.AddDefaulted_GetRef();
					AdjacencyA.NeighborTriangleIndex = Portal.TriangleB;
					AdjacencyA.PortalIndex = PortalIndex;
					AdjacencyA.TravelCost = FVector::Distance(OutNavData.Triangles[Portal.TriangleA].Center, OutNavData.Triangles[Portal.TriangleB].Center);
					AdjacencyA.PortalWidth = Portal.Width;
					AdjacencyA.BoundaryClearance = PortalBoundaryClearance;

					FMobileSurfaceTriangleAdjacency& AdjacencyB = OutNavData.TriangleAdjacency[Portal.TriangleB].Neighbors.AddDefaulted_GetRef();
					AdjacencyB.NeighborTriangleIndex = Portal.TriangleA;
					AdjacencyB.PortalIndex = PortalIndex;
					AdjacencyB.TravelCost = AdjacencyA.TravelCost;
					AdjacencyB.PortalWidth = Portal.Width;
					AdjacencyB.BoundaryClearance = PortalBoundaryClearance;
				}
			}
		}

		OutNavData.RegionRuntimeStates.SetNum(OutNavData.Regions.Num());
		OutNavData.PortalRuntimeStates.SetNum(OutNavData.Portals.Num());
	}

	static void WeldCoincidentVertices(FMobileSurfaceNavData& InOutNavData, const float PositionTolerance)
	{
		if (InOutNavData.Vertices.IsEmpty())
		{
			return;
		}

		const float SafeTolerance = FMath::Max(PositionTolerance, KINDA_SMALL_NUMBER);
		TMap<FVertexKey, int32> CanonicalVertexByKey;
		TArray<FMobileSurfaceNavVertex> WeldedVertices;
		WeldedVertices.Reserve(InOutNavData.Vertices.Num());

		TArray<int32> OldToNewVertexIndex;
		OldToNewVertexIndex.SetNum(InOutNavData.Vertices.Num());

		for (int32 VertexIndex = 0; VertexIndex < InOutNavData.Vertices.Num(); ++VertexIndex)
		{
			const FVector& LocalPosition = InOutNavData.Vertices[VertexIndex].LocalPosition;
			const FVertexKey VertexKey = MakeVertexKey(LocalPosition, SafeTolerance);

			if (const int32* ExistingVertexIndex = CanonicalVertexByKey.Find(VertexKey))
			{
				OldToNewVertexIndex[VertexIndex] = *ExistingVertexIndex;
				continue;
			}

			const int32 NewVertexIndex = WeldedVertices.Add(InOutNavData.Vertices[VertexIndex]);
			CanonicalVertexByKey.Add(VertexKey, NewVertexIndex);
			OldToNewVertexIndex[VertexIndex] = NewVertexIndex;
		}

		for (FMobileSurfaceNavTriangle& Triangle : InOutNavData.Triangles)
		{
			Triangle.VertexIndices.X = OldToNewVertexIndex.IsValidIndex(Triangle.VertexIndices.X) ? OldToNewVertexIndex[Triangle.VertexIndices.X] : Triangle.VertexIndices.X;
			Triangle.VertexIndices.Y = OldToNewVertexIndex.IsValidIndex(Triangle.VertexIndices.Y) ? OldToNewVertexIndex[Triangle.VertexIndices.Y] : Triangle.VertexIndices.Y;
			Triangle.VertexIndices.Z = OldToNewVertexIndex.IsValidIndex(Triangle.VertexIndices.Z) ? OldToNewVertexIndex[Triangle.VertexIndices.Z] : Triangle.VertexIndices.Z;
		}

		for (FMobileSurfaceNavBoundaryLoop& Loop : InOutNavData.BoundaryLoops)
		{
			for (int32& VertexIndex : Loop.VertexIndices)
			{
				if (OldToNewVertexIndex.IsValidIndex(VertexIndex))
				{
					VertexIndex = OldToNewVertexIndex[VertexIndex];
				}
			}
		}

		InOutNavData.Vertices = MoveTemp(WeldedVertices);
	}

	static bool TriangulateRegion(
		const FPlanarRegion& Region,
		const int32 RegionId,
		const TArray<FSourceTriangle>& SourceTriangles,
		const FMobileSurfaceNavBuildSettings& Settings,
		FMobileSurfaceNavData& OutNavData,
		FString& OutError)
	{
		FVector BasisX = FVector::CrossProduct(Region.Normal, FVector::UpVector);
		if (!BasisX.Normalize())
		{
			BasisX = FVector::CrossProduct(Region.Normal, FVector::RightVector);
			BasisX.Normalize();
		}
		const FVector BasisY = FVector::CrossProduct(Region.Normal, BasisX).GetSafeNormal();

		TMap<int32, int32> GlobalToRegionVertex;
		TArray<int32> RegionToGlobalVertex;
		TArray<FVector2d> RegionVertices2D;

		for (const int32 GlobalVertexIndex : Region.VertexIndices)
		{
			const int32 RegionVertexIndex = RegionToGlobalVertex.Add(GlobalVertexIndex);
			GlobalToRegionVertex.Add(GlobalVertexIndex, RegionVertexIndex);

			const FVector Delta = OutNavData.Vertices[GlobalVertexIndex].LocalPosition - Region.Origin;
			RegionVertices2D.Add(FVector2d(FVector::DotProduct(Delta, BasisX), FVector::DotProduct(Delta, BasisY)));
		}

		const TArray<FBoundaryEdge> BoundaryEdges = CollectRegionBoundaryEdges(Region, RegionId, SourceTriangles, GlobalToRegionVertex);
		TArray<FBoundaryLoopTemp> RegionLoops;
		BuildLoopsFromEdges(BoundaryEdges, RegionLoops);
		int32 OriginalLoopVertexCount = 0;
		for (const FBoundaryLoopTemp& Loop : RegionLoops)
		{
			OriginalLoopVertexCount += Loop.VertexIndices.Num();
		}

		for (FBoundaryLoopTemp& Loop : RegionLoops)
		{
			SimplifyLoopInPlace(Loop, RegionVertices2D, Settings.BoundarySimplificationTolerance);
		}
		int32 SimplifiedLoopVertexCount = 0;
		for (const FBoundaryLoopTemp& Loop : RegionLoops)
		{
			SimplifiedLoopVertexCount += Loop.VertexIndices.Num();
		}

		TSet<int32> BoundaryVertexSet;
		for (const FBoundaryLoopTemp& Loop : RegionLoops)
		{
			for (const int32 RegionVertexIndex : Loop.VertexIndices)
			{
				BoundaryVertexSet.Add(RegionVertexIndex);
			}
		}

		TArray<int32> LoopVertexToCdtVertex;
		LoopVertexToCdtVertex.Init(INDEX_NONE, RegionToGlobalVertex.Num());

		TArray<int32> CdtToGlobalVertex;
		TArray<FVector2d> CdtVertices2D;
		CdtToGlobalVertex.Reserve(BoundaryVertexSet.Num());
		CdtVertices2D.Reserve(BoundaryVertexSet.Num());

		for (const int32 RegionVertexIndex : BoundaryVertexSet)
		{
			const int32 GlobalVertexIndex = RegionToGlobalVertex[RegionVertexIndex];
			const int32 CdtVertexIndex = CdtToGlobalVertex.Add(GlobalVertexIndex);
			LoopVertexToCdtVertex[RegionVertexIndex] = CdtVertexIndex;

			const FVector Delta = OutNavData.Vertices[GlobalVertexIndex].LocalPosition - Region.Origin;
			CdtVertices2D.Add(FVector2d(FVector::DotProduct(Delta, BasisX), FVector::DotProduct(Delta, BasisY)));
		}

		UE_LOG(
			LogMobileSurfaceNavigationBuilder,
			Log,
			TEXT("Region %d: sourceTriangles=%d regionVertices=%d boundaryVertices=%d loopVertices=%d simplifiedLoopVertices=%d boundaryEdges=%d loops=%d normal=(%.3f, %.3f, %.3f)"),
			RegionId,
			Region.TriangleIndices.Num(),
			Region.VertexIndices.Num(),
			CdtVertices2D.Num(),
			OriginalLoopVertexCount,
			SimplifiedLoopVertexCount,
			BoundaryEdges.Num(),
			RegionLoops.Num(),
			Region.Normal.X,
			Region.Normal.Y,
			Region.Normal.Z);

		if (RegionLoops.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Region %d has no closed boundary loops for CDT."), RegionId);
			UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
			return false;
		}

		FPlanarComplexd PlanarComplex;
		PlanarComplex.bTrustOrientations = false;
		TArray<FPolygon2d> LoopPolygons;

		for (const FBoundaryLoopTemp& Loop : RegionLoops)
		{
			TArray<FVector2d> PolygonVertices;
			PolygonVertices.Reserve(Loop.VertexIndices.Num());
			for (const int32 RegionVertexIndex : Loop.VertexIndices)
			{
				const int32 CdtVertexIndex = LoopVertexToCdtVertex[RegionVertexIndex];
				if (CdtVertexIndex == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Region %d has a loop vertex that was not mapped into CDT input."), RegionId);
					UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
					return false;
				}

				PolygonVertices.Add(CdtVertices2D[CdtVertexIndex]);
			}
			LoopPolygons.Emplace(PolygonVertices);
			PlanarComplex.Polygons.Add(LoopPolygons.Last());
		}

		PlanarComplex.FindSolidRegions();
		UE_LOG(
			LogMobileSurfaceNavigationBuilder,
			Log,
			TEXT("Region %d: planarComplexPolygons=%d solids=%d"),
			RegionId,
			PlanarComplex.Polygons.Num(),
			PlanarComplex.Solids.Num());

		if (PlanarComplex.Solids.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Region %d could not be converted into a valid planar polygon set."), RegionId);
			UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
			return false;
		}

		TArray<FIndex2i> ConstrainedEdges;
		ConstrainedEdges.Reserve(BoundaryEdges.Num());

		for (const FPlanarComplexd::FPolygonNesting& Solid : PlanarComplex.Solids)
		{
			FPolygon2d Outer = LoopPolygons[Solid.OuterIndex];
			if (Outer.IsClockwise())
			{
				Outer.Reverse();
			}

			for (int32 Last = Outer.VertexCount() - 1, Idx = 0; Idx < Outer.VertexCount(); Last = Idx++)
			{
				ConstrainedEdges.Add(FIndex2i(
					LoopVertexToCdtVertex[RegionLoops[Solid.OuterIndex].VertexIndices[Last]],
					LoopVertexToCdtVertex[RegionLoops[Solid.OuterIndex].VertexIndices[Idx]]));
			}

			for (const int32 HoleIndex : Solid.HoleIndices)
			{
				FPolygon2d Hole = LoopPolygons[HoleIndex];
				if (!Hole.IsClockwise())
				{
					Hole.Reverse();
				}

				for (int32 Last = Hole.VertexCount() - 1, Idx = 0; Idx < Hole.VertexCount(); Last = Idx++)
				{
					ConstrainedEdges.Add(FIndex2i(
						LoopVertexToCdtVertex[RegionLoops[HoleIndex].VertexIndices[Last]],
						LoopVertexToCdtVertex[RegionLoops[HoleIndex].VertexIndices[Idx]]));
				}
			}
		}

		FDelaunay2 Delaunay;
		Delaunay.bAutomaticallyFixEdgesToDuplicateVertices = true;
		Delaunay.bValidateEdges = true;

		if (!Delaunay.Triangulate(CdtVertices2D, ConstrainedEdges))
		{
			OutError = FString::Printf(TEXT("CDT failed for region %d. Result=%d"), RegionId, static_cast<int32>(Delaunay.GetResult()));
			UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
			return false;
		}

		const TArray<FIndex3i> RegionTriangles = Delaunay.GetFilledTriangles(ConstrainedEdges, FDelaunay2::EFillMode::PositiveWinding);
		UE_LOG(
			LogMobileSurfaceNavigationBuilder,
			Log,
			TEXT("Region %d: constrainedEdges=%d delaunayTriangles=%d filledTriangles=%d"),
			RegionId,
			ConstrainedEdges.Num(),
			Delaunay.GetTriangles().Num(),
			RegionTriangles.Num());

		if (RegionTriangles.Num() == 0)
		{
			OutError = FString::Printf(TEXT("CDT produced no filled triangles for region %d."), RegionId);
			UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
			return false;
		}

		FMobileSurfaceNavRegion& NavRegion = OutNavData.Regions[RegionId];
		NavRegion.AverageNormal = Region.Normal;

		for (const FIndex3i& RegionTriangle : RegionTriangles)
		{
			const int32 GlobalVertex0 = CdtToGlobalVertex[RegionTriangle.A];
			int32 GlobalVertex1 = CdtToGlobalVertex[RegionTriangle.B];
			int32 GlobalVertex2 = CdtToGlobalVertex[RegionTriangle.C];

			const FVector Position0 = OutNavData.Vertices[GlobalVertex0].LocalPosition;
			const FVector Position1 = OutNavData.Vertices[GlobalVertex1].LocalPosition;
			const FVector Position2 = OutNavData.Vertices[GlobalVertex2].LocalPosition;
			FVector TriangleNormal = FVector::CrossProduct(Position1 - Position0, Position2 - Position0);
			if (TriangleNormal.SizeSquared() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			if (FVector::DotProduct(TriangleNormal, Region.Normal) < 0.0f)
			{
				Swap(GlobalVertex1, GlobalVertex2);
				TriangleNormal *= -1.0f;
			}

			FMobileSurfaceNavTriangle& NavTriangle = OutNavData.Triangles.AddDefaulted_GetRef();
			const int32 NavTriangleIndex = OutNavData.Triangles.Num() - 1;
			NavTriangle.VertexIndices = FIntVector(GlobalVertex0, GlobalVertex1, GlobalVertex2);
			NavTriangle.Normal = TriangleNormal.GetSafeNormal();
			NavTriangle.Center = (OutNavData.Vertices[GlobalVertex0].LocalPosition + OutNavData.Vertices[GlobalVertex1].LocalPosition + OutNavData.Vertices[GlobalVertex2].LocalPosition) / 3.0f;
			NavTriangle.RegionId = RegionId;
			NavRegion.TriangleIndices.Add(NavTriangleIndex);
		}

		UE_LOG(
			LogMobileSurfaceNavigationBuilder,
			Log,
			TEXT("Region %d: outputTriangles=%d"),
			RegionId,
			NavRegion.TriangleIndices.Num());

		return NavRegion.TriangleIndices.Num() > 0;
	}

	static bool TriangulateInsetRegionFromNavData(
		const FMobileSurfaceNavData& SourceNavData,
		const int32 RegionId,
		const float AgentRadius,
		FMobileSurfaceNavData& OutNavData,
		FString& OutError)
	{
		if (!SourceNavData.Regions.IsValidIndex(RegionId))
		{
			return false;
		}

		const FMobileSurfaceNavRegion& SourceRegion = SourceNavData.Regions[RegionId];
		if (SourceRegion.TriangleIndices.IsEmpty())
		{
			return false;
		}

		FVector RegionNormal = SourceRegion.AverageNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		FVector RegionOrigin = FVector::ZeroVector;
		for (const int32 TriangleIndex : SourceRegion.TriangleIndices)
		{
			if (SourceNavData.Triangles.IsValidIndex(TriangleIndex))
			{
				RegionOrigin += SourceNavData.Triangles[TriangleIndex].Center;
			}
		}
		RegionOrigin /= static_cast<double>(SourceRegion.TriangleIndices.Num());

		FVector BasisX = FVector::CrossProduct(RegionNormal, FVector::UpVector);
		if (!BasisX.Normalize())
		{
			BasisX = FVector::CrossProduct(RegionNormal, FVector::RightVector);
			BasisX.Normalize();
		}
		const FVector BasisY = FVector::CrossProduct(RegionNormal, BasisX).GetSafeNormal();

		TMap<int32, int32> GlobalToRegionVertex;
		TArray<int32> RegionToGlobalVertex;
		TArray<FVector2d> RegionVertices2D;

		TSet<int32> RegionVertexSet;
		TArray<FRegionBoundaryEdgeInfo> BoundaryEdgeInfos;
		CollectRegionBoundaryEdgeInfosFromNavData(SourceNavData, RegionId, BoundaryEdgeInfos);
		for (const int32 TriangleIndex : SourceRegion.TriangleIndices)
		{
			if (!SourceNavData.Triangles.IsValidIndex(TriangleIndex))
			{
				continue;
			}

			const FMobileSurfaceNavTriangle& Triangle = SourceNavData.Triangles[TriangleIndex];
			RegionVertexSet.Add(Triangle.VertexIndices.X);
			RegionVertexSet.Add(Triangle.VertexIndices.Y);
			RegionVertexSet.Add(Triangle.VertexIndices.Z);
		}

		for (const int32 GlobalVertexIndex : RegionVertexSet)
		{
			const int32 RegionVertexIndex = RegionToGlobalVertex.Add(GlobalVertexIndex);
			GlobalToRegionVertex.Add(GlobalVertexIndex, RegionVertexIndex);

			const FVector Delta = SourceNavData.Vertices[GlobalVertexIndex].LocalPosition - RegionOrigin;
			RegionVertices2D.Add(FVector2d(FVector::DotProduct(Delta, BasisX), FVector::DotProduct(Delta, BasisY)));
		}

		TArray<FBoundaryEdge> RemappedBoundaryEdges;
		RemappedBoundaryEdges.Reserve(BoundaryEdgeInfos.Num());
		TMap<uint64, bool> DirectedPhysicalBoundaryMap;
		for (const FRegionBoundaryEdgeInfo& Edge : BoundaryEdgeInfos)
		{
			const int32* StartVertex = GlobalToRegionVertex.Find(Edge.StartVertex);
			const int32* EndVertex = GlobalToRegionVertex.Find(Edge.EndVertex);
			if (!StartVertex || !EndVertex)
			{
				continue;
			}

			FBoundaryEdge& Remapped = RemappedBoundaryEdges.AddDefaulted_GetRef();
			Remapped.StartVertex = *StartVertex;
			Remapped.EndVertex = *EndVertex;
			DirectedPhysicalBoundaryMap.Add(MakeDirectedEdgeKey(*StartVertex, *EndVertex), Edge.bIsPhysicalBoundary);

		}

		TArray<FBoundaryLoopTemp> RegionLoops;
		BuildLoopsFromEdges(RemappedBoundaryEdges, RegionLoops);
		if (RegionLoops.IsEmpty())
		{
			return false;
		}

		TArray<FPolygon2d> LoopPolygons;
		LoopPolygons.Reserve(RegionLoops.Num());
		for (const FBoundaryLoopTemp& Loop : RegionLoops)
		{
			TArray<FVector2d> LoopPoints2D;
			LoopPoints2D.Reserve(Loop.VertexIndices.Num());
			for (const int32 RegionVertexIndex : Loop.VertexIndices)
			{
				LoopPoints2D.Add(RegionVertices2D[RegionVertexIndex]);
			}

			LoopPolygons.Emplace(LoopPoints2D);
		}

		TArray<bool> bLoopInsetsTowardPolygonInterior;
		bLoopInsetsTowardPolygonInterior.Init(true, RegionLoops.Num());
		{
			FPlanarComplexd PlanarComplex;
			PlanarComplex.bTrustOrientations = false;
			for (const FPolygon2d& Polygon : LoopPolygons)
			{
				PlanarComplex.Polygons.Add(Polygon);
			}
			PlanarComplex.FindSolidRegions();

			bLoopInsetsTowardPolygonInterior.Init(false, RegionLoops.Num());
			for (const FPlanarComplexd::FPolygonNesting& Solid : PlanarComplex.Solids)
			{
				if (bLoopInsetsTowardPolygonInterior.IsValidIndex(Solid.OuterIndex))
				{
					bLoopInsetsTowardPolygonInterior[Solid.OuterIndex] = true;
				}

				for (const int32 HoleIndex : Solid.HoleIndices)
				{
					if (bLoopInsetsTowardPolygonInterior.IsValidIndex(HoleIndex))
					{
						bLoopInsetsTowardPolygonInterior[HoleIndex] = false;
					}
				}
			}
		}

		TArray<FVector2d> InsetVertices2D = RegionVertices2D;
		for (int32 LoopArrayIndex = 0; LoopArrayIndex < RegionLoops.Num(); ++LoopArrayIndex)
		{
			const FBoundaryLoopTemp& Loop = RegionLoops[LoopArrayIndex];
			if (!Loop.bClosed || Loop.VertexIndices.Num() < 3)
			{
				continue;
			}

			const FPolygon2d& LoopPolygon = LoopPolygons[LoopArrayIndex];
			const bool bCounterClockwise = LoopPolygon.IsClockwise() == false;
			const bool bInsetTowardPolygonInterior = bLoopInsetsTowardPolygonInterior.IsValidIndex(LoopArrayIndex)
				? bLoopInsetsTowardPolygonInterior[LoopArrayIndex]
				: true;

			for (int32 LoopIndex = 0; LoopIndex < Loop.VertexIndices.Num(); ++LoopIndex)
			{
				const int32 PrevLoopIndex = (LoopIndex - 1 + Loop.VertexIndices.Num()) % Loop.VertexIndices.Num();
				const int32 NextLoopIndex = (LoopIndex + 1) % Loop.VertexIndices.Num();
				const int32 PrevVertexIndex = Loop.VertexIndices[PrevLoopIndex];
				const int32 CurrentVertexIndex = Loop.VertexIndices[LoopIndex];
				const int32 NextVertexIndex = Loop.VertexIndices[NextLoopIndex];

				const bool bPrevPhysical = DirectedPhysicalBoundaryMap.FindRef(MakeDirectedEdgeKey(PrevVertexIndex, CurrentVertexIndex));
				const bool bNextPhysical = DirectedPhysicalBoundaryMap.FindRef(MakeDirectedEdgeKey(CurrentVertexIndex, NextVertexIndex));
				if (!bPrevPhysical && !bNextPhysical)
				{
					continue;
				}

				const FVector2d PrevPos = RegionVertices2D[PrevVertexIndex];
				const FVector2d CurrPos = RegionVertices2D[CurrentVertexIndex];
				const FVector2d NextPos = RegionVertices2D[NextVertexIndex];

				FVector2d PrevDir = (CurrPos - PrevPos).GetSafeNormal();
				FVector2d NextDir = (NextPos - CurrPos).GetSafeNormal();
				if (PrevDir.IsNearlyZero() || NextDir.IsNearlyZero())
				{
					continue;
				}

				const auto MakeInwardNormal = [&](const FVector2d& Dir) -> FVector2d
				{
					const FVector2d PolygonInteriorNormal = bCounterClockwise ? FVector2d(-Dir.Y, Dir.X) : FVector2d(Dir.Y, -Dir.X);
					return bInsetTowardPolygonInterior ? PolygonInteriorNormal : -PolygonInteriorNormal;
				};

				const FVector2d PrevNormal = MakeInwardNormal(PrevDir);
				const FVector2d NextNormal = MakeInwardNormal(NextDir);
				FVector2d NewPosition = CurrPos;

				if (bPrevPhysical && bNextPhysical)
				{
					const FVector2d PrevOffsetPoint = CurrPos + PrevNormal * AgentRadius;
					const FVector2d NextOffsetPoint = CurrPos + NextNormal * AgentRadius;
					if (!IntersectLines2D(PrevOffsetPoint, PrevDir, NextOffsetPoint, NextDir, NewPosition))
					{
						NewPosition = CurrPos + (PrevNormal + NextNormal).GetSafeNormal() * AgentRadius;
					}
				}
				else if (bPrevPhysical)
				{
					NewPosition = CurrPos + PrevNormal * AgentRadius;
				}
				else if (bNextPhysical)
				{
					NewPosition = CurrPos + NextNormal * AgentRadius;
				}

				InsetVertices2D[CurrentVertexIndex] = NewPosition;
			}
		}

		TArray<FVector2d> CdtVertices2D;
		CdtVertices2D.Reserve(RegionToGlobalVertex.Num());
		TArray<int32> CdtToLayerVertexIndex;
		CdtToLayerVertexIndex.Reserve(RegionToGlobalVertex.Num());
		for (int32 RegionVertexIndex = 0; RegionVertexIndex < RegionToGlobalVertex.Num(); ++RegionVertexIndex)
		{
			const FVector2d Position2D = InsetVertices2D[RegionVertexIndex];
			const FVector Position3D = RegionOrigin + BasisX * Position2D.X + BasisY * Position2D.Y;
			FMobileSurfaceNavVertex& Vertex = OutNavData.Vertices.AddDefaulted_GetRef();
			Vertex.LocalPosition = Position3D;
			OutNavData.LocalBounds += Position3D;

			CdtVertices2D.Add(Position2D);
			CdtToLayerVertexIndex.Add(OutNavData.Vertices.Num() - 1);
		}

		TArray<FIndex2i> ConstrainedEdges;
		ConstrainedEdges.Reserve(RemappedBoundaryEdges.Num());
		for (const FBoundaryEdge& Edge : RemappedBoundaryEdges)
		{
			ConstrainedEdges.Add(FIndex2i(Edge.StartVertex, Edge.EndVertex));
		}

		FDelaunay2 Delaunay;
		Delaunay.bAutomaticallyFixEdgesToDuplicateVertices = true;
		Delaunay.bValidateEdges = true;

		if (!Delaunay.Triangulate(CdtVertices2D, ConstrainedEdges))
		{
			OutError = FString::Printf(TEXT("Inset CDT failed for region %d. Result=%d"), RegionId, static_cast<int32>(Delaunay.GetResult()));
			return false;
		}

		const TArray<FIndex3i> RegionTriangles = Delaunay.GetFilledTriangles(ConstrainedEdges, FDelaunay2::EFillMode::PositiveWinding);
		if (RegionTriangles.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Inset CDT produced no filled triangles for region %d."), RegionId);
			return false;
		}

		FMobileSurfaceNavRegion& NavRegion = OutNavData.Regions[RegionId];
		NavRegion.AverageNormal = RegionNormal;

		for (const FIndex3i& RegionTriangle : RegionTriangles)
		{
			const int32 LayerVertex0 = CdtToLayerVertexIndex[RegionTriangle.A];
			int32 LayerVertex1 = CdtToLayerVertexIndex[RegionTriangle.B];
			int32 LayerVertex2 = CdtToLayerVertexIndex[RegionTriangle.C];

			const FVector Position0 = OutNavData.Vertices[LayerVertex0].LocalPosition;
			const FVector Position1 = OutNavData.Vertices[LayerVertex1].LocalPosition;
			const FVector Position2 = OutNavData.Vertices[LayerVertex2].LocalPosition;
			FVector TriangleNormal = FVector::CrossProduct(Position1 - Position0, Position2 - Position0);
			if (TriangleNormal.SizeSquared() <= UE_SMALL_NUMBER)
			{
				continue;
			}

			if (FVector::DotProduct(TriangleNormal, RegionNormal) < 0.0f)
			{
				Swap(LayerVertex1, LayerVertex2);
				TriangleNormal *= -1.0f;
			}

			FMobileSurfaceNavTriangle& NavTriangle = OutNavData.Triangles.AddDefaulted_GetRef();
			const int32 NavTriangleIndex = OutNavData.Triangles.Num() - 1;
			NavTriangle.VertexIndices = FIntVector(LayerVertex0, LayerVertex1, LayerVertex2);
			NavTriangle.Normal = TriangleNormal.GetSafeNormal();
			NavTriangle.Center = (Position0 + Position1 + Position2) / 3.0f;
			NavTriangle.RegionId = RegionId;
			NavRegion.TriangleIndices.Add(NavTriangleIndex);
		}

		return NavRegion.TriangleIndices.Num() > 0;
	}
}

bool FMobileSurfaceNavigationBuilder::BuildFromStaticMeshComponent(
	const UStaticMeshComponent* SourceComponent,
	const USceneComponent* TargetSpaceComponent,
	const FMobileSurfaceNavBuildSettings& Settings,
	FMobileSurfaceNavData& OutNavData,
	FString& OutError)
{
	OutNavData.Reset();
	OutError.Reset();

	if (!SourceComponent)
	{
		OutError = TEXT("Navigation source component is null.");
		return false;
	}

	const UStaticMesh* StaticMesh = SourceComponent->GetStaticMesh();
	if (!StaticMesh)
	{
		OutError = TEXT("Navigation source component has no static mesh assigned.");
		return false;
	}

	TArray<MobileSurfaceNavigation::Builder::FSourceTriangle> SourceTriangles;
	MobileSurfaceNavigation::Builder::ExtractSourceTriangles(
		SourceComponent,
		TargetSpaceComponent,
		Settings,
		OutNavData,
		SourceTriangles,
		OutError);

	if (!OutError.IsEmpty())
	{
		UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("Build failed during source extraction: %s"), *OutError);
		return false;
	}

	if (SourceTriangles.Num() == 0)
	{
		OutError = TEXT("Failed to extract any valid source triangles from the navigation mesh.");
		UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
		return false;
	}

	UE_LOG(
		LogMobileSurfaceNavigationBuilder,
		Log,
		TEXT("Build start: sourceMesh=%s weldedVertices=%d sourceTriangles=%d"),
		*GetNameSafe(StaticMesh),
		OutNavData.Vertices.Num(),
		SourceTriangles.Num());

	TArray<MobileSurfaceNavigation::Builder::FPlanarRegion> Regions;
	MobileSurfaceNavigation::Builder::BuildPlanarRegions(SourceTriangles, OutNavData.Vertices, Settings, Regions);
	if (Regions.Num() == 0)
	{
		OutError = TEXT("Failed to build planar regions from the navigation source mesh.");
		UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
		return false;
	}

	UE_LOG(LogMobileSurfaceNavigationBuilder, Log, TEXT("Build planar regions: regionCount=%d"), Regions.Num());

	OutNavData.Regions.SetNum(Regions.Num());
	MobileSurfaceNavigation::Builder::BuildGlobalBoundaryLoops(SourceTriangles, OutNavData);
	UE_LOG(LogMobileSurfaceNavigationBuilder, Log, TEXT("Global open boundary loops=%d"), OutNavData.BoundaryLoops.Num());

	for (int32 RegionId = 0; RegionId < Regions.Num(); ++RegionId)
	{
		if (!MobileSurfaceNavigation::Builder::TriangulateRegion(Regions[RegionId], RegionId, SourceTriangles, Settings, OutNavData, OutError))
		{
			UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("Build failed in region %d: %s"), RegionId, *OutError);
			return false;
		}
	}

	MobileSurfaceNavigation::Builder::RebuildFinalEdgesAndAdjacency(OutNavData);

	OutNavData.bIsValid = OutNavData.Vertices.Num() > 0 && OutNavData.Triangles.Num() > 0;
	OutNavData.BuildNotes = TEXT("Navigation data is now baked with constrained Delaunay triangulation per planar region. Region seams are used as internal constraints while exposed boundary loops still represent only open mesh boundaries.");

	if (!OutNavData.bIsValid)
	{
		OutError = TEXT("Failed to build any valid navigation triangles from the source mesh.");
		UE_LOG(LogMobileSurfaceNavigationBuilder, Warning, TEXT("%s"), *OutError);
		return false;
	}

	UE_LOG(
		LogMobileSurfaceNavigationBuilder,
		Log,
		TEXT("Build complete: outputTriangles=%d outputEdges=%d outputRegions=%d buildNotes=\"%s\""),
		OutNavData.Triangles.Num(),
		OutNavData.Edges.Num(),
		OutNavData.Regions.Num(),
		*OutNavData.BuildNotes);

	return true;
}

bool FMobileSurfaceNavigationBuilder::BuildAgentRadiusLayer(
	const FMobileSurfaceNavData& SourceNavData,
	const float AgentRadius,
	FMobileSurfaceNavData& OutNavData,
	FString& OutError)
{
	OutNavData.Reset();
	OutError.Reset();

	if (!SourceNavData.bIsValid)
	{
		OutError = TEXT("Source navigation data is invalid.");
		return false;
	}

	if (AgentRadius <= UE_KINDA_SMALL_NUMBER)
	{
		OutNavData = SourceNavData;
		return true;
	}

	OutNavData.Reset();
	OutNavData.RegionRuntimeStates = SourceNavData.RegionRuntimeStates;
	OutNavData.Regions.SetNum(SourceNavData.Regions.Num());

	// Base special links are copied later and remapped by the component layer sync.
	for (int32 RegionId = 0; RegionId < SourceNavData.Regions.Num(); ++RegionId)
	{
		if (!MobileSurfaceNavigation::Builder::TriangulateInsetRegionFromNavData(SourceNavData, RegionId, AgentRadius, OutNavData, OutError))
		{
			if (!OutError.IsEmpty())
			{
				return false;
			}
		}
	}

	if (OutNavData.Triangles.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No navigation triangles remain for agent radius %.2f after inset triangulation."), AgentRadius);
		return false;
	}

	MobileSurfaceNavigation::Builder::WeldCoincidentVertices(OutNavData, 0.01f);
	MobileSurfaceNavigation::Builder::RebuildFinalEdgesAndAdjacency(OutNavData);
	MobileSurfaceNavigation::Builder::BuildGlobalBoundaryLoopsFromNavTriangles(OutNavData);
	OutNavData.SpecialLinks = SourceNavData.SpecialLinks;

	OutNavData.BuildNotes = FString::Printf(
		TEXT("Derived navigation layer baked for agent radius %.2f using inset boundary geometry."),
		AgentRadius);
	OutNavData.bIsValid = true;
	UE_LOG(
		LogMobileSurfaceNavigationBuilder,
		Log,
		TEXT("Radius layer %.2f: triangles=%d portals=%d"),
		AgentRadius,
		OutNavData.Triangles.Num(),
		OutNavData.Portals.Num());
	return true;
}
