#include "MobileSurfacePathfinder.h"

#include "MobileSurfaceNavigationQuery.h"

namespace MobileSurfaceNavigation::Pathfinder
{
	struct FFunnelPortal
	{
		FVector Left3D = FVector::ZeroVector;
		FVector Right3D = FVector::ZeroVector;
		FVector2D Left2D = FVector2D::ZeroVector;
		FVector2D Right2D = FVector2D::ZeroVector;
	};

	struct FOpenNode
	{
		int32 TriangleIndex = INDEX_NONE;
		double FScore = TNumericLimits<double>::Max();
	};

	static bool HasHigherPriority(const FOpenNode& A, const FOpenNode& B)
	{
		return A.FScore < B.FScore;
	}

	static void PushOpenNode(TArray<FOpenNode>& Heap, const FOpenNode& Node)
	{
		Heap.Add(Node);
		int32 ChildIndex = Heap.Num() - 1;
		while (ChildIndex > 0)
		{
			const int32 ParentIndex = (ChildIndex - 1) / 2;
			if (HasHigherPriority(Heap[ParentIndex], Heap[ChildIndex]))
			{
				break;
			}

			Swap(Heap[ParentIndex], Heap[ChildIndex]);
			ChildIndex = ParentIndex;
		}
	}

	static FOpenNode PopOpenNode(TArray<FOpenNode>& Heap)
	{
		FOpenNode Result = Heap[0];
		Heap[0] = Heap.Last();
		Heap.Pop(EAllowShrinking::No);

		int32 ParentIndex = 0;
		while (true)
		{
			const int32 LeftChildIndex = ParentIndex * 2 + 1;
			const int32 RightChildIndex = LeftChildIndex + 1;
			int32 BestChildIndex = ParentIndex;

			if (Heap.IsValidIndex(LeftChildIndex) && HasHigherPriority(Heap[LeftChildIndex], Heap[BestChildIndex]))
			{
				BestChildIndex = LeftChildIndex;
			}

			if (Heap.IsValidIndex(RightChildIndex) && HasHigherPriority(Heap[RightChildIndex], Heap[BestChildIndex]))
			{
				BestChildIndex = RightChildIndex;
			}

			if (BestChildIndex == ParentIndex)
			{
				break;
			}

			Swap(Heap[ParentIndex], Heap[BestChildIndex]);
			ParentIndex = BestChildIndex;
		}

		return Result;
	}

	static double ComputeBoundaryClearanceSquared(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition)
	{
		double BestDistanceSquared = TNumericLimits<double>::Max();
		bool bHasBoundary = false;

		for (const FMobileSurfaceNavEdge& Edge : NavData.Edges)
		{
			if (!Edge.bIsBoundary)
			{
				continue;
			}

			bHasBoundary = true;
			const FVector A = NavData.Vertices[Edge.VertexIndices.X].LocalPosition;
			const FVector B = NavData.Vertices[Edge.VertexIndices.Y].LocalPosition;
			BestDistanceSquared = FMath::Min(BestDistanceSquared, static_cast<double>(FMath::PointDistToSegmentSquared(LocalPosition, A, B)));
		}

		return bHasBoundary ? BestDistanceSquared : TNumericLimits<double>::Max();
	}

	static bool HasEnoughClearance(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition, const float AgentRadius)
	{
		if (AgentRadius <= 0.0f)
		{
			return true;
		}

		return ComputeBoundaryClearanceSquared(NavData, LocalPosition) >= FMath::Square(static_cast<double>(AgentRadius));
	}

	static bool IsTagAllowed(const FName Tag, const TArray<FName>& AllowedTags, const TArray<FName>& ExcludedTags)
	{
		if (ExcludedTags.Contains(Tag))
		{
			return false;
		}

		return AllowedTags.IsEmpty() || AllowedTags.Contains(Tag);
	}

	static const FMobileSurfaceNavRegionRuntimeState* GetRegionState(const FMobileSurfaceNavData& NavData, const int32 RegionId)
	{
		return NavData.RegionRuntimeStates.IsValidIndex(RegionId) ? &NavData.RegionRuntimeStates[RegionId] : nullptr;
	}

	static const FMobileSurfaceNavPortalRuntimeState* GetPortalState(const FMobileSurfaceNavData& NavData, const int32 PortalIndex)
	{
		return NavData.PortalRuntimeStates.IsValidIndex(PortalIndex) ? &NavData.PortalRuntimeStates[PortalIndex] : nullptr;
	}

	static bool IsTriangleAllowed(const FMobileSurfaceNavData& NavData, const int32 TriangleIndex, const FMobileSurfacePathQueryParams& Params)
	{
		if (!NavData.Triangles.IsValidIndex(TriangleIndex))
		{
			return false;
		}

		const FMobileSurfaceNavRegionRuntimeState* RegionState = GetRegionState(NavData, NavData.Triangles[TriangleIndex].RegionId);
		if (!RegionState)
		{
			return true;
		}

		return RegionState->bEnabled && IsTagAllowed(RegionState->AreaTag, Params.AllowedAreaTags, Params.ExcludedAreaTags);
	}

	static bool IsPortalAllowed(
		const FMobileSurfaceNavData& NavData,
		const FMobileSurfaceTriangleAdjacency& Adjacency,
		const FMobileSurfacePathQueryParams& Params)
	{
		const FMobileSurfaceNavPortalRuntimeState* PortalState = GetPortalState(NavData, Adjacency.PortalIndex);
		if (PortalState)
		{
			if (!PortalState->bOpen && !Params.bCanUseClosedPortals)
			{
				return false;
			}

			if (!IsTagAllowed(PortalState->PortalTag, Params.AllowedPortalTags, Params.ExcludedPortalTags))
			{
				return false;
			}
		}

		const float EffectivePortalWidth = PortalState && PortalState->EffectiveWidthOverride > 0.0f
			? PortalState->EffectiveWidthOverride
			: Adjacency.PortalWidth;

		if (Params.AgentRadius > 0.0f)
		{
			if (EffectivePortalWidth < Params.AgentRadius * 2.0f || Adjacency.BoundaryClearance < Params.AgentRadius)
			{
				return false;
			}
		}

		return true;
	}

	static double GetTriangleCostMultiplier(const FMobileSurfaceNavData& NavData, const int32 TriangleIndex)
	{
		const FMobileSurfaceNavRegionRuntimeState* RegionState = NavData.Triangles.IsValidIndex(TriangleIndex)
			? GetRegionState(NavData, NavData.Triangles[TriangleIndex].RegionId)
			: nullptr;

		return RegionState ? FMath::Max(0.001f, RegionState->CostMultiplier) : 1.0;
	}

	static double GetPortalTraversalCost(
		const FMobileSurfaceNavData& NavData,
		const FMobileSurfaceTriangleAdjacency& Adjacency,
		const int32 NeighborTriangle)
	{
		const FMobileSurfaceNavPortalRuntimeState* PortalState = GetPortalState(NavData, Adjacency.PortalIndex);
		const double PortalCostMultiplier = PortalState ? FMath::Max(0.001f, PortalState->CostMultiplier) : 1.0;
		const double PortalExtraCost = PortalState ? FMath::Max(0.0f, PortalState->ExtraCost) : 0.0;
		return (Adjacency.TravelCost * GetTriangleCostMultiplier(NavData, NeighborTriangle) * PortalCostMultiplier) + PortalExtraCost;
	}

	static double TriArea2(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const FVector2D AB = B - A;
		const FVector2D AC = C - A;
		return static_cast<double>(AC.X * AB.Y) - static_cast<double>(AB.X * AC.Y);
	}

	static bool NearlySamePoint(const FVector& A, const FVector& B)
	{
		return FVector::DistSquared(A, B) <= FMath::Square(0.1);
	}

	static void AddWaypointIfNeeded(TArray<FVector>& Waypoints, const FVector& Point)
	{
		if (Waypoints.IsEmpty() || !NearlySamePoint(Waypoints.Last(), Point))
		{
			Waypoints.Add(Point);
		}
	}

	static void BuildPathBasis(
		const FMobileSurfaceNavData& NavData,
		const TArray<int32>& TrianglePath,
		const FVector& Start,
		const FVector& End,
		FVector& OutOrigin,
		FVector& OutBasisX,
		FVector& OutBasisY,
		FVector& OutNormal)
	{
		OutOrigin = Start;
		OutNormal = FVector::ZeroVector;
		for (const int32 TriangleIndex : TrianglePath)
		{
			OutNormal += NavData.Triangles[TriangleIndex].Normal;
		}
		OutNormal = OutNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

		OutBasisX = End - Start;
		OutBasisX -= OutNormal * FVector::DotProduct(OutBasisX, OutNormal);
		if (!OutBasisX.Normalize())
		{
			OutBasisX = FVector::CrossProduct(OutNormal, FVector::UpVector);
			if (!OutBasisX.Normalize())
			{
				OutBasisX = FVector::CrossProduct(OutNormal, FVector::RightVector).GetSafeNormal();
			}
		}

		OutBasisY = FVector::CrossProduct(OutNormal, OutBasisX).GetSafeNormal();
	}

	static FVector2D ProjectToPath2D(const FVector& Point, const FVector& Origin, const FVector& BasisX, const FVector& BasisY)
	{
		const FVector Delta = Point - Origin;
		return FVector2D(FVector::DotProduct(Delta, BasisX), FVector::DotProduct(Delta, BasisY));
	}

	static TArray<FVector> RunFunnel(
		const FMobileSurfaceNavData& NavData,
		const TArray<int32>& TrianglePath,
		const FVector& Start,
		const FVector& End)
	{
		TArray<FVector> Result;
		if (TrianglePath.Num() < 2)
		{
			Result.Add(Start);
			Result.Add(End);
			return Result;
		}

		FVector Origin;
		FVector BasisX;
		FVector BasisY;
		FVector PathNormal;
		BuildPathBasis(NavData, TrianglePath, Start, End, Origin, BasisX, BasisY, PathNormal);

		TArray<FFunnelPortal> Portals;
		Portals.Reserve(TrianglePath.Num() + 1);

		FFunnelPortal& StartPortal = Portals.AddDefaulted_GetRef();
		StartPortal.Left3D = Start;
		StartPortal.Right3D = Start;
		StartPortal.Left2D = ProjectToPath2D(Start, Origin, BasisX, BasisY);
		StartPortal.Right2D = StartPortal.Left2D;

		for (int32 PathIndex = 0; PathIndex + 1 < TrianglePath.Num(); ++PathIndex)
		{
			const int32 CurrentTriangleIndex = TrianglePath[PathIndex];
			const int32 NextTriangleIndex = TrianglePath[PathIndex + 1];
			const FMobileSurfaceNavPortal* Portal = nullptr;
			if (NavData.TriangleAdjacency.IsValidIndex(CurrentTriangleIndex))
			{
				for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[CurrentTriangleIndex].Neighbors)
				{
					if (Adjacency.NeighborTriangleIndex == NextTriangleIndex && NavData.Portals.IsValidIndex(Adjacency.PortalIndex))
					{
						Portal = &NavData.Portals[Adjacency.PortalIndex];
						break;
					}
				}
			}

			if (!Portal)
			{
				continue;
			}

			const FVector CurrentCenter = NavData.Triangles[CurrentTriangleIndex].Center;
			const FVector NextCenter = NavData.Triangles[NextTriangleIndex].Center;
			const FVector2D CurrentCenter2D = ProjectToPath2D(CurrentCenter, Origin, BasisX, BasisY);
			const FVector2D NextCenter2D = ProjectToPath2D(NextCenter, Origin, BasisX, BasisY);
			const FVector2D PathDirection2D = NextCenter2D - CurrentCenter2D;

			const FVector2D PointA2D = ProjectToPath2D(Portal->LeftPoint, Origin, BasisX, BasisY);
			const FVector2D PointB2D = ProjectToPath2D(Portal->RightPoint, Origin, BasisX, BasisY);
			const FVector2D PortalCenter2D = (PointA2D + PointB2D) * 0.5f;
			const double SideA = static_cast<double>(PathDirection2D.X * (PointA2D.Y - PortalCenter2D.Y) - PathDirection2D.Y * (PointA2D.X - PortalCenter2D.X));

			FFunnelPortal& FunnelPortal = Portals.AddDefaulted_GetRef();
			if (SideA >= 0.0)
			{
				FunnelPortal.Left3D = Portal->LeftPoint;
				FunnelPortal.Right3D = Portal->RightPoint;
				FunnelPortal.Left2D = PointA2D;
				FunnelPortal.Right2D = PointB2D;
			}
			else
			{
				FunnelPortal.Left3D = Portal->RightPoint;
				FunnelPortal.Right3D = Portal->LeftPoint;
				FunnelPortal.Left2D = PointB2D;
				FunnelPortal.Right2D = PointA2D;
			}
		}

		FFunnelPortal& EndPortal = Portals.AddDefaulted_GetRef();
		EndPortal.Left3D = End;
		EndPortal.Right3D = End;
		EndPortal.Left2D = ProjectToPath2D(End, Origin, BasisX, BasisY);
		EndPortal.Right2D = EndPortal.Left2D;

		FVector2D PortalApex = Portals[0].Left2D;
		FVector2D PortalLeft = Portals[0].Left2D;
		FVector2D PortalRight = Portals[0].Right2D;
		FVector Apex3D = Start;
		FVector Left3D = Start;
		FVector Right3D = Start;
		int32 ApexIndex = 0;
		int32 LeftIndex = 0;
		int32 RightIndex = 0;

		Result.Add(Start);

		for (int32 PortalIndex = 1; PortalIndex < Portals.Num(); ++PortalIndex)
		{
			const FVector2D NewLeft = Portals[PortalIndex].Left2D;
			const FVector2D NewRight = Portals[PortalIndex].Right2D;

			if (TriArea2(PortalApex, PortalRight, NewRight) <= 0.0)
			{
				if (ApexIndex == RightIndex || TriArea2(PortalApex, PortalLeft, NewRight) > 0.0)
				{
					PortalRight = NewRight;
					Right3D = Portals[PortalIndex].Right3D;
					RightIndex = PortalIndex;
				}
				else
				{
					AddWaypointIfNeeded(Result, Left3D);
					PortalApex = PortalLeft;
					Apex3D = Left3D;
					ApexIndex = LeftIndex;
					PortalLeft = PortalApex;
					PortalRight = PortalApex;
					Left3D = Apex3D;
					Right3D = Apex3D;
					LeftIndex = ApexIndex;
					RightIndex = ApexIndex;
					PortalIndex = ApexIndex;
					continue;
				}
			}

			if (TriArea2(PortalApex, PortalLeft, NewLeft) >= 0.0)
			{
				if (ApexIndex == LeftIndex || TriArea2(PortalApex, PortalRight, NewLeft) < 0.0)
				{
					PortalLeft = NewLeft;
					Left3D = Portals[PortalIndex].Left3D;
					LeftIndex = PortalIndex;
				}
				else
				{
					AddWaypointIfNeeded(Result, Right3D);
					PortalApex = PortalRight;
					Apex3D = Right3D;
					ApexIndex = RightIndex;
					PortalLeft = PortalApex;
					PortalRight = PortalApex;
					Left3D = Apex3D;
					Right3D = Apex3D;
					LeftIndex = ApexIndex;
					RightIndex = ApexIndex;
					PortalIndex = ApexIndex;
					continue;
				}
			}
		}

		AddWaypointIfNeeded(Result, End);
		return Result;
	}
}

bool FMobileSurfacePathfinder::FindPath(
	const FMobileSurfaceNavData& NavData,
	const FVector& StartLocalPosition,
	const FVector& EndLocalPosition,
	const FMobileSurfacePathQueryParams& Params,
	FMobileSurfaceNavPath& OutPath)
{
	OutPath = FMobileSurfaceNavPath();

	if (!NavData.bIsValid || NavData.Triangles.IsEmpty())
	{
		return false;
	}

	const int32 StartTriangleIndex = FMobileSurfaceNavigationQuery::FindContainingTriangle(NavData, StartLocalPosition);
	const int32 EndTriangleIndex = FMobileSurfaceNavigationQuery::FindContainingTriangle(NavData, EndLocalPosition);
	const int32 FallbackStartTriangle = StartTriangleIndex != INDEX_NONE ? StartTriangleIndex : FMobileSurfaceNavigationQuery::FindNearestTriangle(NavData, StartLocalPosition);
	const int32 FallbackEndTriangle = EndTriangleIndex != INDEX_NONE ? EndTriangleIndex : FMobileSurfaceNavigationQuery::FindNearestTriangle(NavData, EndLocalPosition);

	if (FallbackStartTriangle == INDEX_NONE || FallbackEndTriangle == INDEX_NONE)
	{
		return false;
	}

	if (!MobileSurfaceNavigation::Pathfinder::IsTriangleAllowed(NavData, FallbackStartTriangle, Params) ||
		!MobileSurfaceNavigation::Pathfinder::IsTriangleAllowed(NavData, FallbackEndTriangle, Params))
	{
		return false;
	}

	if (Params.bRequireStartAndEndClearance &&
		(!MobileSurfaceNavigation::Pathfinder::HasEnoughClearance(NavData, StartLocalPosition, Params.AgentRadius) ||
		 !MobileSurfaceNavigation::Pathfinder::HasEnoughClearance(NavData, EndLocalPosition, Params.AgentRadius)))
	{
		return false;
	}

	OutPath.StartTriangleIndex = FallbackStartTriangle;
	OutPath.EndTriangleIndex = FallbackEndTriangle;
	OutPath.RuntimeStateRevision = NavData.RuntimeStateRevision;

	if (FallbackStartTriangle == FallbackEndTriangle)
	{
		OutPath.bIsValid = true;
		OutPath.TriangleIndices = { FallbackStartTriangle };
		OutPath.RawWaypoints = { StartLocalPosition, EndLocalPosition };
		OutPath.Waypoints = OutPath.RawWaypoints;
		OutPath.EstimatedLength = FVector::Distance(StartLocalPosition, EndLocalPosition);
		return true;
	}

	struct FNodeRecord
	{
		double GScore = TNumericLimits<double>::Max();
		double FScore = TNumericLimits<double>::Max();
		int32 CameFrom = INDEX_NONE;
		bool bClosed = false;
	};

	TArray<FNodeRecord> Records;
	Records.SetNum(NavData.Triangles.Num());

	TArray<MobileSurfaceNavigation::Pathfinder::FOpenNode> OpenSet;
	Records[FallbackStartTriangle].GScore = 0.0;
	Records[FallbackStartTriangle].FScore = FVector::Distance(NavData.Triangles[FallbackStartTriangle].Center, NavData.Triangles[FallbackEndTriangle].Center);
	MobileSurfaceNavigation::Pathfinder::PushOpenNode(OpenSet, { FallbackStartTriangle, Records[FallbackStartTriangle].FScore });

	while (!OpenSet.IsEmpty())
	{
		const MobileSurfaceNavigation::Pathfinder::FOpenNode CurrentOpenNode = MobileSurfaceNavigation::Pathfinder::PopOpenNode(OpenSet);
		const int32 CurrentTriangle = CurrentOpenNode.TriangleIndex;
		if (!Records.IsValidIndex(CurrentTriangle) || Records[CurrentTriangle].bClosed || CurrentOpenNode.FScore > Records[CurrentTriangle].FScore + UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		Records[CurrentTriangle].bClosed = true;

		if (CurrentTriangle == FallbackEndTriangle)
		{
			break;
		}

		if (!NavData.TriangleAdjacency.IsValidIndex(CurrentTriangle))
		{
			continue;
		}

		for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[CurrentTriangle].Neighbors)
		{
			const int32 NeighborTriangle = Adjacency.NeighborTriangleIndex;
			if (NeighborTriangle == INDEX_NONE || Records[NeighborTriangle].bClosed)
			{
				continue;
			}

			if (!MobileSurfaceNavigation::Pathfinder::IsTriangleAllowed(NavData, NeighborTriangle, Params) ||
				!MobileSurfaceNavigation::Pathfinder::IsPortalAllowed(NavData, Adjacency, Params))
			{
				continue;
			}

			const double TentativeG = Records[CurrentTriangle].GScore + MobileSurfaceNavigation::Pathfinder::GetPortalTraversalCost(NavData, Adjacency, NeighborTriangle);
			if (TentativeG < Records[NeighborTriangle].GScore)
			{
				Records[NeighborTriangle].CameFrom = CurrentTriangle;
				Records[NeighborTriangle].GScore = TentativeG;
				Records[NeighborTriangle].FScore = TentativeG + FVector::Distance(
					NavData.Triangles[NeighborTriangle].Center,
					NavData.Triangles[FallbackEndTriangle].Center);
				MobileSurfaceNavigation::Pathfinder::PushOpenNode(OpenSet, { NeighborTriangle, Records[NeighborTriangle].FScore });
			}
		}
	}

	if (Records[FallbackEndTriangle].CameFrom == INDEX_NONE)
	{
		return false;
	}

	TArray<int32> ReversedTriangles;
	for (int32 Cursor = FallbackEndTriangle; Cursor != INDEX_NONE; Cursor = Records[Cursor].CameFrom)
	{
		ReversedTriangles.Add(Cursor);
	}
	Algo::Reverse(ReversedTriangles);
	OutPath.TriangleIndices = ReversedTriangles;

	OutPath.RawWaypoints.Add(StartLocalPosition);
	for (int32 PathIndex = 0; PathIndex + 1 < ReversedTriangles.Num(); ++PathIndex)
	{
		if (NavData.TriangleAdjacency.IsValidIndex(ReversedTriangles[PathIndex]))
		{
			for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[ReversedTriangles[PathIndex]].Neighbors)
			{
				if (Adjacency.NeighborTriangleIndex == ReversedTriangles[PathIndex + 1] && NavData.Portals.IsValidIndex(Adjacency.PortalIndex))
				{
					OutPath.RawWaypoints.Add(NavData.Portals[Adjacency.PortalIndex].Center);
					break;
				}
			}
		}
	}
	OutPath.RawWaypoints.Add(EndLocalPosition);
	OutPath.Waypoints = MobileSurfaceNavigation::Pathfinder::RunFunnel(NavData, OutPath.TriangleIndices, StartLocalPosition, EndLocalPosition);
	if (OutPath.Waypoints.Num() < 2)
	{
		OutPath.Waypoints = OutPath.RawWaypoints;
	}

	for (int32 WaypointIndex = 0; WaypointIndex + 1 < OutPath.Waypoints.Num(); ++WaypointIndex)
	{
		OutPath.EstimatedLength += FVector::Distance(OutPath.Waypoints[WaypointIndex], OutPath.Waypoints[WaypointIndex + 1]);
	}

	OutPath.bIsValid = true;
	return true;
}
