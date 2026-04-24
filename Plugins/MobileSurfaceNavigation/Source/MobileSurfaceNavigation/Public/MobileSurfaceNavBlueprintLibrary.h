#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavBlueprintLibrary.generated.h"

class AActor;
class USceneComponent;

UCLASS()
class MOBILESURFACENAVIGATION_API UMobileSurfaceNavBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Special Links", meta = (DisplayName = "Make Special Link Node From World Position"))
	static FMobileSurfaceNavSpecialLinkNode MakeSpecialLinkNodeFromWorldPosition(
		const FVector& WorldPosition,
		int32 StopIndex = -1);

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Special Links", meta = (DisplayName = "Make Special Link Node From Scene Component"))
	static FMobileSurfaceNavSpecialLinkNode MakeSpecialLinkNodeFromSceneComponent(
		const USceneComponent* SceneComponent,
		int32 StopIndex = -1);

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Special Links", meta = (DisplayName = "Make Special Link Node From Actor"))
	static FMobileSurfaceNavSpecialLinkNode MakeSpecialLinkNodeFromActor(
		const AActor* Actor,
		int32 StopIndex = -1);
};
