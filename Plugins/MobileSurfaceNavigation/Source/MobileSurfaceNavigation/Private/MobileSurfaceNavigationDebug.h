#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.h"

class UWorld;
class USceneComponent;

struct FMobileSurfaceNavDebugSettings
{
	bool bDrawVertices = true;
	bool bDrawBoundaryEdges = true;
	bool bDrawTriangles = true;
	bool bDrawTriangleNormals = false;
	bool bDrawPortals = false;
	bool bDrawPortalLabels = false;
	bool bDrawSpecialLinks = false;
	int32 HighlightPortalIndex = INDEX_NONE;
	int32 HighlightSpecialLinkIndex = INDEX_NONE;
	float VertexSize = 8.0f;
	float LineThickness = 1.5f;
	float NormalLength = 30.0f;
	float Duration = 0.0f;
	FVector WorldOffset = FVector::ZeroVector;
};

class FMobileSurfaceNavigationDebug
{
public:
	static void DrawNavData(
		UWorld* World,
		const USceneComponent* SpaceComponent,
		const FMobileSurfaceNavData& NavData,
		const FMobileSurfaceNavDebugSettings& Settings);
};
