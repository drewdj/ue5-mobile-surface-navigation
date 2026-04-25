#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavAgent.h"
#include "SphereOnlyNavAgent.generated.h"

UCLASS(BlueprintType, Blueprintable)
class PATHFINDINGTEST_API ASphereOnlyNavAgent : public AMobileSurfaceNavAgent
{
	GENERATED_BODY()

public:
	ASphereOnlyNavAgent();
};
