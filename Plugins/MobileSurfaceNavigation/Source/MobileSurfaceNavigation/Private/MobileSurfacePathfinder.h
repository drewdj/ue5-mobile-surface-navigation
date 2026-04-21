#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.h"

class FMobileSurfacePathfinder
{
public:
	static bool FindPath(
		const FMobileSurfaceNavData& NavData,
		const FVector& StartLocalPosition,
		const FVector& EndLocalPosition,
		const FMobileSurfacePathQueryParams& Params,
		FMobileSurfaceNavPath& OutPath);
};
