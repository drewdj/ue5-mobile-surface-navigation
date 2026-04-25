#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.h"

class UStaticMeshComponent;
class USceneComponent;

struct FMobileSurfaceNavBuildSettings
{
	float VertexWeldTolerance = 0.01f;
	float PlanarRegionAngleToleranceDegrees = 5.0f;
	float PlanarRegionDistanceTolerance = 1.0f;
	float BoundarySimplificationTolerance = 1.0f;
};

class FMobileSurfaceNavigationBuilder
{
public:
	static bool BuildFromStaticMeshComponent(
		const UStaticMeshComponent* SourceComponent,
		const USceneComponent* TargetSpaceComponent,
		const FMobileSurfaceNavBuildSettings& Settings,
		FMobileSurfaceNavData& OutNavData,
		FString& OutError);

	static bool BuildAgentRadiusLayer(
		const FMobileSurfaceNavData& SourceNavData,
		float AgentRadius,
		FMobileSurfaceNavData& OutNavData,
		FString& OutError);
};
