#include "MobileSurfaceNavigationDebug.h"

#include "Components/SceneComponent.h"
#include "Containers/Set.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

namespace MobileSurfaceNavigation::Debug
{
	static FColor GetRegionColor(const int32 RegionId)
	{
		static const FColor Palette[] =
		{
			FColor(75, 170, 255),
			FColor(255, 110, 64),
			FColor(80, 200, 120),
			FColor(255, 210, 90),
			FColor(210, 120, 255),
			FColor(255, 140, 190)
		};

		if (RegionId < 0)
		{
			return FColor(75, 170, 255);
		}

		return Palette[RegionId % UE_ARRAY_COUNT(Palette)];
	}

	static FColor GetPortalColor(const FMobileSurfaceNavData& NavData, const int32 PortalIndex)
	{
		if (!NavData.PortalRuntimeStates.IsValidIndex(PortalIndex))
		{
			return FColor::Magenta;
		}

		const FMobileSurfaceNavPortalRuntimeState& PortalState = NavData.PortalRuntimeStates[PortalIndex];
		if (!PortalState.bOpen)
		{
			return FColor::Red;
		}

		if (PortalState.CostMultiplier > 1.01f || PortalState.ExtraCost > 0.0f || PortalState.EffectiveWidthOverride > 0.0f)
		{
			return FColor::Orange;
		}

		return FColor::Green;
	}

	static FColor GetBuildDebugSegmentColor(const EMobileSurfaceNavBuildDebugSegmentType SegmentType)
	{
		switch (SegmentType)
		{
		case EMobileSurfaceNavBuildDebugSegmentType::PhysicalBoundary:
			return FColor::Red;
		case EMobileSurfaceNavBuildDebugSegmentType::RegionSeam:
			return FColor::Cyan;
		default:
			return FColor::White;
		}
	}

	static FColor GetBuildDebugLoopColor(
		const EMobileSurfaceNavBuildDebugLoopType LoopType,
		const EMobileSurfaceBoundaryKind BoundaryKind)
	{
		if (LoopType == EMobileSurfaceNavBuildDebugLoopType::Original)
		{
			return BoundaryKind == EMobileSurfaceBoundaryKind::Hole ? FColor(255, 170, 0) : FColor(0, 220, 120);
		}

		return BoundaryKind == EMobileSurfaceBoundaryKind::Hole ? FColor(255, 90, 255) : FColor(255, 255, 0);
	}
}

void FMobileSurfaceNavigationDebug::DrawNavData(
	UWorld* World,
	const USceneComponent* SpaceComponent,
	const FMobileSurfaceNavData& NavData,
	const FMobileSurfaceNavDebugSettings& Settings)
{
	if (!World || !SpaceComponent || !NavData.bIsValid)
	{
		return;
	}

	const FTransform LocalToWorld = SpaceComponent->GetComponentTransform();
	constexpr uint8 DepthPriority = 0;
	TSet<int32> UsedVertexIndices;
	TSet<int32> BoundaryVertexIndices;

	for (const FMobileSurfaceNavTriangle& Triangle : NavData.Triangles)
	{
		UsedVertexIndices.Add(Triangle.VertexIndices.X);
		UsedVertexIndices.Add(Triangle.VertexIndices.Y);
		UsedVertexIndices.Add(Triangle.VertexIndices.Z);
	}

	if (Settings.bDrawTriangles)
	{
		for (const FMobileSurfaceNavTriangle& Triangle : NavData.Triangles)
		{
			const FVector Vertex0 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.X].LocalPosition) + Settings.WorldOffset;
			const FVector Vertex1 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition) + Settings.WorldOffset;
			const FVector Vertex2 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition) + Settings.WorldOffset;
			const FColor TriangleColor = MobileSurfaceNavigation::Debug::GetRegionColor(Triangle.RegionId);

			DrawDebugLine(World, Vertex0, Vertex1, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);
			DrawDebugLine(World, Vertex1, Vertex2, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);
			DrawDebugLine(World, Vertex2, Vertex0, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);

			if (Settings.bDrawTriangleNormals)
			{
				const FVector Center = LocalToWorld.TransformPosition(Triangle.Center) + Settings.WorldOffset;
				const FVector NormalEnd = Center + LocalToWorld.TransformVectorNoScale(Triangle.Normal * Settings.NormalLength);
				DrawDebugLine(World, Center, NormalEnd, FColor::Cyan, false, Settings.Duration, DepthPriority, Settings.LineThickness);
			}
		}
	}

	if (Settings.bDrawBoundaryEdges)
	{
		for (const FMobileSurfaceNavBoundaryLoop& Loop : NavData.BoundaryLoops)
		{
			const FColor LoopColor = Loop.Kind == EMobileSurfaceBoundaryKind::Hole ? FColor::Orange : FColor::Green;
			for (int32 VertexIndex = 0; VertexIndex + 1 < Loop.VertexIndices.Num(); ++VertexIndex)
			{
				BoundaryVertexIndices.Add(Loop.VertexIndices[VertexIndex]);
				BoundaryVertexIndices.Add(Loop.VertexIndices[VertexIndex + 1]);

				const FVector Start = LocalToWorld.TransformPosition(NavData.Vertices[Loop.VertexIndices[VertexIndex]].LocalPosition) + Settings.WorldOffset;
				const FVector End = LocalToWorld.TransformPosition(NavData.Vertices[Loop.VertexIndices[VertexIndex + 1]].LocalPosition) + Settings.WorldOffset;
				DrawDebugLine(World, Start, End, LoopColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 2.0f);
			}
		}
	}

	if (Settings.bDrawBuildDebugData)
	{
		for (const FMobileSurfaceNavBuildDebugSegment& Segment : NavData.BuildDebugSegments)
		{
			if (Settings.BuildDebugRegionId != INDEX_NONE && Segment.RegionId != Settings.BuildDebugRegionId)
			{
				continue;
			}

			const FVector Start = LocalToWorld.TransformPosition(Segment.StartLocalPosition) + Settings.WorldOffset;
			const FVector End = LocalToWorld.TransformPosition(Segment.EndLocalPosition) + Settings.WorldOffset;
			DrawDebugLine(
				World,
				Start,
				End,
				MobileSurfaceNavigation::Debug::GetBuildDebugSegmentColor(Segment.SegmentType),
				false,
				Settings.Duration,
				DepthPriority,
				Settings.LineThickness * 3.0f);
		}

		for (const FMobileSurfaceNavBuildDebugLoop& Loop : NavData.BuildDebugLoops)
		{
			if (Settings.BuildDebugRegionId != INDEX_NONE && Loop.RegionId != Settings.BuildDebugRegionId)
			{
				continue;
			}

			if (Loop.LocalPoints.Num() < 2)
			{
				continue;
			}

			const FColor LoopColor = MobileSurfaceNavigation::Debug::GetBuildDebugLoopColor(Loop.LoopType, Loop.Kind);
			for (int32 PointIndex = 0; PointIndex + 1 < Loop.LocalPoints.Num(); ++PointIndex)
			{
				const FVector Start = LocalToWorld.TransformPosition(Loop.LocalPoints[PointIndex]) + Settings.WorldOffset;
				const FVector End = LocalToWorld.TransformPosition(Loop.LocalPoints[PointIndex + 1]) + Settings.WorldOffset;
				DrawDebugLine(World, Start, End, LoopColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 2.5f);
			}

			if (Loop.bClosed)
			{
				const FVector Start = LocalToWorld.TransformPosition(Loop.LocalPoints.Last()) + Settings.WorldOffset;
				const FVector End = LocalToWorld.TransformPosition(Loop.LocalPoints[0]) + Settings.WorldOffset;
				DrawDebugLine(World, Start, End, LoopColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 2.5f);
			}

			const FString Label = FString::Printf(
				TEXT("R%d %s %s"),
				Loop.RegionId,
				Loop.LoopType == EMobileSurfaceNavBuildDebugLoopType::Inset ? TEXT("Inset") : TEXT("Original"),
				Loop.Kind == EMobileSurfaceBoundaryKind::Hole ? TEXT("Hole") : TEXT("Outer"));
			DrawDebugString(
				World,
				LocalToWorld.TransformPosition(Loop.LocalPoints[0]) + Settings.WorldOffset + FVector(0.0f, 0.0f, 20.0f),
				Label,
				nullptr,
				LoopColor,
				Settings.Duration,
				false,
				1.0f);
		}
	}

	if (Settings.bDrawPortals)
	{
		for (int32 PortalIndex = 0; PortalIndex < NavData.Portals.Num(); ++PortalIndex)
		{
			const FMobileSurfaceNavPortal& Portal = NavData.Portals[PortalIndex];
			if (!NavData.Triangles.IsValidIndex(Portal.TriangleA) || !NavData.Triangles.IsValidIndex(Portal.TriangleB))
			{
				continue;
			}

			const FColor PortalColor = MobileSurfaceNavigation::Debug::GetPortalColor(NavData, PortalIndex);
			const FVector CenterA = LocalToWorld.TransformPosition(NavData.Triangles[Portal.TriangleA].Center) + Settings.WorldOffset;
			const FVector CenterB = LocalToWorld.TransformPosition(NavData.Triangles[Portal.TriangleB].Center) + Settings.WorldOffset;
			DrawDebugLine(World, CenterA, CenterB, PortalColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 1.5f);
			const FVector PortalCenter = LocalToWorld.TransformPosition(Portal.Center) + Settings.WorldOffset;
			DrawDebugPoint(World, PortalCenter, Settings.VertexSize * 0.75f, PortalColor, false, Settings.Duration, DepthPriority);

			if (PortalIndex == Settings.HighlightPortalIndex)
			{
				DrawDebugLine(World, CenterA + FVector(0.0, 0.0, 8.0), CenterB + FVector(0.0, 0.0, 8.0), FColor::White, false, Settings.Duration, DepthPriority, Settings.LineThickness * 3.0f);
				DrawDebugPoint(World, PortalCenter + FVector(0.0, 0.0, 8.0), Settings.VertexSize * 1.5f, FColor::White, false, Settings.Duration, DepthPriority);
			}
		}
	}

	if (Settings.bDrawSpecialLinks)
	{
		for (int32 LinkIndex = 0; LinkIndex < NavData.SpecialLinks.Num(); ++LinkIndex)
		{
			const FMobileSurfaceNavSpecialLink& Link = NavData.SpecialLinks[LinkIndex];
			const FColor LinkColor = LinkIndex == Settings.HighlightSpecialLinkIndex ? FColor::White : Link.bEnabled ? FColor::Cyan : FColor::Red;
			FVector LabelAnchor = FVector::ZeroVector;
			for (int32 NodeIndex = 0; NodeIndex < Link.GetNodeCount(); ++NodeIndex)
			{
				const FVector NodeWorld = LocalToWorld.TransformPosition(Link.GetNodeLocalPosition(NodeIndex)) + Settings.WorldOffset;
				if (NodeIndex == 0)
				{
					LabelAnchor = NodeWorld;
				}
				else
				{
					const FVector PreviousNodeWorld = LocalToWorld.TransformPosition(Link.GetNodeLocalPosition(NodeIndex - 1));
					DrawDebugLine(World, PreviousNodeWorld, NodeWorld, LinkColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 3.0f);
					LabelAnchor = (LabelAnchor + NodeWorld) * 0.5f;
				}

				DrawDebugPoint(World, NodeWorld, Settings.VertexSize * 1.5f, LinkColor, false, Settings.Duration, DepthPriority);
			}

			const FString Label = Link.LinkId.IsNone()
				? FString::Printf(TEXT("L%d"), LinkIndex)
				: FString::Printf(TEXT("L%d %s"), LinkIndex, *Link.LinkId.ToString());
			DrawDebugString(World, LabelAnchor + FVector(0.0, 0.0, 35.0), Label, nullptr, LinkColor, Settings.Duration, false, 1.0f);
		}
	}

	if (Settings.bDrawVertices)
	{
		for (const int32 VertexIndex : UsedVertexIndices)
		{
			const FVector VertexPosition = LocalToWorld.TransformPosition(NavData.Vertices[VertexIndex].LocalPosition) + Settings.WorldOffset;
			const FColor VertexColor = BoundaryVertexIndices.Contains(VertexIndex) ? FColor::Green : FColor::Yellow;
			const float VertexSize = BoundaryVertexIndices.Contains(VertexIndex) ? Settings.VertexSize * 1.25f : Settings.VertexSize;
			DrawDebugPoint(World, VertexPosition, VertexSize, VertexColor, false, Settings.Duration, DepthPriority);
		}
	}
}
