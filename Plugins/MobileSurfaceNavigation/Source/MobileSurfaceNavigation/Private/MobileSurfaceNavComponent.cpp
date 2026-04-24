#include "MobileSurfaceNavComponent.h"

#include "MobileSurfaceNavAgent.h"
#include "MobileSurfaceNavAgentComponent.h"
#include "MobileSurfaceNavSubsystem.h"
#include "MobileSurfaceNavElevator.h"
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

static void NormalizeSpecialLinkNodesForType(
	TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes,
	const EMobileSurfaceNavSpecialLinkType LinkType)
{
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		if (LinkType == EMobileSurfaceNavSpecialLinkType::Elevator)
		{
			if (Nodes[NodeIndex].StopIndex == INDEX_NONE)
			{
				Nodes[NodeIndex].StopIndex = NodeIndex;
			}
		}
		else
		{
			Nodes[NodeIndex].StopIndex = INDEX_NONE;
		}
	}
}

static TArray<FVector> BuildSpecialLinkWorldNodeLocations(const USceneComponent* SpaceComponent, const FMobileSurfaceNavSpecialLink& Link)
{
	TArray<FVector> WorldNodeLocations;
	if (!SpaceComponent)
	{
		return WorldNodeLocations;
	}

	const FTransform LocalToWorld = SpaceComponent->GetComponentTransform();
	WorldNodeLocations.Reserve(Link.Nodes.Num());
	for (const FMobileSurfaceNavSpecialLinkNode& Node : Link.Nodes)
	{
		WorldNodeLocations.Add(LocalToWorld.TransformPosition(Node.LocalPosition));
	}

	return WorldNodeLocations;
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromLocalPoints(
	const FName LinkId,
	const FVector& FromLocalPosition,
	const FVector& ToLocalPosition,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const EMobileSurfaceNavLinkTraversalMode TraversalMode,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	TArray<FMobileSurfaceNavSpecialLinkNode> Nodes;
	FMobileSurfaceNavSpecialLinkNode& FromNode = Nodes.AddDefaulted_GetRef();
	FromNode.LocalPosition = FromLocalPosition;
	FromNode.StopIndex = LinkType == EMobileSurfaceNavSpecialLinkType::Elevator ? 0 : INDEX_NONE;

	FMobileSurfaceNavSpecialLinkNode& ToNode = Nodes.AddDefaulted_GetRef();
	ToNode.LocalPosition = ToLocalPosition;
	ToNode.StopIndex = LinkType == EMobileSurfaceNavSpecialLinkType::Elevator ? 1 : INDEX_NONE;

	return AddSpecialLinkFromLocalNodes(
		LinkId,
		Nodes,
		LinkType,
		TraversalMode,
		bBidirectional,
		Cost,
		LinkTag);
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromWorldPoints(
	const FName LinkId,
	const FVector& FromWorldPosition,
	const FVector& ToWorldPosition,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const EMobileSurfaceNavLinkTraversalMode TraversalMode,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	TArray<FMobileSurfaceNavSpecialLinkNode> Nodes;
	FMobileSurfaceNavSpecialLinkNode& FromNode = Nodes.AddDefaulted_GetRef();
	FromNode.LocalPosition = FromWorldPosition;
	FromNode.StopIndex = LinkType == EMobileSurfaceNavSpecialLinkType::Elevator ? 0 : INDEX_NONE;

	FMobileSurfaceNavSpecialLinkNode& ToNode = Nodes.AddDefaulted_GetRef();
	ToNode.LocalPosition = ToWorldPosition;
	ToNode.StopIndex = LinkType == EMobileSurfaceNavSpecialLinkType::Elevator ? 1 : INDEX_NONE;

	return AddSpecialLinkFromWorldNodes(
		LinkId,
		Nodes,
		LinkType,
		TraversalMode,
		bBidirectional,
		Cost,
		LinkTag);
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromLocalNodes(
	const FName LinkId,
	const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const EMobileSurfaceNavLinkTraversalMode TraversalMode,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	if (!NavigationData.bIsValid || Nodes.Num() < 2)
	{
		return INDEX_NONE;
	}

	FMobileSurfaceNavSpecialLink Link;
	Link.LinkId = LinkId;
	Link.LinkType = LinkType;
	Link.TraversalMode = TraversalMode;
	Link.bBidirectional = bBidirectional;
	Link.Cost = FMath::Max(0.0f, Cost);
	Link.LinkTag = LinkTag;
	Link.Nodes = Nodes;
	NormalizeSpecialLinkNodesForType(Link.Nodes, LinkType);

	if (!ResolveSpecialLinkTriangles(Link))
	{
		return INDEX_NONE;
	}

	const int32 LinkIndex = NavigationData.SpecialLinks.Add(Link);
	EnsureSpecialLinkRuntimeStateSize();
	MarkRuntimeStateDirty();
	return LinkIndex;
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromWorldNodes(
	const FName LinkId,
	const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const EMobileSurfaceNavLinkTraversalMode TraversalMode,
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
	TArray<FMobileSurfaceNavSpecialLinkNode> LocalNodes = Nodes;
	for (FMobileSurfaceNavSpecialLinkNode& Node : LocalNodes)
	{
		Node.LocalPosition = WorldToLocal.TransformPosition(Node.LocalPosition);
	}

	return AddSpecialLinkFromLocalNodes(LinkId, LocalNodes, LinkType, TraversalMode, bBidirectional, Cost, LinkTag);
}

int32 UMobileSurfaceNavComponent::AddSpecialLinkFromSceneComponents(
	const FName LinkId,
	const TArray<USceneComponent*>& SceneComponents,
	const EMobileSurfaceNavSpecialLinkType LinkType,
	const EMobileSurfaceNavLinkTraversalMode TraversalMode,
	const bool bBidirectional,
	const float Cost,
	const FName LinkTag)
{
	TArray<FMobileSurfaceNavSpecialLinkNode> Nodes;
	Nodes.Reserve(SceneComponents.Num());

	for (int32 ComponentIndex = 0; ComponentIndex < SceneComponents.Num(); ++ComponentIndex)
	{
		const USceneComponent* SceneComponent = SceneComponents[ComponentIndex];
		if (!SceneComponent)
		{
			continue;
		}

		FMobileSurfaceNavSpecialLinkNode& Node = Nodes.AddDefaulted_GetRef();
		Node.LocalPosition = SceneComponent->GetComponentLocation();
		Node.StopIndex = LinkType == EMobileSurfaceNavSpecialLinkType::Elevator ? ComponentIndex : INDEX_NONE;
	}

	return AddSpecialLinkFromWorldNodes(LinkId, Nodes, LinkType, TraversalMode, bBidirectional, Cost, LinkTag);
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

bool UMobileSurfaceNavComponent::SetSpecialLinkTraversalMode(const int32 LinkIndex, const EMobileSurfaceNavLinkTraversalMode TraversalMode)
{
	if (!NavigationData.SpecialLinks.IsValidIndex(LinkIndex))
	{
		return false;
	}

	NavigationData.SpecialLinks[LinkIndex].TraversalMode = TraversalMode;
	FMobileSurfaceNavSpecialLink& Link = NavigationData.SpecialLinks[LinkIndex];
	if (Link.ElevatorActor && Link.LinkType == EMobileSurfaceNavSpecialLinkType::Elevator)
	{
		if (const USceneComponent* SpaceComponent = GetNavigationSpaceComponent())
		{
			Link.ElevatorActor->ConfigureServiceLocations(
				BuildSpecialLinkWorldNodeLocations(SpaceComponent, Link),
				Link.TraversalMode,
				false);
		}
	}
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetSpecialLinkElevatorActor(
	const int32 LinkIndex,
	AMobileSurfaceNavElevator* ElevatorActor)
{
	if (!NavigationData.SpecialLinks.IsValidIndex(LinkIndex))
	{
		return false;
	}

	FMobileSurfaceNavSpecialLink& Link = NavigationData.SpecialLinks[LinkIndex];
	Link.ElevatorActor = ElevatorActor;
	if (ElevatorActor && Link.Nodes.IsValidIndex(0))
	{
		if (const USceneComponent* SpaceComponent = GetNavigationSpaceComponent())
		{
			ElevatorActor->ConfigureServiceLocations(
				BuildSpecialLinkWorldNodeLocations(SpaceComponent, Link),
				Link.TraversalMode,
				true);
		}
	}
	MarkRuntimeStateDirty();
	return true;
}

bool UMobileSurfaceNavComponent::SetSpecialLinkNodes(const int32 LinkIndex, const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes)
{
	if (!NavigationData.SpecialLinks.IsValidIndex(LinkIndex) || Nodes.Num() < 2)
	{
		return false;
	}

	FMobileSurfaceNavSpecialLink& Link = NavigationData.SpecialLinks[LinkIndex];
	Link.Nodes = Nodes;
	NormalizeSpecialLinkNodesForType(Link.Nodes, Link.LinkType);
	if (!ResolveSpecialLinkTriangles(Link))
	{
		return false;
	}

	if (Link.ElevatorActor && Link.LinkType == EMobileSurfaceNavSpecialLinkType::Elevator && Link.Nodes.IsValidIndex(0))
	{
		if (const USceneComponent* SpaceComponent = GetNavigationSpaceComponent())
		{
			Link.ElevatorActor->ConfigureServiceLocations(
				BuildSpecialLinkWorldNodeLocations(SpaceComponent, Link),
				Link.TraversalMode,
				true);
		}
	}

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
	if (LadderRuntimeStates.IsValidIndex(LinkIndex))
	{
		LadderRuntimeStates.RemoveAt(LinkIndex);
	}
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
	LadderRuntimeStates.Reset();
	SelectedSpecialLinkIndex = INDEX_NONE;
	MarkRuntimeStateDirty();
}

int32 UMobileSurfaceNavComponent::GetSpecialLinkCount() const
{
	return NavigationData.SpecialLinks.Num();
}

bool UMobileSurfaceNavComponent::TryAcquireLadderTraversal(const int32 LinkIndex, AActor* Agent, const int32 DirectionSign)
{
	if (!Agent || DirectionSign == 0 || !NavigationData.SpecialLinks.IsValidIndex(LinkIndex))
	{
		return false;
	}

	const FMobileSurfaceNavSpecialLink& Link = NavigationData.SpecialLinks[LinkIndex];
	if (Link.LinkType != EMobileSurfaceNavSpecialLinkType::Ladder)
	{
		return false;
	}

	FMobileSurfaceNavLadderRuntimeState* RuntimeState = GetLadderRuntimeState(LinkIndex);
	if (!RuntimeState)
	{
		return false;
	}

	CleanupLadderRuntimeState(*RuntimeState);
	for (const TWeakObjectPtr<AActor>& ActiveAgent : RuntimeState->ActiveAgents)
	{
		if (ActiveAgent.Get() == Agent)
		{
			return true;
		}
	}

	if (!RuntimeState->ActiveAgents.IsEmpty() && RuntimeState->ActiveDirectionSign != DirectionSign)
	{
		return false;
	}

	RuntimeState->ActiveAgents.Add(Agent);
	RuntimeState->ActiveDirectionSign = DirectionSign;
	return true;
}

void UMobileSurfaceNavComponent::ReleaseLadderTraversal(const int32 LinkIndex, AActor* Agent)
{
	if (!Agent)
	{
		return;
	}

	FMobileSurfaceNavLadderRuntimeState* RuntimeState = GetLadderRuntimeState(LinkIndex);
	if (!RuntimeState)
	{
		return;
	}

	RuntimeState->ActiveAgents.RemoveAllSwap([Agent](const TWeakObjectPtr<AActor>& ActiveAgent)
	{
		return !ActiveAgent.IsValid() || ActiveAgent.Get() == Agent;
	});

	if (RuntimeState->ActiveAgents.IsEmpty())
	{
		RuntimeState->ActiveDirectionSign = 0;
	}
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
	if (Link.Nodes.Num() < 2)
	{
		return false;
	}

	for (FMobileSurfaceNavSpecialLinkNode& Node : Link.Nodes)
	{
		Node.TriangleIndex = FindContainingTriangle(Node.LocalPosition);
		if (Node.TriangleIndex == INDEX_NONE)
		{
			Node.TriangleIndex = FindNearestTriangle(Node.LocalPosition);
		}

		if (Node.TriangleIndex == INDEX_NONE)
		{
			return false;
		}
	}

	return true;
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

void UMobileSurfaceNavComponent::EnsureSpecialLinkRuntimeStateSize()
{
	LadderRuntimeStates.SetNum(NavigationData.SpecialLinks.Num());
}

FMobileSurfaceNavLadderRuntimeState* UMobileSurfaceNavComponent::GetLadderRuntimeState(const int32 LinkIndex)
{
	EnsureSpecialLinkRuntimeStateSize();
	if (!LadderRuntimeStates.IsValidIndex(LinkIndex))
	{
		return nullptr;
	}

	return &LadderRuntimeStates[LinkIndex];
}

const FMobileSurfaceNavLadderRuntimeState* UMobileSurfaceNavComponent::GetLadderRuntimeState(const int32 LinkIndex) const
{
	if (!LadderRuntimeStates.IsValidIndex(LinkIndex))
	{
		return nullptr;
	}

	return &LadderRuntimeStates[LinkIndex];
}

void UMobileSurfaceNavComponent::CleanupLadderRuntimeState(FMobileSurfaceNavLadderRuntimeState& RuntimeState) const
{
	RuntimeState.ActiveAgents.RemoveAllSwap([](const TWeakObjectPtr<AActor>& ActiveAgent)
	{
		return !ActiveAgent.IsValid();
	});

	if (RuntimeState.ActiveAgents.IsEmpty())
	{
		RuntimeState.ActiveDirectionSign = 0;
	}
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
		if (UMobileSurfaceNavAgentComponent* AgentComponent = Agent->GetNavigationAgentComponent())
		{
			AgentComponent->SetRandomPathDelay(DebugAgentRandomPathDelay);
		}
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
