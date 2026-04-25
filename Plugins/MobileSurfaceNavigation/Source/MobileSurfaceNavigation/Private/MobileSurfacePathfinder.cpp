#include "MobileSurfacePathfinder.h"

#include "MobileSurfaceNavigationQuery.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfacePathfinder, Log, All);

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

	struct FTraversalStep
	{
		int32 PreviousTriangle = INDEX_NONE;
		int32 PortalIndex = INDEX_NONE;
		int32 SpecialLinkIndex = INDEX_NONE;
		int32 SpecialLinkEntryNodeIndex = INDEX_NONE;
		int32 SpecialLinkExitNodeIndex = INDEX_NONE;
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

		return true;
	}

	static bool IsSpecialLinkAllowed(
		const FMobileSurfaceNavData& NavData,
		const FMobileSurfaceNavSpecialLink& Link,
		const FMobileSurfacePathQueryParams& Params)
	{
		if (!Link.bEnabled ||
			!IsTagAllowed(Link.LinkTag, Params.AllowedSpecialLinkTags, Params.ExcludedSpecialLinkTags) ||
			Link.GetNodeCount() < 2)
		{
			return false;
		}

		bool bHasAllowedNode = false;
		for (int32 NodeIndex = 0; NodeIndex < Link.GetNodeCount(); ++NodeIndex)
		{
			const int32 TriangleIndex = Link.GetNodeTriangleIndex(NodeIndex);
			if (TriangleIndex == INDEX_NONE)
			{
				continue;
			}

			if (IsTriangleAllowed(NavData, TriangleIndex, Params))
			{
				bHasAllowedNode = true;
			}
		}

		return bHasAllowedNode;
	}

	static double GetSpecialLinkTraversalFactor(const FMobileSurfaceNavSpecialLink& Link, const int32 EntryNodeIndex, const int32 ExitNodeIndex)
	{
		if (EntryNodeIndex == INDEX_NONE || ExitNodeIndex == INDEX_NONE || EntryNodeIndex == ExitNodeIndex)
		{
			return 1.0;
		}

		if (Link.LinkType == EMobileSurfaceNavSpecialLinkType::Jump)
		{
			return 1.0;
		}

		return FMath::Max(1, FMath::Abs(ExitNodeIndex - EntryNodeIndex));
	}

	static double GetSpecialLinkTraversalCost(
		const FMobileSurfaceNavData& NavData,
		const FMobileSurfaceNavSpecialLink& Link,
		const int32 EntryNodeIndex,
		const int32 ExitNodeIndex,
		const int32 NeighborTriangle)
	{
		const double TraversalFactor = GetSpecialLinkTraversalFactor(Link, EntryNodeIndex, ExitNodeIndex);
		const FMobileSurfaceNavRegionRuntimeState* RegionState = NavData.Triangles.IsValidIndex(NeighborTriangle)
			? GetRegionState(NavData, NavData.Triangles[NeighborTriangle].RegionId)
			: nullptr;
		const double TriangleCostMultiplier = RegionState ? FMath::Max(0.001f, RegionState->CostMultiplier) : 1.0;
		return FMath::Max(0.0f, Link.Cost) *
			TraversalFactor *
			FMath::Max(0.001f, Link.CostMultiplier) *
			TriangleCostMultiplier;
	}

	static TArray<int32> BuildTraversalNodeRoute(
		const FMobileSurfaceNavSpecialLink& Link,
		const int32 EntryNodeIndex,
		const int32 ExitNodeIndex)
	{
		TArray<int32> Route;
		if (EntryNodeIndex == INDEX_NONE || ExitNodeIndex == INDEX_NONE)
		{
			return Route;
		}

		if (Link.LinkType == EMobileSurfaceNavSpecialLinkType::Ladder ||
			Link.TraversalMode == EMobileSurfaceNavLinkTraversalMode::Sequential)
		{
			const int32 Step = ExitNodeIndex >= EntryNodeIndex ? 1 : -1;
			for (int32 NodeIndex = EntryNodeIndex; ; NodeIndex += Step)
			{
				Route.Add(NodeIndex);
				if (NodeIndex == ExitNodeIndex)
				{
					break;
				}
			}
			return Route;
		}

		Route.Add(EntryNodeIndex);
		Route.Add(ExitNodeIndex);
		return Route;
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

	static FVector GetClosestPointOnTriangle(
		const FMobileSurfaceNavData& NavData,
		const int32 TriangleIndex,
		const FVector& LocalPosition)
	{
		if (!NavData.Triangles.IsValidIndex(TriangleIndex))
		{
			return LocalPosition;
		}

		const FMobileSurfaceNavTriangle& Triangle = NavData.Triangles[TriangleIndex];
		const FVector A = NavData.Vertices[Triangle.VertexIndices.X].LocalPosition;
		const FVector B = NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition;
		const FVector C = NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition;
		return FMath::ClosestPointOnTriangleToPoint(LocalPosition, A, B, C);
	}

	static void AddWaypointIfNeeded(TArray<FVector>& Waypoints, const FVector& Point)
	{
		if (Waypoints.IsEmpty() || !NearlySamePoint(Waypoints.Last(), Point))
		{
			Waypoints.Add(Point);
		}
	}

	static void AppendWaypointsIfNeeded(TArray<FVector>& InOutWaypoints, const TArray<FVector>& NewWaypoints)
	{
		for (const FVector& Waypoint : NewWaypoints)
		{
			AddWaypointIfNeeded(InOutWaypoints, Waypoint);
		}
	}

	static TArray<FVector> RunFunnel(
		const FMobileSurfaceNavData& NavData,
		const TArray<int32>& TrianglePath,
		const FVector& Start,
		const FVector& End);

	static const FMobileSurfaceNavPortal* FindPortalBetweenTriangles(
		const FMobileSurfaceNavData& NavData,
		const int32 TriangleA,
		const int32 TriangleB)
	{
		if (!NavData.TriangleAdjacency.IsValidIndex(TriangleA))
		{
			return nullptr;
		}

		for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[TriangleA].Neighbors)
		{
			if (Adjacency.NeighborTriangleIndex == TriangleB && NavData.Portals.IsValidIndex(Adjacency.PortalIndex))
			{
				return &NavData.Portals[Adjacency.PortalIndex];
			}
		}

		return nullptr;
	}

	static FVector ComputeBestTransitionPointOnPortal(
		const FMobileSurfaceNavPortal& Portal,
		const FVector& Start,
		const FVector& End)
	{
		const FVector PortalStart = Portal.LeftPoint;
		const FVector PortalEnd = Portal.RightPoint;
		const FVector PortalVector = PortalEnd - PortalStart;
		const double PortalLengthSquared = PortalVector.SizeSquared();
		if (PortalLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Portal.Center;
		}

		const FVector PathVector = End - Start;
		const double PathLengthSquared = PathVector.SizeSquared();
		if (PathLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FMath::ClosestPointOnSegment(Start, PortalStart, PortalEnd);
		}

		const double A = PortalLengthSquared;
		const double B = FVector::DotProduct(PortalVector, PathVector);
		const double C = PathLengthSquared;
		const FVector W = PortalStart - Start;
		const double D = FVector::DotProduct(PortalVector, W);
		const double E = FVector::DotProduct(PathVector, W);
		const double Denom = (A * C) - (B * B);

		double PortalT = 0.5;
		if (FMath::Abs(Denom) > UE_DOUBLE_SMALL_NUMBER)
		{
			PortalT = ((B * E) - (C * D)) / Denom;
		}
		else
		{
			PortalT = static_cast<double>(FVector::DotProduct(Start - PortalStart, PortalVector)) / A;
		}

		PortalT = FMath::Clamp(PortalT, 0.0, 1.0);
		return PortalStart + (PortalVector * PortalT);
	}

	static void AppendRegionFunnelWaypoints(
		const FMobileSurfaceNavData& NavData,
		const TArray<int32>& TrianglePath,
		const FVector& Start,
		const FVector& End,
		TArray<FVector>& InOutWaypoints)
	{
		if (TrianglePath.IsEmpty())
		{
			AddWaypointIfNeeded(InOutWaypoints, Start);
			AddWaypointIfNeeded(InOutWaypoints, End);
			return;
		}

		int32 RegionSegmentStartIndex = 0;
		FVector RegionSegmentStartPosition = Start;

		for (int32 PathIndex = 0; PathIndex + 1 < TrianglePath.Num(); ++PathIndex)
		{
			const int32 CurrentTriangleIndex = TrianglePath[PathIndex];
			const int32 NextTriangleIndex = TrianglePath[PathIndex + 1];
			if (!NavData.Triangles.IsValidIndex(CurrentTriangleIndex) || !NavData.Triangles.IsValidIndex(NextTriangleIndex))
			{
				continue;
			}

			const int32 CurrentRegionId = NavData.Triangles[CurrentTriangleIndex].RegionId;
			const int32 NextRegionId = NavData.Triangles[NextTriangleIndex].RegionId;
			if (CurrentRegionId == NextRegionId)
			{
				continue;
			}

			const FMobileSurfaceNavPortal* RegionTransitionPortal = FindPortalBetweenTriangles(NavData, CurrentTriangleIndex, NextTriangleIndex);
			if (!RegionTransitionPortal)
			{
				continue;
			}

			const FVector RegionTransitionPoint = ComputeBestTransitionPointOnPortal(
				*RegionTransitionPortal,
				RegionSegmentStartPosition,
				End);

			TArray<int32> RegionTrianglePath;
			for (int32 SegmentIndex = RegionSegmentStartIndex; SegmentIndex <= PathIndex; ++SegmentIndex)
			{
				RegionTrianglePath.Add(TrianglePath[SegmentIndex]);
			}

			AppendWaypointsIfNeeded(
				InOutWaypoints,
				RunFunnel(
					NavData,
					RegionTrianglePath,
					RegionSegmentStartPosition,
					RegionTransitionPoint));

			RegionSegmentStartIndex = PathIndex + 1;
			RegionSegmentStartPosition = RegionTransitionPoint;
		}

		TArray<int32> FinalRegionTrianglePath;
		for (int32 SegmentIndex = RegionSegmentStartIndex; SegmentIndex < TrianglePath.Num(); ++SegmentIndex)
		{
			FinalRegionTrianglePath.Add(TrianglePath[SegmentIndex]);
		}

		AppendWaypointsIfNeeded(
			InOutWaypoints,
			RunFunnel(
				NavData,
				FinalRegionTrianglePath,
				RegionSegmentStartPosition,
				End));
	}

	static EMobileSurfaceNavPathSegmentType ToPathSegmentType(const EMobileSurfaceNavSpecialLinkType LinkType)
	{
		switch (LinkType)
		{
		case EMobileSurfaceNavSpecialLinkType::Ladder:
			return EMobileSurfaceNavPathSegmentType::Ladder;
		case EMobileSurfaceNavSpecialLinkType::Elevator:
			return EMobileSurfaceNavPathSegmentType::Elevator;
		case EMobileSurfaceNavSpecialLinkType::Jump:
			return EMobileSurfaceNavPathSegmentType::Jump;
		default:
			return EMobileSurfaceNavPathSegmentType::Walk;
		}
	}

	static void AddPathSegment(
		FMobileSurfaceNavPath& Path,
		const EMobileSurfaceNavPathSegmentType SegmentType,
		const int32 StartWaypointIndex,
		const int32 EndWaypointIndex,
		const int32 SpecialLinkIndex = INDEX_NONE,
		const EMobileSurfaceNavSpecialLinkType SpecialLinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		const int32 SpecialLinkEntryNodeIndex = INDEX_NONE,
		const int32 SpecialLinkExitNodeIndex = INDEX_NONE,
		const TArray<int32>& SpecialLinkTraversalNodeIndices = TArray<int32>())
	{
		if (StartWaypointIndex == INDEX_NONE || EndWaypointIndex == INDEX_NONE || EndWaypointIndex <= StartWaypointIndex)
		{
			return;
		}

		FMobileSurfaceNavPathSegment& Segment = Path.Segments.AddDefaulted_GetRef();
		Segment.SegmentType = SegmentType;
		Segment.StartWaypointIndex = StartWaypointIndex;
		Segment.EndWaypointIndex = EndWaypointIndex;
		Segment.SpecialLinkIndex = SpecialLinkIndex;
		Segment.SpecialLinkType = SpecialLinkType;
		Segment.SpecialLinkEntryNodeIndex = SpecialLinkEntryNodeIndex;
		Segment.SpecialLinkExitNodeIndex = SpecialLinkExitNodeIndex;
		Segment.SpecialLinkTraversalNodeIndices = SpecialLinkTraversalNodeIndices;
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

	FVector ResolvedStartLocalPosition = FVector::ZeroVector;
	FVector ResolvedEndLocalPosition = FVector::ZeroVector;
	ResolvedStartLocalPosition = StartTriangleIndex != INDEX_NONE
		? StartLocalPosition
		: MobileSurfaceNavigation::Pathfinder::GetClosestPointOnTriangle(NavData, FallbackStartTriangle, StartLocalPosition);
	ResolvedEndLocalPosition = EndTriangleIndex != INDEX_NONE
		? EndLocalPosition
		: MobileSurfaceNavigation::Pathfinder::GetClosestPointOnTriangle(NavData, FallbackEndTriangle, EndLocalPosition);

	OutPath.StartTriangleIndex = FallbackStartTriangle;
	OutPath.EndTriangleIndex = FallbackEndTriangle;
	OutPath.RuntimeStateRevision = NavData.RuntimeStateRevision;

	UE_LOG(
		LogMobileSurfacePathfinder,
		Log,
		TEXT("FindPath: requestedRadius=%.1f navTriangles=%d startTri=%d fallbackStart=%d endTri=%d fallbackEnd=%d resolvedStart=%s resolvedEnd=%s"),
		Params.AgentRadius,
		NavData.Triangles.Num(),
		StartTriangleIndex,
		FallbackStartTriangle,
		EndTriangleIndex,
		FallbackEndTriangle,
		*ResolvedStartLocalPosition.ToCompactString(),
		*ResolvedEndLocalPosition.ToCompactString());

	if (FallbackStartTriangle == FallbackEndTriangle)
	{
		OutPath.bIsValid = true;
		OutPath.TriangleIndices = { FallbackStartTriangle };
		OutPath.RawWaypoints = { ResolvedStartLocalPosition, ResolvedEndLocalPosition };
		OutPath.Waypoints = OutPath.RawWaypoints;
		MobileSurfaceNavigation::Pathfinder::AddPathSegment(
			OutPath,
			EMobileSurfaceNavPathSegmentType::Walk,
			0,
			1);
		OutPath.EstimatedLength = FVector::Distance(ResolvedStartLocalPosition, ResolvedEndLocalPosition);
		return true;
	}

	struct FNodeRecord
	{
		double GScore = TNumericLimits<double>::Max();
		double FScore = TNumericLimits<double>::Max();
		MobileSurfaceNavigation::Pathfinder::FTraversalStep CameFrom;
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
				Records[NeighborTriangle].CameFrom.PreviousTriangle = CurrentTriangle;
				Records[NeighborTriangle].CameFrom.PortalIndex = Adjacency.PortalIndex;
				Records[NeighborTriangle].CameFrom.SpecialLinkIndex = INDEX_NONE;
				Records[NeighborTriangle].GScore = TentativeG;
				Records[NeighborTriangle].FScore = TentativeG + FVector::Distance(
					NavData.Triangles[NeighborTriangle].Center,
					NavData.Triangles[FallbackEndTriangle].Center);
				MobileSurfaceNavigation::Pathfinder::PushOpenNode(OpenSet, { NeighborTriangle, Records[NeighborTriangle].FScore });
			}
		}

		for (int32 LinkIndex = 0; LinkIndex < NavData.SpecialLinks.Num(); ++LinkIndex)
		{
			const FMobileSurfaceNavSpecialLink& Link = NavData.SpecialLinks[LinkIndex];
			if (!MobileSurfaceNavigation::Pathfinder::IsSpecialLinkAllowed(NavData, Link, Params))
			{
				continue;
			}

			for (int32 EntryNodeIndex = 0; EntryNodeIndex < Link.GetNodeCount(); ++EntryNodeIndex)
			{
				if (Link.GetNodeTriangleIndex(EntryNodeIndex) != CurrentTriangle)
				{
					continue;
				}

				for (int32 ExitNodeIndex = 0; ExitNodeIndex < Link.GetNodeCount(); ++ExitNodeIndex)
				{
					if (ExitNodeIndex == EntryNodeIndex)
					{
						continue;
					}

					if (!Link.bBidirectional && ExitNodeIndex < EntryNodeIndex)
					{
						continue;
					}

					const int32 NeighborTriangle = Link.GetNodeTriangleIndex(ExitNodeIndex);
					if (NeighborTriangle == INDEX_NONE || Records[NeighborTriangle].bClosed)
					{
						continue;
					}

					if (!MobileSurfaceNavigation::Pathfinder::IsTriangleAllowed(NavData, NeighborTriangle, Params))
					{
						continue;
					}

					const double LinkCost = MobileSurfaceNavigation::Pathfinder::GetSpecialLinkTraversalCost(
						NavData,
						Link,
						EntryNodeIndex,
						ExitNodeIndex,
						NeighborTriangle);
					const double TentativeG = Records[CurrentTriangle].GScore + LinkCost;
					if (TentativeG < Records[NeighborTriangle].GScore)
					{
						Records[NeighborTriangle].CameFrom.PreviousTriangle = CurrentTriangle;
						Records[NeighborTriangle].CameFrom.PortalIndex = INDEX_NONE;
						Records[NeighborTriangle].CameFrom.SpecialLinkIndex = LinkIndex;
						Records[NeighborTriangle].CameFrom.SpecialLinkEntryNodeIndex = EntryNodeIndex;
						Records[NeighborTriangle].CameFrom.SpecialLinkExitNodeIndex = ExitNodeIndex;
						Records[NeighborTriangle].GScore = TentativeG;
						Records[NeighborTriangle].FScore = TentativeG + FVector::Distance(
							NavData.Triangles[NeighborTriangle].Center,
							NavData.Triangles[FallbackEndTriangle].Center);
						MobileSurfaceNavigation::Pathfinder::PushOpenNode(OpenSet, { NeighborTriangle, Records[NeighborTriangle].FScore });
					}
				}
			}
		}
	}

	if (Records[FallbackEndTriangle].CameFrom.PreviousTriangle == INDEX_NONE)
	{
		return false;
	}

	TArray<int32> ReversedTriangles;
	for (int32 Cursor = FallbackEndTriangle; Cursor != INDEX_NONE; Cursor = Records[Cursor].CameFrom.PreviousTriangle)
	{
		ReversedTriangles.Add(Cursor);
	}
	Algo::Reverse(ReversedTriangles);
	OutPath.TriangleIndices = ReversedTriangles;

	OutPath.RawWaypoints.Add(ResolvedStartLocalPosition);
	OutPath.Waypoints.Reset();
	int32 FunnelSegmentStartPathIndex = 0;
	FVector FunnelSegmentStartPosition = ResolvedStartLocalPosition;

	for (int32 PathIndex = 0; PathIndex + 1 < ReversedTriangles.Num(); ++PathIndex)
	{
		const int32 CurrentTriangle = ReversedTriangles[PathIndex];
		const int32 NextTriangle = ReversedTriangles[PathIndex + 1];
		if (Records[NextTriangle].CameFrom.SpecialLinkIndex != INDEX_NONE && NavData.SpecialLinks.IsValidIndex(Records[NextTriangle].CameFrom.SpecialLinkIndex))
		{
			const FMobileSurfaceNavSpecialLink& Link = NavData.SpecialLinks[Records[NextTriangle].CameFrom.SpecialLinkIndex];
			const int32 EntryNodeIndex = Records[NextTriangle].CameFrom.SpecialLinkEntryNodeIndex;
			const int32 ExitNodeIndex = Records[NextTriangle].CameFrom.SpecialLinkExitNodeIndex;
			const FVector LinkEntryPosition = Link.GetNodeLocalPosition(EntryNodeIndex);
			const FVector LinkExitPosition = Link.GetNodeLocalPosition(ExitNodeIndex);

			TArray<int32> FunnelSegmentTriangles;
			for (int32 SegmentIndex = FunnelSegmentStartPathIndex; SegmentIndex <= PathIndex; ++SegmentIndex)
			{
				FunnelSegmentTriangles.Add(ReversedTriangles[SegmentIndex]);
			}
			const int32 WalkSegmentStartWaypointIndex = OutPath.Waypoints.IsEmpty() ? 0 : (OutPath.Waypoints.Num() - 1);
			MobileSurfaceNavigation::Pathfinder::AppendRegionFunnelWaypoints(
				NavData,
				FunnelSegmentTriangles,
				FunnelSegmentStartPosition,
				LinkEntryPosition,
				OutPath.Waypoints);
			MobileSurfaceNavigation::Pathfinder::AddPathSegment(
				OutPath,
				EMobileSurfaceNavPathSegmentType::Walk,
				WalkSegmentStartWaypointIndex,
				OutPath.Waypoints.Num() - 1);
			const int32 SpecialSegmentStartWaypointIndex = OutPath.Waypoints.Num() - 1;
			MobileSurfaceNavigation::Pathfinder::AddWaypointIfNeeded(OutPath.Waypoints, LinkExitPosition);
			MobileSurfaceNavigation::Pathfinder::AddPathSegment(
				OutPath,
				MobileSurfaceNavigation::Pathfinder::ToPathSegmentType(Link.LinkType),
				SpecialSegmentStartWaypointIndex,
				OutPath.Waypoints.Num() - 1,
				Records[NextTriangle].CameFrom.SpecialLinkIndex,
				Link.LinkType,
				EntryNodeIndex,
				ExitNodeIndex,
				MobileSurfaceNavigation::Pathfinder::BuildTraversalNodeRoute(Link, EntryNodeIndex, ExitNodeIndex));

			OutPath.RawWaypoints.Add(LinkEntryPosition);
			OutPath.RawWaypoints.Add(LinkExitPosition);

			FunnelSegmentStartPathIndex = PathIndex + 1;
			FunnelSegmentStartPosition = LinkExitPosition;
			continue;
		}

		if (NavData.TriangleAdjacency.IsValidIndex(CurrentTriangle))
		{
			for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[CurrentTriangle].Neighbors)
			{
				if (Adjacency.NeighborTriangleIndex == NextTriangle && NavData.Portals.IsValidIndex(Adjacency.PortalIndex))
				{
					OutPath.RawWaypoints.Add(NavData.Portals[Adjacency.PortalIndex].Center);
					break;
				}
			}
		}
	}
	OutPath.RawWaypoints.Add(ResolvedEndLocalPosition);

	TArray<int32> FinalFunnelSegmentTriangles;
	for (int32 SegmentIndex = FunnelSegmentStartPathIndex; SegmentIndex < ReversedTriangles.Num(); ++SegmentIndex)
	{
		FinalFunnelSegmentTriangles.Add(ReversedTriangles[SegmentIndex]);
	}
	const int32 FinalWalkSegmentStartWaypointIndex = OutPath.Waypoints.IsEmpty() ? 0 : (OutPath.Waypoints.Num() - 1);
	MobileSurfaceNavigation::Pathfinder::AppendRegionFunnelWaypoints(
		NavData,
		FinalFunnelSegmentTriangles,
		FunnelSegmentStartPosition,
		ResolvedEndLocalPosition,
		OutPath.Waypoints);
	MobileSurfaceNavigation::Pathfinder::AddPathSegment(
		OutPath,
		EMobileSurfaceNavPathSegmentType::Walk,
		FinalWalkSegmentStartWaypointIndex,
		OutPath.Waypoints.Num() - 1);

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
