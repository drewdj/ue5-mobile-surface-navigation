#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.h"

class FMobileSurfaceNavigationQuery
{
public:
	static int32 FindContainingTriangle(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition);
	static int32 FindNearestTriangle(const FMobileSurfaceNavData& NavData, const FVector& LocalPosition);
	static bool FindPath(const FMobileSurfaceNavData& NavData, const FVector& StartLocalPosition, const FVector& EndLocalPosition, FMobileSurfaceNavPath& OutPath, float AgentRadius = 0.0f);
};
