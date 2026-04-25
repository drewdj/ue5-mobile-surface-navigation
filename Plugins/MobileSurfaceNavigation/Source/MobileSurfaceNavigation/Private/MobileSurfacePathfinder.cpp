#include "MobileSurfacePathfinder.h"

#include "MobileSurfaceNavigationQuery.h"

namespace MobileSurfaceNavigation::Pathfinder
{
#include "MobileSurfacePathfinderInternal.inl"
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
