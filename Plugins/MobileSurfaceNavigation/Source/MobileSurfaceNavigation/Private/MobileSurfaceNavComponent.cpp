#include "MobileSurfaceNavComponent.h"

#include "MobileSurfaceNavAgent.h"
#include "MobileSurfaceNavSubsystem.h"
#include "MobileSurfaceNavigationBuilder.h"
#include "MobileSurfaceNavigationDebug.h"
#include "MobileSurfaceNavigationQuery.h"
#include "MobileSurfacePathfinder.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

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
	Settings.bDrawPortals = bDrawPortals;
	Settings.bDrawPortalLabels = bDrawPortalLabels;
	Settings.bDrawSpecialLinks = bDrawSpecialLinks;
	Settings.HighlightPortalIndex = SelectedPortalIndex;
	Settings.HighlightSpecialLinkIndex = SelectedSpecialLinkIndex;
	Settings.Duration = Duration;

	FMobileSurfaceNavigationDebug::DrawNavData(GetWorld(), GetNavigationSpaceComponent(), NavigationData, Settings);
	DrawDebugPath(Duration);
}

bool UMobileSurfaceNavComponent::FindPathLocal(const FVector& StartLocalPosition, const FVector& EndLocalPosition, FMobileSurfaceNavPath& OutPath, const float AgentRadius) const
{
	FMobileSurfacePathQueryParams Params;
	Params.AgentRadius = AgentRadius;
	return FMobileSurfacePathfinder::FindPath(NavigationData, StartLocalPosition, EndLocalPosition, Params, OutPath);
}

bool UMobileSurfaceNavComponent::FindPathLocalWithParams(
	const FVector& StartLocalPosition,
	const FVector& EndLocalPosition,
	const FMobileSurfacePathQueryParams& Params,
	FMobileSurfaceNavPath& OutPath) const
{
	return FMobileSurfacePathfinder::FindPath(NavigationData, StartLocalPosition, EndLocalPosition, Params, OutPath);
}

int32 UMobileSurfaceNavComponent::FindContainingTriangle(const FVector& LocalPosition) const
{
	return FMobileSurfaceNavigationQuery::FindContainingTriangle(NavigationData, LocalPosition);
}

int32 UMobileSurfaceNavComponent::FindNearestTriangle(const FVector& LocalPosition) const
{
	return FMobileSurfaceNavigationQuery::FindNearestTriangle(NavigationData, LocalPosition);
}

void UMobileSurfaceNavComponent::ClearNavigationData()
{
	DestroyPortalLabelComponents();
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

int32 UMobileSurfaceNavComponent::GetPortalCount() const
{
	return NavigationData.Portals.Num();
}

int32 UMobileSurfaceNavComponent::GetRegionCount() const
{
	return NavigationData.Regions.Num();
}

int32 UMobileSurfaceNavComponent::GetRuntimeStateRevision() const
{
	return NavigationData.RuntimeStateRevision;
}

bool UMobileSurfaceNavComponent::SetPortalOpen(const int32 PortalIndex, const bool bOpen)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex].bOpen = bOpen;
	NavigationData.PortalRuntimeStates[PortalIndex].bUserModified = true;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetPortalCostMultiplier(const int32 PortalIndex, const float CostMultiplier)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex].CostMultiplier = FMath::Max(0.001f, CostMultiplier);
	NavigationData.PortalRuntimeStates[PortalIndex].bUserModified = true;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetPortalExtraCost(const int32 PortalIndex, const float ExtraCost)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex].ExtraCost = FMath::Max(0.0f, ExtraCost);
	NavigationData.PortalRuntimeStates[PortalIndex].bUserModified = true;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetPortalEffectiveWidthOverride(const int32 PortalIndex, const float EffectiveWidthOverride)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex].EffectiveWidthOverride = FMath::Max(0.0f, EffectiveWidthOverride);
	NavigationData.PortalRuntimeStates[PortalIndex].bUserModified = true;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetPortalTag(const int32 PortalIndex, const FName PortalTag)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex].PortalTag = PortalTag;
	NavigationData.PortalRuntimeStates[PortalIndex].bUserModified = true;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::ResetPortalRuntimeState(const int32 PortalIndex)
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex))
	{
		return false;
	}

	NavigationData.PortalRuntimeStates[PortalIndex] = FMobileSurfaceNavPortalRuntimeState();
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetRegionEnabled(const int32 RegionId, const bool bEnabled)
{
	if (!NavigationData.RegionRuntimeStates.IsValidIndex(RegionId))
	{
		return false;
	}

	NavigationData.RegionRuntimeStates[RegionId].bEnabled = bEnabled;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetRegionCostMultiplier(const int32 RegionId, const float CostMultiplier)
{
	if (!NavigationData.RegionRuntimeStates.IsValidIndex(RegionId))
	{
		return false;
	}

	NavigationData.RegionRuntimeStates[RegionId].CostMultiplier = FMath::Max(0.001f, CostMultiplier);
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetRegionAreaTag(const int32 RegionId, const FName AreaTag)
{
	if (!NavigationData.RegionRuntimeStates.IsValidIndex(RegionId))
	{
		return false;
	}

	NavigationData.RegionRuntimeStates[RegionId].AreaTag = AreaTag;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::ResetRegionRuntimeState(const int32 RegionId)
{
	if (!NavigationData.RegionRuntimeStates.IsValidIndex(RegionId))
	{
		return false;
	}

	NavigationData.RegionRuntimeStates[RegionId] = FMobileSurfaceNavRegionRuntimeState();
	MarkRuntimeStateDirty();
	return true;
}

void UMobileSurfaceNavComponent::ResetAllRuntimeState()
{
	for (FMobileSurfaceNavPortalRuntimeState& PortalState : NavigationData.PortalRuntimeStates)
	{
		PortalState = FMobileSurfaceNavPortalRuntimeState();
	}

	for (FMobileSurfaceNavRegionRuntimeState& RegionState : NavigationData.RegionRuntimeStates)
	{
		RegionState = FMobileSurfaceNavRegionRuntimeState();
	}

	MarkRuntimeStateDirty();
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromLocalPoints(
	const FName LinkId,
	const FVector& FromLocalPosition,
	const FVector& ToLocalPosition,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	if (!NavigationData.bIsValid)
	{
		return INDEX_NONE;
	}

	FMobileSurfaceNavSpecialLink Link;
	Link.LinkId = LinkId;
	Link.LinkType = LinkType;
	Link.bBidirectional = bBidirectional;
	Link.Cost = FMath::Max(0.0f, Cost);
	Link.LinkTag = LinkTag;
	Link.FromLocalPosition = FromLocalPosition;
	Link.ToLocalPosition = ToLocalPosition;

	if (!ResolveSpecialLinkTriangles(Link))
	{
		return INDEX_NONE;
	}

	const int32 LinkIndex = NavigationData.SpecialLinks.Add(Link);
	MarkRuntimeStateDirty();
	return LinkIndex;
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromWorldPoints(
	const FName LinkId,
	const FVector& FromWorldPosition,
	const FVector& ToWorldPosition,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!SpaceComponent)
	{
		return INDEX_NONE;
	}

	const FTransform WorldToLocal = SpaceComponent->GetComponentTransform().Inverse();
	return AddSpecialLinkFromLocalPoints(
		LinkId,
		WorldToLocal.TransformPosition(FromWorldPosition),
		WorldToLocal.TransformPosition(ToWorldPosition),
		LinkType,
		bBidirectional,
		Cost,
		LinkTag);
}

bool UMobileSurfaceNavComponent::SetSpecialLinkEnabled(const int32 LinkIndex, const bool bEnabled)
{
	if (!NavigationData.SpecialLinks.IsValidIndex(LinkIndex))
	{
		return false;
	}

	NavigationData.SpecialLinks[LinkIndex].bEnabled = bEnabled;
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::RemoveSpecialLink(const int32 LinkIndex)
{
	if (!NavigationData.SpecialLinks.IsValidIndex(LinkIndex))
	{
		return false;
	}

	NavigationData.SpecialLinks.RemoveAt(LinkIndex);
	if (SelectedSpecialLinkIndex == LinkIndex)
	{
		SelectedSpecialLinkIndex = INDEX_NONE;
	}
	else if (SelectedSpecialLinkIndex > LinkIndex)
	{
		--SelectedSpecialLinkIndex;
	}
	MarkRuntimeStateDirty();
	return true;
}

void UMobileSurfaceNavComponent::ClearSpecialLinks()
{
	NavigationData.SpecialLinks.Reset();
	SelectedSpecialLinkIndex = INDEX_NONE;
	MarkRuntimeStateDirty();
}

int32 UMobileSurfaceNavComponent::GetSpecialLinkCount() const
{
	return NavigationData.SpecialLinks.Num();
}

void UMobileSurfaceNavComponent::OpenSelectedPortal()
{
	SetPortalOpen(SelectedPortalIndex, true);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::CloseSelectedPortal()
{
	SetPortalOpen(SelectedPortalIndex, false);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::ToggleSelectedPortal()
{
	if (!NavigationData.PortalRuntimeStates.IsValidIndex(SelectedPortalIndex))
	{
		return;
	}

	SetPortalOpen(SelectedPortalIndex, !NavigationData.PortalRuntimeStates[SelectedPortalIndex].bOpen);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::ResetSelectedPortalRuntimeState()
{
	ResetPortalRuntimeState(SelectedPortalIndex);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::EnableSelectedRegion()
{
	SetRegionEnabled(SelectedRegionId, true);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::DisableSelectedRegion()
{
	SetRegionEnabled(SelectedRegionId, false);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::EnableSelectedSpecialLink()
{
	SetSpecialLinkEnabled(SelectedSpecialLinkIndex, true);
	DrawNavigationDebug(DebugDuration);
}

void UMobileSurfaceNavComponent::DisableSelectedSpecialLink()
{
	SetSpecialLinkEnabled(SelectedSpecialLinkIndex, false);
	DrawNavigationDebug(DebugDuration);
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

void UMobileSurfaceNavComponent::MarkRuntimeStateDirty()
{
	++NavigationData.RuntimeStateRevision;
	CurrentDebugPath = FMobileSurfaceNavPath();
	RefreshPortalLabelComponents();

	if (bAutoDrawDebugOnRuntimeStateChange)
	{
		DrawNavigationDebug(DebugDuration);
	}
}

bool UMobileSurfaceNavComponent::ResolveSpecialLinkTriangles(FMobileSurfaceNavSpecialLink& Link) const
{
	Link.FromTriangleIndex = FindContainingTriangle(Link.FromLocalPosition);
	if (Link.FromTriangleIndex == INDEX_NONE)
	{
		Link.FromTriangleIndex = FindNearestTriangle(Link.FromLocalPosition);
	}

	Link.ToTriangleIndex = FindContainingTriangle(Link.ToLocalPosition);
	if (Link.ToTriangleIndex == INDEX_NONE)
	{
		Link.ToTriangleIndex = FindNearestTriangle(Link.ToLocalPosition);
	}

	return Link.FromTriangleIndex != INDEX_NONE && Link.ToTriangleIndex != INDEX_NONE;
}

void UMobileSurfaceNavComponent::BeginPlay()
{
	Super::BeginPlay();
	DebugPathRandomStream.Initialize(DebugPathRandomSeed);

	if (UWorld* World = GetWorld())
	{
		if (UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>())
		{
			Subsystem->RegisterNavSurface(this);
		}
	}

	if (bRebuildOnBeginPlay && !NavigationData.bIsValid)
	{
		RebuildNavigationData();
	}

	if (bAutoGenerateDebugPaths)
	{
		GenerateDebugPath();
	}

	if (bSpawnDebugAgentsOnBeginPlay)
	{
		SpawnDebugAgents();
	}

	UpdateTickState();
}

void UMobileSurfaceNavComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>())
		{
			Subsystem->UnregisterNavSurface(this);
		}
	}

	DestroyDebugAgents();
	DestroyPortalLabelComponents();
	Super::EndPlay(EndPlayReason);
}

void UMobileSurfaceNavComponent::TickComponent(
	float DeltaTime,
	enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePortalLabelFacing();

	if (bDrawDebugEveryTick && NavigationData.bIsValid)
	{
		DrawNavigationDebug(0.0f);
	}

	if (bAutoGenerateDebugPaths && NavigationData.bIsValid)
	{
		DebugPathTimeAccumulator += DeltaTime;
		if (DebugPathTimeAccumulator >= DebugPathGenerationInterval)
		{
			DebugPathTimeAccumulator = 0.0f;
			GenerateDebugPath();
		}

		DrawDebugPath(0.0f);
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
	const bool bNeedsPortalLabelTick = bDrawPortalLabels && bDrawPortals && NavigationData.bIsValid;
	SetComponentTickEnabled((bDrawDebugEveryTick || bAutoGenerateDebugPaths || bNeedsPortalLabelTick) && NavigationData.bIsValid);
}

void UMobileSurfaceNavComponent::GenerateDebugPath()
{
	CurrentDebugPath = FMobileSurfaceNavPath();

	if (!NavigationData.bIsValid || NavigationData.Triangles.Num() < 2)
	{
		return;
	}

	const int32 StartTriangleIndex = DebugPathRandomStream.RandRange(0, NavigationData.Triangles.Num() - 1);
	int32 EndTriangleIndex = DebugPathRandomStream.RandRange(0, NavigationData.Triangles.Num() - 1);
	for (int32 AttemptIndex = 0; AttemptIndex < 8 && EndTriangleIndex == StartTriangleIndex; ++AttemptIndex)
	{
		EndTriangleIndex = DebugPathRandomStream.RandRange(0, NavigationData.Triangles.Num() - 1);
	}

	if (EndTriangleIndex == StartTriangleIndex)
	{
		return;
	}

	const FVector Start = NavigationData.Triangles[StartTriangleIndex].Center;
	const FVector End = NavigationData.Triangles[EndTriangleIndex].Center;
	FindPathLocal(Start, End, CurrentDebugPath);

	if (CurrentDebugPath.bIsValid)
	{
		DrawDebugPath(DebugPathDuration);
	}
}

void UMobileSurfaceNavComponent::DrawDebugPath(const float Duration) const
{
	if (!CurrentDebugPath.bIsValid || CurrentDebugPath.Waypoints.Num() < 2)
	{
		return;
	}

	UWorld* World = GetWorld();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!World || !SpaceComponent)
	{
		return;
	}

	const FTransform LocalToWorld = SpaceComponent->GetComponentTransform();
	constexpr uint8 DepthPriority = 0;

	if (bDrawRawDebugPath && CurrentDebugPath.RawWaypoints.Num() >= 2)
	{
		for (int32 WaypointIndex = 0; WaypointIndex + 1 < CurrentDebugPath.RawWaypoints.Num(); ++WaypointIndex)
		{
			const FVector Start = LocalToWorld.TransformPosition(CurrentDebugPath.RawWaypoints[WaypointIndex]);
			const FVector End = LocalToWorld.TransformPosition(CurrentDebugPath.RawWaypoints[WaypointIndex + 1]);
			DrawDebugLine(World, Start, End, FColor::Orange, false, Duration, DepthPriority, 2.0f);
		}
	}

	for (int32 WaypointIndex = 0; WaypointIndex + 1 < CurrentDebugPath.Waypoints.Num(); ++WaypointIndex)
	{
		const FVector Start = LocalToWorld.TransformPosition(CurrentDebugPath.Waypoints[WaypointIndex]);
		const FVector End = LocalToWorld.TransformPosition(CurrentDebugPath.Waypoints[WaypointIndex + 1]);
		DrawDebugLine(World, Start, End, FColor::Red, false, Duration, DepthPriority, 6.0f);
		DrawDebugPoint(World, Start, 16.0f, WaypointIndex == 0 ? FColor::Green : FColor::Red, false, Duration, DepthPriority);
	}

	DrawDebugPoint(World, LocalToWorld.TransformPosition(CurrentDebugPath.Waypoints.Last()), 16.0f, FColor::Blue, false, Duration, DepthPriority);
}

void UMobileSurfaceNavComponent::SpawnDebugAgents()
{
	DestroyDebugAgents();

	if (!NavigationData.bIsValid || NavigationData.Triangles.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!World || !SpaceComponent)
	{
		return;
	}

	const TSubclassOf<AMobileSurfaceNavAgent> AgentClass = DebugAgentClass.Get() ? DebugAgentClass : TSubclassOf<AMobileSurfaceNavAgent>(AMobileSurfaceNavAgent::StaticClass());
	FRandomStream AgentRandom(DebugAgentRandomSeed);

	for (int32 AgentIndex = 0; AgentIndex < DebugAgentCount; ++AgentIndex)
	{
		FVector LocalSpawn = FVector::ZeroVector;
		bool bFoundSpawn = false;
		for (int32 AttemptIndex = 0; AttemptIndex < 16; ++AttemptIndex)
		{
			const int32 TriangleIndex = AgentRandom.RandRange(0, NavigationData.Triangles.Num() - 1);
			const FVector CandidateSpawn = NavigationData.Triangles[TriangleIndex].Center;
			FMobileSurfaceNavPath TestPath;
			if (FindPathLocal(CandidateSpawn, CandidateSpawn, TestPath, DebugAgentRadius))
			{
				LocalSpawn = CandidateSpawn;
				bFoundSpawn = true;
				break;
			}
		}

		if (!bFoundSpawn)
		{
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwner();
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AMobileSurfaceNavAgent* Agent = World->SpawnActor<AMobileSurfaceNavAgent>(
			AgentClass,
			SpaceComponent->GetComponentTransform().TransformPosition(LocalSpawn),
			FRotator::ZeroRotator,
			SpawnParameters);

		if (!Agent)
		{
			continue;
		}

		Agent->InitializeAgent(this, DebugAgentRadius, DebugAgentMoveSpeed, DebugAgentRandomSeed + AgentIndex * 31);
		SpawnedDebugAgents.Add(Agent);
	}
}

void UMobileSurfaceNavComponent::DestroyDebugAgents()
{
	for (AMobileSurfaceNavAgent* Agent : SpawnedDebugAgents)
	{
		if (IsValid(Agent))
		{
			Agent->Destroy();
		}
	}

	SpawnedDebugAgents.Reset();
}

void UMobileSurfaceNavComponent::RefreshPortalLabelComponents()
{
	if (!bDrawPortalLabels || !bDrawPortals || !NavigationData.bIsValid)
	{
		DestroyPortalLabelComponents();
		return;
	}

	AActor* Owner = GetOwner();
	USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent)
	{
		DestroyPortalLabelComponents();
		return;
	}

	while (PortalLabelComponents.Num() > NavigationData.Portals.Num())
	{
		if (UTextRenderComponent* LabelComponent = PortalLabelComponents.Pop(EAllowShrinking::No))
		{
			LabelComponent->DestroyComponent();
		}
	}

	while (PortalLabelComponents.Num() < NavigationData.Portals.Num())
	{
		UTextRenderComponent* LabelComponent = NewObject<UTextRenderComponent>(Owner, NAME_None, RF_Transient);
		if (!LabelComponent)
		{
			break;
		}

		LabelComponent->SetupAttachment(SpaceComponent);
		LabelComponent->SetMobility(EComponentMobility::Movable);
		LabelComponent->SetHorizontalAlignment(EHTA_Center);
		LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
		LabelComponent->SetWorldSize(18.0f);
		LabelComponent->SetHiddenInGame(false);
		LabelComponent->RegisterComponent();
		PortalLabelComponents.Add(LabelComponent);
	}

	for (int32 PortalIndex = 0; PortalIndex < PortalLabelComponents.Num(); ++PortalIndex)
	{
		UTextRenderComponent* LabelComponent = PortalLabelComponents[PortalIndex];
		if (!LabelComponent || !NavigationData.Portals.IsValidIndex(PortalIndex))
		{
			continue;
		}

		const FMobileSurfaceNavPortal& Portal = NavigationData.Portals[PortalIndex];
		const bool bIsClosed = NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex) && !NavigationData.PortalRuntimeStates[PortalIndex].bOpen;
		const bool bIsModified = NavigationData.PortalRuntimeStates.IsValidIndex(PortalIndex) &&
			(NavigationData.PortalRuntimeStates[PortalIndex].CostMultiplier > 1.01f ||
			 NavigationData.PortalRuntimeStates[PortalIndex].ExtraCost > 0.0f ||
			 NavigationData.PortalRuntimeStates[PortalIndex].EffectiveWidthOverride > 0.0f);
		const FColor LabelColor = bIsClosed ? FColor::Red : bIsModified ? FColor::Orange : FColor::Green;
		const FString LabelText = PortalIndex == SelectedPortalIndex
			? FString::Printf(TEXT("[P%d]"), PortalIndex)
			: FString::Printf(TEXT("P%d"), PortalIndex);

		LabelComponent->SetText(FText::FromString(LabelText));
		LabelComponent->SetTextRenderColor(LabelColor);
		LabelComponent->SetRelativeLocation(Portal.Center + FVector(0.0, 0.0, 35.0));
		LabelComponent->SetVisibility(true);
	}

	UpdatePortalLabelFacing();
	UpdateTickState();
}

void UMobileSurfaceNavComponent::UpdatePortalLabelFacing()
{
	if (!bDrawPortalLabels || !bDrawPortals || !NavigationData.bIsValid || PortalLabelComponents.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!CameraManager)
	{
		return;
	}

	for (int32 PortalIndex = 0; PortalIndex < PortalLabelComponents.Num(); ++PortalIndex)
	{
		UTextRenderComponent* LabelComponent = PortalLabelComponents[PortalIndex];
		if (!LabelComponent || !NavigationData.Portals.IsValidIndex(PortalIndex))
		{
			continue;
		}

		const FVector ToCamera = CameraManager->GetCameraLocation() - LabelComponent->GetComponentLocation();
		if (ToCamera.IsNearlyZero())
		{
			continue;
		}

		LabelComponent->SetWorldRotation(ToCamera.Rotation());
	}
}

void UMobileSurfaceNavComponent::DestroyPortalLabelComponents()
{
	for (UTextRenderComponent* LabelComponent : PortalLabelComponents)
	{
		if (LabelComponent)
		{
			LabelComponent->DestroyComponent();
		}
	}

	PortalLabelComponents.Reset();
}
