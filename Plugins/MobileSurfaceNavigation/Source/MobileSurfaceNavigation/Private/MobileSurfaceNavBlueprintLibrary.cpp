#include "MobileSurfaceNavBlueprintLibrary.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

FMobileSurfaceNavSpecialLinkNode UMobileSurfaceNavBlueprintLibrary::MakeSpecialLinkNodeFromWorldPosition(
	const FVector& WorldPosition,
	const int32 StopIndex)
{
	FMobileSurfaceNavSpecialLinkNode Node;
	Node.LocalPosition = WorldPosition;
	Node.StopIndex = StopIndex;
	return Node;
}

FMobileSurfaceNavSpecialLinkNode UMobileSurfaceNavBlueprintLibrary::MakeSpecialLinkNodeFromSceneComponent(
	const USceneComponent* SceneComponent,
	const int32 StopIndex)
{
	if (!SceneComponent)
	{
		return MakeSpecialLinkNodeFromWorldPosition(FVector::ZeroVector, StopIndex);
	}

	return MakeSpecialLinkNodeFromWorldPosition(SceneComponent->GetComponentLocation(), StopIndex);
}

FMobileSurfaceNavSpecialLinkNode UMobileSurfaceNavBlueprintLibrary::MakeSpecialLinkNodeFromActor(
	const AActor* Actor,
	const int32 StopIndex)
{
	if (!Actor)
	{
		return MakeSpecialLinkNodeFromWorldPosition(FVector::ZeroVector, StopIndex);
	}

	if (const USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		return MakeSpecialLinkNodeFromSceneComponent(RootComponent, StopIndex);
	}

	return MakeSpecialLinkNodeFromWorldPosition(Actor->GetActorLocation(), StopIndex);
}
