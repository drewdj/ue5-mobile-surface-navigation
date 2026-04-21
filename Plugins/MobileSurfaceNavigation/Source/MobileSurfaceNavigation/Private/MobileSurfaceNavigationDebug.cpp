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
			const FVector Vertex0 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.X].LocalPosition);
			const FVector Vertex1 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition);
			const FVector Vertex2 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition);
			const FColor TriangleColor = MobileSurfaceNavigation::Debug::GetRegionColor(Triangle.RegionId);

			DrawDebugLine(World, Vertex0, Vertex1, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);
			DrawDebugLine(World, Vertex1, Vertex2, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);
			DrawDebugLine(World, Vertex2, Vertex0, TriangleColor, false, Settings.Duration, DepthPriority, Settings.LineThickness);

			if (Settings.bDrawTriangleNormals)
			{
				const FVector Center = LocalToWorld.TransformPosition(Triangle.Center);
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

				const FVector Start = LocalToWorld.TransformPosition(NavData.Vertices[Loop.VertexIndices[VertexIndex]].LocalPosition);
				const FVector End = LocalToWorld.TransformPosition(NavData.Vertices[Loop.VertexIndices[VertexIndex + 1]].LocalPosition);
				DrawDebugLine(World, Start, End, LoopColor, false, Settings.Duration, DepthPriority, Settings.LineThickness * 2.0f);
			}
		}
	}

	if (Settings.bDrawPortals)
	{
		for (const FMobileSurfaceNavPortal& Portal : NavData.Portals)
		{
			if (!NavData.Triangles.IsValidIndex(Portal.TriangleA) || !NavData.Triangles.IsValidIndex(Portal.TriangleB))
			{
				continue;
			}

			const FVector CenterA = LocalToWorld.TransformPosition(NavData.Triangles[Portal.TriangleA].Center);
			const FVector CenterB = LocalToWorld.TransformPosition(NavData.Triangles[Portal.TriangleB].Center);
			DrawDebugLine(World, CenterA, CenterB, FColor::Magenta, false, Settings.Duration, DepthPriority, Settings.LineThickness * 1.5f);
			DrawDebugPoint(World, LocalToWorld.TransformPosition(Portal.Center), Settings.VertexSize * 0.75f, FColor::Magenta, false, Settings.Duration, DepthPriority);
		}
	}

	if (Settings.bDrawVertices)
	{
		for (const int32 VertexIndex : UsedVertexIndices)
		{
			const FVector VertexPosition = LocalToWorld.TransformPosition(NavData.Vertices[VertexIndex].LocalPosition);
			const FColor VertexColor = BoundaryVertexIndices.Contains(VertexIndex) ? FColor::Green : FColor::Yellow;
			const float VertexSize = BoundaryVertexIndices.Contains(VertexIndex) ? Settings.VertexSize * 1.25f : Settings.VertexSize;
			DrawDebugPoint(World, VertexPosition, VertexSize, VertexColor, false, Settings.Duration, DepthPriority);
		}
	}
}
