#include "MobileSurfaceNavComponent.h"

#include "MobileSurfaceNavigationBuilder.h"
#include "MobileSurfaceNavigationDebug.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UMobileSurfaceNavComponent::UMobileSurfaceNavComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMobileSurfaceNavComponent::RebuildNavigationData()
{
	LastBuildError.Reset();
	NavigationData.Reset();
	bLastBuildSucceeded = false;

	FMobileSurfaceNavBuildSettings Settings;
	Settings.VertexWeldTolerance = VertexWeldTolerance;
	Settings.PlanarRegionAngleToleranceDegrees = PlanarRegionAngleToleranceDegrees;
	Settings.PlanarRegionDistanceTolerance = PlanarRegionDistanceTolerance;
	Settings.BoundarySimplificationTolerance = BoundarySimplificationTolerance;
	UStaticMeshComponent* SourceMeshComponent = GetNavigationSourceMeshComponent();
	USceneComponent* NavigationSpaceComponent = GetNavigationSpaceComponent();

	if (!FMobileSurfaceNavigationBuilder::BuildFromStaticMeshComponent(
		SourceMeshComponent,
		NavigationSpaceComponent,
		Settings,
		NavigationData,
		LastBuildError))
	{
		UpdateTickState();
		return;
	}

	bLastBuildSucceeded = true;

	UpdateTickState();

	if (bAutoDrawDebugAfterRebuild)
	{
		DrawNavigationDebug(DebugDuration);
	}
}

void UMobileSurfaceNavComponent::DrawNavigationDebug(const float Duration)
{
	FMobileSurfaceNavDebugSettings Settings;
	Settings.bDrawVertices = bDrawVertices;
	Settings.bDrawBoundaryEdges = bDrawBoundaryEdges;
	Settings.bDrawTriangles = bDrawTriangles;
	Settings.bDrawTriangleNormals = bDrawTriangleNormals;
	Settings.Duration = Duration;

	FMobileSurfaceNavigationDebug::DrawNavData(GetWorld(), GetNavigationSpaceComponent(), NavigationData, Settings);
}

void UMobileSurfaceNavComponent::ClearNavigationData()
{
	NavigationData.Reset();
	LastBuildError.Reset();
	bLastBuildSucceeded = false;
	UpdateTickState();
}

bool UMobileSurfaceNavComponent::HasValidNavigationData() const
{
	return NavigationData.bIsValid;
}

bool UMobileSurfaceNavComponent::WasLastBuildSuccessful() const
{
	return bLastBuildSucceeded;
}

const FMobileSurfaceNavData& UMobileSurfaceNavComponent::GetNavigationData() const
{
	return NavigationData;
}

const FString& UMobileSurfaceNavComponent::GetLastBuildError() const
{
	return LastBuildError;
}

UStaticMeshComponent* UMobileSurfaceNavComponent::GetNavigationSourceMeshComponent() const
{
	if (AActor* Owner = GetOwner())
	{
		return Cast<UStaticMeshComponent>(NavigationSourceMesh.GetComponent(Owner));
	}

	return nullptr;
}

USceneComponent* UMobileSurfaceNavComponent::GetNavigationSpaceComponent() const
{
	if (AActor* Owner = GetOwner())
	{
		if (USceneComponent* RootComponent = Owner->GetRootComponent())
		{
			return RootComponent;
		}
	}

	return GetNavigationSourceMeshComponent();
}

void UMobileSurfaceNavComponent::BeginPlay()
{
	Super::BeginPlay();
	UpdateTickState();
}

void UMobileSurfaceNavComponent::TickComponent(
	float DeltaTime,
	enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDrawDebugEveryTick && NavigationData.bIsValid)
	{
		DrawNavigationDebug(0.0f);
	}
}

#if WITH_EDITOR
void UMobileSurfaceNavComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMobileSurfaceNavComponent, NavigationSourceMesh))
	{
		NavigationData.Reset();
		LastBuildError.Reset();
		bLastBuildSucceeded = false;
	}

	UpdateTickState();
}
#endif

void UMobileSurfaceNavComponent::UpdateTickState()
{
	SetComponentTickEnabled(bDrawDebugEveryTick && NavigationData.bIsValid);
}
