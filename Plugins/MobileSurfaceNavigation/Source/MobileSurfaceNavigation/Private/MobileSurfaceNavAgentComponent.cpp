#include "MobileSurfaceNavAgentComponent.h"

#include "MobileSurfaceNavElevator.h"
#include "MobileSurfaceNavComponent.h"
#include "MobileSurfaceNavSubsystem.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfaceNavAgent, Log, All);

UMobileSurfaceNavAgentComponent::UMobileSurfaceNavAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMobileSurfaceNavAgentComponent::InitializeAgent(
	UMobileSurfaceNavComponent* InNavigationComponent,
	const float InAgentRadius,
	const float InMoveSpeed,
	const int32 InRandomSeed)
{
	NavigationComponent = InNavigationComponent;
	AgentRadius = InAgentRadius;
	MoveSpeed = InMoveSpeed;
	RandomStream.Initialize(InRandomSeed);
	CacheCurrentNavigationLocalPosition();
}

static const TCHAR* LexAgentStateText(const EMobileSurfaceNavAgentState State)
{
	switch (State)
	{
	case EMobileSurfaceNavAgentState::Idle:
		return TEXT("Idle");
	case EMobileSurfaceNavAgentState::Moving:
		return TEXT("Moving");
	case EMobileSurfaceNavAgentState::WaitingForPath:
		return TEXT("WaitingForPath");
	case EMobileSurfaceNavAgentState::UsingSpecialLink:
		return TEXT("UsingSpecialLink");
	case EMobileSurfaceNavAgentState::WaitingForElevator:
		return TEXT("WaitingForElevator");
	default:
		return TEXT("Unknown");
	}
}

static const TCHAR* LexBoolText(const bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}

bool UMobileSurfaceNavAgentComponent::RequestMoveToLocal(const FVector& TargetLocalPosition)
{
	return RequestMoveToLocalInternal(TargetLocalPosition, false);
}

bool UMobileSurfaceNavAgentComponent::RequestMoveToLocalInternal(
	const FVector& TargetLocalPosition,
	const bool bPreserveCurrentPathUntilSuccess)
{
	if (IsInBlockingSpecialLinkTraversal())
	{
		QueueDeferredMoveRequest(TargetLocalPosition);
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s deferred move request while traversing special link: state=%s target=%s"),
				*GetNameSafe(GetOwner()),
				LexAgentStateText(AgentState),
				*TargetLocalPosition.ToCompactString());
		}
		return true;
	}

	bHasActiveTarget = true;
	bActiveTargetIsRandom = false;
	ActiveTargetLocalPosition = TargetLocalPosition;

	if (!NavigationComponent || !NavigationComponent->HasValidNavigationData())
	{
		ClearCurrentPath();
		ClearPendingPathRequest();
		bHasActiveTarget = false;
		bActiveTargetIsRandom = false;
		AgentState = EMobileSurfaceNavAgentState::Idle;
		return false;
	}

	const AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent)
	{
		bHasActiveTarget = false;
		bActiveTargetIsRandom = false;
		AgentState = EMobileSurfaceNavAgentState::Idle;
		return false;
	}

	const FVector CurrentLocal = SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	FMobileSurfacePathQueryParams Params;
	Params.AgentRadius = AgentRadius;
	LastRequestedRuntimeStateRevision = NavigationComponent->GetRuntimeStateRevision();

	if (bLogPathRequests)
	{
		UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s request path: start=%s target=%s revision=%d queued=%s"),
			*GetNameSafe(GetOwner()),
			*CurrentLocal.ToCompactString(),
			*TargetLocalPosition.ToCompactString(),
			LastRequestedRuntimeStateRevision,
			LexBoolText(bUseQueuedPathRequests));
	}

	if (bUseQueuedPathRequests)
	{
		if (UWorld* World = GetWorld())
		{
			if (UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>())
			{
				ClearPendingPathRequest();
				PendingPathRequest = Subsystem->QueuePathRequest(NavigationComponent, CurrentLocal, TargetLocalPosition, Params);
				PendingTargetLocalPosition = TargetLocalPosition;
				bPendingPathPreservesCurrentPath = bPreserveCurrentPathUntilSuccess;
				if (!bPreserveCurrentPathUntilSuccess)
				{
					ClearCurrentPath();
					CurrentWaypointIndex = 0;
				}
				return PendingPathRequest.IsValid();
			}
		}
	}

	return RequestPathImmediate(TargetLocalPosition, bPreserveCurrentPathUntilSuccess);
}

bool UMobileSurfaceNavAgentComponent::RequestMoveToWorld(const FVector& TargetWorldPosition)
{
	if (!NavigationComponent || !NavigationComponent->GetOwner())
	{
		return false;
	}

	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner()->GetRootComponent();
	if (!SpaceComponent)
	{
		return false;
	}

	return RequestMoveToLocal(SpaceComponent->GetComponentTransform().InverseTransformPosition(TargetWorldPosition));
}

bool UMobileSurfaceNavAgentComponent::RequestPathImmediate(
	const FVector& TargetLocalPosition,
	const bool bPreserveCurrentPathUntilSuccess)
{
	if (!NavigationComponent || !NavigationComponent->HasValidNavigationData())
	{
		if (!bPreserveCurrentPathUntilSuccess)
		{
			ClearCurrentPath();
		}
		ClearPendingPathRequest();
		return false;
	}

	const AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent)
	{
		return false;
	}

	const FVector CurrentLocal = SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	FMobileSurfaceNavPath NewPath;
	const bool bPathFound = NavigationComponent->FindPathLocal(CurrentLocal, TargetLocalPosition, NewPath, AgentRadius);
	if (!bPathFound)
	{
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s immediate path failed: target=%s revision=%d"),
				*GetNameSafe(GetOwner()),
				*TargetLocalPosition.ToCompactString(),
				LastRequestedRuntimeStateRevision);
		}
		bWaitingForNavigationChange = bHasActiveTarget;
		AgentState = EMobileSurfaceNavAgentState::WaitingForPath;
		ObservedRuntimeStateRevision = NavigationComponent->GetRuntimeStateRevision();
		if (!bPreserveCurrentPathUntilSuccess)
		{
			ClearCurrentPath();
		}
		return false;
	}

	ResetActiveSpecialLinkState();
	CurrentPath = NewPath;
	ClearPendingPathRequest();
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
	AgentState = CurrentPath.Segments.IsEmpty() ? EMobileSurfaceNavAgentState::Moving : EMobileSurfaceNavAgentState::Moving;
	LastProgressWaypointIndex = CurrentWaypointIndex;
	LastProgressWorldPosition = Owner->GetActorLocation();
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	CachedNavigationLocalPosition = CurrentPath.Waypoints.IsValidIndex(0)
		? CurrentPath.Waypoints[0]
		: CurrentLocal;
	bHasCachedNavigationLocalPosition = true;
	SyncOwnerToCachedNavigationLocalPosition();
	if (bLogPathRequests)
	{
		UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s immediate path success: triangles=%d waypoints=%d length=%.1f revision=%d layer=%.1f start=%s"),
			*GetNameSafe(GetOwner()),
			CurrentPath.TriangleIndices.Num(),
			CurrentPath.Waypoints.Num(),
			CurrentPath.EstimatedLength,
			ObservedRuntimeStateRevision,
			CurrentPath.AgentRadiusLayer,
			*CachedNavigationLocalPosition.ToCompactString());
	}
	return true;
}

bool UMobileSurfaceNavAgentComponent::PollPendingPathRequest()
{
	if (!PendingPathRequest.IsValid())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>();
	if (!Subsystem)
	{
		const FVector Target = PendingTargetLocalPosition;
		PendingPathRequest = FMobileSurfacePathRequestHandle();
		return RequestPathImmediate(Target, false);
	}

	bool bSucceeded = false;
	FMobileSurfaceNavPath Path;
	if (!Subsystem->TryGetPathResult(PendingPathRequest, Path, bSucceeded))
	{
		return false;
	}

	PendingPathRequest = FMobileSurfacePathRequestHandle();
	if (!bSucceeded || !Path.bIsValid)
	{
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s queued path failed: target=%s requestedRevision=%d currentRevision=%d"),
				*GetNameSafe(GetOwner()),
				*PendingTargetLocalPosition.ToCompactString(),
				LastRequestedRuntimeStateRevision,
				NavigationComponent ? NavigationComponent->GetRuntimeStateRevision() : INDEX_NONE);
		}
		if (!bPendingPathPreservesCurrentPath)
		{
			ClearCurrentPath();
		}
		bWaitingForNavigationChange = bHasActiveTarget;
		AgentState = EMobileSurfaceNavAgentState::WaitingForPath;
		ObservedRuntimeStateRevision = NavigationComponent ? NavigationComponent->GetRuntimeStateRevision() : INDEX_NONE;
		bPendingPathPreservesCurrentPath = false;
		return false;
	}

	ResetActiveSpecialLinkState();
	CurrentPath = Path;
	bPendingPathPreservesCurrentPath = false;
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
	AgentState = EMobileSurfaceNavAgentState::Moving;
	LastProgressWaypointIndex = CurrentWaypointIndex;
	LastProgressWorldPosition = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	if (GetOwner() && NavigationComponent && NavigationComponent->GetOwner() && NavigationComponent->GetOwner()->GetRootComponent())
	{
		CachedNavigationLocalPosition = CurrentPath.Waypoints.IsValidIndex(0)
			? CurrentPath.Waypoints[0]
			: NavigationComponent->GetOwner()->GetRootComponent()->GetComponentTransform().InverseTransformPosition(GetOwner()->GetActorLocation());
		bHasCachedNavigationLocalPosition = true;
		SyncOwnerToCachedNavigationLocalPosition();
	}
	if (bLogPathRequests)
	{
		UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s queued path success: triangles=%d waypoints=%d length=%.1f revision=%d layer=%.1f start=%s"),
			*GetNameSafe(GetOwner()),
			CurrentPath.TriangleIndices.Num(),
			CurrentPath.Waypoints.Num(),
			CurrentPath.EstimatedLength,
			ObservedRuntimeStateRevision,
			CurrentPath.AgentRadiusLayer,
			*CachedNavigationLocalPosition.ToCompactString());
	}
	return true;
}

void UMobileSurfaceNavAgentComponent::ClearPendingPathRequest()
{
	if (!PendingPathRequest.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>())
		{
			Subsystem->CancelPathRequest(PendingPathRequest);
		}
	}

	PendingPathRequest = FMobileSurfacePathRequestHandle();
	bPendingPathPreservesCurrentPath = false;
}

void UMobileSurfaceNavAgentComponent::ClearCurrentPath()
{
	ResetActiveSpecialLinkState();
	CurrentPath = FMobileSurfaceNavPath();
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = 0;
	LastProgressWaypointIndex = INDEX_NONE;
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	AgentState = EMobileSurfaceNavAgentState::Idle;
	bHasCachedNavigationLocalPosition = false;
}

bool UMobileSurfaceNavAgentComponent::RepathToActiveTarget(const bool bPreserveCurrentPathUntilSuccess)
{
	if (!bHasActiveTarget)
	{
		return false;
	}

	const bool bWasRandomTarget = bActiveTargetIsRandom;
	const FVector TargetLocalPosition = ActiveTargetLocalPosition;
	const bool bRequested = RequestMoveToLocalInternal(TargetLocalPosition, bPreserveCurrentPathUntilSuccess);
	bHasActiveTarget = true;
	bActiveTargetIsRandom = bWasRandomTarget;
	ActiveTargetLocalPosition = TargetLocalPosition;
	return bRequested;
}

void UMobileSurfaceNavAgentComponent::HandleMoveCompleted()
{
	ClearCurrentPath();
	bWaitingForNavigationChange = false;

	if (bActiveTargetIsRandom)
	{
		if (RandomPathDelay > UE_SMALL_NUMBER)
		{
			bPendingInitialRandomPathRequest = true;
			RepathTimer = RandomPathDelay;
		}
		else
		{
			RequestRandomPath();
		}
		return;
	}

	bHasActiveTarget = false;
	AgentState = EMobileSurfaceNavAgentState::Idle;
}

bool UMobileSurfaceNavAgentComponent::RequestRandomPath()
{
	if (PendingPathRequest.IsValid())
	{
		return true;
	}

	FVector TargetLocal = FVector::ZeroVector;
	for (int32 AttemptIndex = 0; AttemptIndex < 12; ++AttemptIndex)
	{
		if (PickRandomNavigablePoint(TargetLocal))
		{
			const bool bRequested = RequestMoveToLocal(TargetLocal);
			if (bRequested)
			{
				bPendingInitialRandomPathRequest = false;
				bActiveTargetIsRandom = true;
				ActiveTargetLocalPosition = TargetLocal;
			}
			return bRequested;
		}
	}

	ClearCurrentPath();
	return false;
}

void UMobileSurfaceNavAgentComponent::StopMovement()
{
	bHasActiveTarget = false;
	bActiveTargetIsRandom = false;
	bWaitingForNavigationChange = false;
	bHasDeferredMoveRequest = false;
	ClearPendingPathRequest();
	ClearCurrentPath();
}

float UMobileSurfaceNavAgentComponent::GetAgentRadius() const
{
	return AgentRadius;
}

EMobileSurfaceNavAgentState UMobileSurfaceNavAgentComponent::GetAgentState() const
{
	return AgentState;
}

UMobileSurfaceNavComponent* UMobileSurfaceNavAgentComponent::GetNavigationComponent() const
{
	return NavigationComponent;
}

void UMobileSurfaceNavAgentComponent::SetRandomPathDelay(const float InRandomPathDelay)
{
	RandomPathDelay = FMath::Max(0.0f, InRandomPathDelay);
}


bool UMobileSurfaceNavAgentComponent::IsInBlockingSpecialLinkTraversal() const
{
	if (IsBoardedOnActiveElevator())
	{
		return true;
	}

	if (AgentState != EMobileSurfaceNavAgentState::UsingSpecialLink)
	{
		return false;
	}

	return CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex) &&
		CurrentPath.Segments[CurrentSegmentIndex].SegmentType != EMobileSurfaceNavPathSegmentType::Walk;
}

bool UMobileSurfaceNavAgentComponent::ConsumeDeferredMoveRequestFromCurrentLocation()
{
	if (!bHasDeferredMoveRequest || !NavigationComponent)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
	if (!Owner || !SpaceComponent)
	{
		return false;
	}

	const FVector DeferredTarget = DeferredMoveTargetLocalPosition;
	bHasDeferredMoveRequest = false;
	DeferredMoveTargetLocalPosition = FVector::ZeroVector;
	ClearPendingPathRequest();
	ResetActiveSpecialLinkState();
	CurrentPath = FMobileSurfaceNavPath();
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = 0;
	LastProgressWaypointIndex = INDEX_NONE;
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	AgentState = EMobileSurfaceNavAgentState::Idle;
	return RequestMoveToLocalInternal(DeferredTarget, false);
}

bool UMobileSurfaceNavAgentComponent::CacheCurrentNavigationLocalPosition()
{
	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent)
	{
		return false;
	}

	CachedNavigationLocalPosition = SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	bHasCachedNavigationLocalPosition = true;
	return true;
}

bool UMobileSurfaceNavAgentComponent::SyncOwnerToCachedNavigationLocalPosition() const
{
	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent || !bHasCachedNavigationLocalPosition)
	{
		return false;
	}

	Owner->SetActorLocation(SpaceComponent->GetComponentTransform().TransformPosition(CachedNavigationLocalPosition));
	return true;
}

void UMobileSurfaceNavAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bRequestRandomPathOnBeginPlay)
	{
		bPendingInitialRandomPathRequest = !RequestRandomPath();
	}
}

void UMobileSurfaceNavAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPendingPathRequest();
	ClearCurrentPath();
	Super::EndPlay(EndPlayReason);
}

const USceneComponent* UMobileSurfaceNavAgentComponent::GetNavigationSpaceComponent() const
{
	return NavigationComponent && NavigationComponent->GetOwner()
		? NavigationComponent->GetOwner()->GetRootComponent()
		: nullptr;
}

void UMobileSurfaceNavAgentComponent::QueueDeferredMoveRequest(const FVector& TargetLocalPosition)
{
	bHasDeferredMoveRequest = true;
	DeferredMoveTargetLocalPosition = TargetLocalPosition;
	bHasActiveTarget = true;
	bActiveTargetIsRandom = false;
	ActiveTargetLocalPosition = TargetLocalPosition;
}

void UMobileSurfaceNavAgentComponent::ConsumeDeferredMoveRequest()
{
	if (!bHasDeferredMoveRequest)
	{
		return;
	}

	const FVector DeferredTarget = DeferredMoveTargetLocalPosition;
	bHasDeferredMoveRequest = false;
	DeferredMoveTargetLocalPosition = FVector::ZeroVector;
	RequestMoveToLocalInternal(DeferredTarget, false);
}

void UMobileSurfaceNavAgentComponent::DrawCurrentPathDebug() const
{
	if (!bDrawCurrentPathDebug || !CurrentPath.bIsValid || !NavigationComponent || CurrentPath.TriangleIndices.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!World || !SpaceComponent)
	{
		return;
	}

	const FMobileSurfaceNavData* NavDataPtr = NavigationComponent->GetNavigationDataForAgentRadius(CurrentPath.AgentRadiusLayer);
	const FMobileSurfaceNavData& NavData = NavDataPtr ? *NavDataPtr : NavigationComponent->GetNavigationData();
	const FTransform LocalToWorld = SpaceComponent->GetComponentTransform();
	const float Duration = 0.0f;
	const uint8 DepthPriority = 0;

	for (int32 TrianglePathIndex = 0; TrianglePathIndex < CurrentPath.TriangleIndices.Num(); ++TrianglePathIndex)
	{
		const int32 TriangleIndex = CurrentPath.TriangleIndices[TrianglePathIndex];
		if (!NavData.Triangles.IsValidIndex(TriangleIndex))
		{
			continue;
		}

		const FMobileSurfaceNavTriangle& Triangle = NavData.Triangles[TriangleIndex];
		const FColor TriangleColor = FColor(90, 180, 255);
		const float LineThickness = 2.0f;

		const FVector V0 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.X].LocalPosition);
		const FVector V1 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Y].LocalPosition);
		const FVector V2 = LocalToWorld.TransformPosition(NavData.Vertices[Triangle.VertexIndices.Z].LocalPosition);
		DrawDebugLine(World, V0, V1, TriangleColor, false, Duration, DepthPriority, LineThickness);
		DrawDebugLine(World, V1, V2, TriangleColor, false, Duration, DepthPriority, LineThickness);
		DrawDebugLine(World, V2, V0, TriangleColor, false, Duration, DepthPriority, LineThickness);

		if (bDrawCurrentPathTriangleLabels)
		{
			DrawDebugString(
				World,
				LocalToWorld.TransformPosition(Triangle.Center + FVector(0.0, 0.0, 10.0f)),
				FString::Printf(TEXT("T%d [%d]"), TriangleIndex, TrianglePathIndex),
				nullptr,
				TriangleColor,
				Duration,
				false,
				1.0f);
		}
	}

	for (int32 TrianglePathIndex = 0; TrianglePathIndex + 1 < CurrentPath.TriangleIndices.Num(); ++TrianglePathIndex)
	{
		const int32 TriangleA = CurrentPath.TriangleIndices[TrianglePathIndex];
		const int32 TriangleB = CurrentPath.TriangleIndices[TrianglePathIndex + 1];
		int32 PortalIndex = INDEX_NONE;
		if (NavData.TriangleAdjacency.IsValidIndex(TriangleA))
		{
			for (const FMobileSurfaceTriangleAdjacency& Adjacency : NavData.TriangleAdjacency[TriangleA].Neighbors)
			{
				if (Adjacency.NeighborTriangleIndex == TriangleB)
				{
					PortalIndex = Adjacency.PortalIndex;
					break;
				}
			}
		}
		if (!NavData.Portals.IsValidIndex(PortalIndex))
		{
			continue;
		}

		const FMobileSurfaceNavPortal& Portal = NavData.Portals[PortalIndex];
		const FVector Left = LocalToWorld.TransformPosition(Portal.LeftPoint);
		const FVector Right = LocalToWorld.TransformPosition(Portal.RightPoint);
		const FVector Center = LocalToWorld.TransformPosition(Portal.Center);
		DrawDebugLine(World, Left, Right, FColor::Cyan, false, Duration, DepthPriority, 4.0f);
		DrawDebugPoint(World, Center, 12.0f, FColor::Cyan, false, Duration, DepthPriority);
		if (bDrawCurrentPathTriangleLabels)
		{
			DrawDebugString(
				World,
				Center + FVector(0.0, 0.0, 18.0f),
				FString::Printf(TEXT("P%d"), PortalIndex),
				nullptr,
				FColor::Cyan,
				Duration,
				false,
				1.0f);
		}
	}

	if (CurrentPath.RawWaypoints.Num() >= 2)
	{
		for (int32 WaypointIndex = 0; WaypointIndex + 1 < CurrentPath.RawWaypoints.Num(); ++WaypointIndex)
		{
			const FVector Start = LocalToWorld.TransformPosition(CurrentPath.RawWaypoints[WaypointIndex]);
			const FVector End = LocalToWorld.TransformPosition(CurrentPath.RawWaypoints[WaypointIndex + 1]);
			DrawDebugLine(World, Start, End, FColor::Orange, false, Duration, DepthPriority, 2.0f);
			DrawDebugPoint(World, Start, 10.0f, WaypointIndex == 0 ? FColor::Green : FColor::Orange, false, Duration, DepthPriority);
		}
		DrawDebugPoint(World, LocalToWorld.TransformPosition(CurrentPath.RawWaypoints.Last()), 10.0f, FColor::Orange, false, Duration, DepthPriority);
	}

	if (CurrentPath.Waypoints.Num() >= 2)
	{
		for (int32 WaypointIndex = 0; WaypointIndex + 1 < CurrentPath.Waypoints.Num(); ++WaypointIndex)
		{
			const FVector Start = LocalToWorld.TransformPosition(CurrentPath.Waypoints[WaypointIndex]);
			const FVector End = LocalToWorld.TransformPosition(CurrentPath.Waypoints[WaypointIndex + 1]);
			DrawDebugLine(World, Start, End, FColor::Red, false, Duration, DepthPriority, 6.0f);
			DrawDebugPoint(World, Start, 16.0f, WaypointIndex == 0 ? FColor::Green : FColor::Red, false, Duration, DepthPriority);
			if (bDrawCurrentPathTriangleLabels)
			{
				DrawDebugString(World, Start + FVector(0.0, 0.0, 24.0f), FString::Printf(TEXT("W%d"), WaypointIndex), nullptr, FColor::Red, Duration, false, 1.0f);
			}
		}
		DrawDebugPoint(World, LocalToWorld.TransformPosition(CurrentPath.Waypoints.Last()), 16.0f, FColor::Red, false, Duration, DepthPriority);
	}

	if (GetOwner())
	{
		DrawDebugString(
			World,
			GetOwner()->GetActorLocation() + FVector(0.0, 0.0, AgentRadius + 40.0f),
			FString::Printf(TEXT("%s Seg=%d Wp=%d"), LexAgentStateText(AgentState), CurrentSegmentIndex, CurrentWaypointIndex),
			nullptr,
			FColor::White,
			Duration,
			false,
			1.0f);
	}
}

void UMobileSurfaceNavAgentComponent::AdvanceBeyondCurrentSegmentOrComplete()
{
	if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		const FMobileSurfaceNavPathSegment& NextSegment = CurrentPath.Segments[CurrentSegmentIndex];
		if (NextSegment.SegmentType == EMobileSurfaceNavPathSegmentType::Walk)
		{
			CurrentWaypointIndex = FMath::Max(CurrentWaypointIndex + 1, NextSegment.StartWaypointIndex + 1);
		}
		return;
	}

	HandleMoveCompleted();
}

void UMobileSurfaceNavAgentComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (bDrawCurrentPathDebug)
	{
		DrawCurrentPathDebug();
	}

	if (!IsBoardedOnActiveElevator())
	{
		if (!bHasCachedNavigationLocalPosition)
		{
			CacheCurrentNavigationLocalPosition();
		}
		else if (AgentState == EMobileSurfaceNavAgentState::Idle || AgentState == EMobileSurfaceNavAgentState::WaitingForPath || !CurrentPath.bIsValid)
		{
			SyncOwnerToCachedNavigationLocalPosition();
		}
	}

	PollPendingPathRequest();

	if (PendingPathRequest.IsValid())
	{
		if (bRepathWhenNavigationChanges && NavigationComponent && bHasActiveTarget &&
			LastRequestedRuntimeStateRevision != NavigationComponent->GetRuntimeStateRevision())
		{
			ClearPendingPathRequest();
			RepathToActiveTarget(false);
		}
		return;
	}

	if (bPendingInitialRandomPathRequest)
	{
		RepathTimer -= DeltaTime;
		if (RepathTimer <= 0.0f)
		{
			RepathTimer = RepathDelay;
			bPendingInitialRandomPathRequest = !RequestRandomPath();
		}
		return;
	}

	if (bRepathWhenNavigationChanges && NavigationComponent && bHasActiveTarget && CurrentPath.bIsValid &&
		!IsInBlockingSpecialLinkTraversal() &&
		NavigationComponent->GetRuntimeStateRevision() != ObservedRuntimeStateRevision)
	{
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s nav revision changed: observed=%d current=%d repathing"),
				*GetNameSafe(GetOwner()),
				ObservedRuntimeStateRevision,
				NavigationComponent->GetRuntimeStateRevision());
		}
		ClearPendingPathRequest();
		RepathToActiveTarget(false);
		return;
	}

	if (bRepathWhenNavigationChanges && NavigationComponent && bHasActiveTarget && bWaitingForNavigationChange &&
		!IsInBlockingSpecialLinkTraversal() &&
		NavigationComponent->GetRuntimeStateRevision() != ObservedRuntimeStateRevision)
	{
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s nav revision changed while waiting for path: observed=%d current=%d retrying"),
				*GetNameSafe(GetOwner()),
				ObservedRuntimeStateRevision,
				NavigationComponent->GetRuntimeStateRevision());
		}
		ObservedRuntimeStateRevision = NavigationComponent->GetRuntimeStateRevision();
		RepathToActiveTarget(false);
		return;
	}

	if (!NavigationComponent || !CurrentPath.bIsValid || CurrentPath.Waypoints.Num() == 0)
	{
		RepathTimer -= DeltaTime;
		if (bWaitingForNavigationChange)
		{
			return;
		}

		if (RepathTimer <= 0.0f && bPendingInitialRandomPathRequest)
		{
			RepathTimer = RepathDelay;
			bPendingInitialRandomPathRequest = !RequestRandomPath();
		}
		return;
	}

	if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		const FMobileSurfaceNavPathSegment& CurrentSegment = CurrentPath.Segments[CurrentSegmentIndex];
		if (CurrentSegment.SegmentType != EMobileSurfaceNavPathSegmentType::Walk)
		{
			if (AgentState == EMobileSurfaceNavAgentState::Moving)
			{
				BeginCurrentSpecialLink();
			}
			if (AgentState == EMobileSurfaceNavAgentState::UsingSpecialLink || AgentState == EMobileSurfaceNavAgentState::WaitingForElevator)
			{
				TickCurrentSpecialLink(DeltaTime);
				return;
			}
		}
		else
		{
			AgentState = EMobileSurfaceNavAgentState::Moving;
			CurrentWaypointIndex = FMath::Max(CurrentWaypointIndex, CurrentSegment.StartWaypointIndex + 1);
		}
	}

	if (CurrentWaypointIndex >= CurrentPath.Waypoints.Num() || !CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		HandleMoveCompleted();
		return;
	}

	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
	if (!SpaceComponent)
	{
		return;
	}

	const FVector CurrentLocal = bHasCachedNavigationLocalPosition
		? CachedNavigationLocalPosition
		: SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	const FVector TargetLocal = CurrentPath.Waypoints[CurrentWaypointIndex];
	const FVector ToTarget = TargetLocal - CurrentLocal;
	const double DistanceToTarget = ToTarget.Length();
	const float MaxTravelThisTick = MoveSpeed * DeltaTime;
	const double SnapDistance = FMath::Max(static_cast<double>(PositionSnapTolerance), static_cast<double>(MaxTravelThisTick));

	if (DistanceToTarget <= SnapDistance)
	{
		CachedNavigationLocalPosition = TargetLocal;
		bHasCachedNavigationLocalPosition = true;
		SyncOwnerToCachedNavigationLocalPosition();
		++CurrentWaypointIndex;
		LastProgressWaypointIndex = CurrentWaypointIndex;
		LastProgressWorldPosition = Owner->GetActorLocation();
		SameWaypointStuckChecks = 0;
		StuckCheckTimer = 0.0f;
		if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex) &&
			CurrentWaypointIndex > CurrentPath.Segments[CurrentSegmentIndex].EndWaypointIndex)
		{
			++CurrentSegmentIndex;
			if (!CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
			{
				HandleMoveCompleted();
				return;
			}
		}
		if (CurrentWaypointIndex >= CurrentPath.Waypoints.Num())
		{
			HandleMoveCompleted();
		}
		return;
	}

	const FVector Step = ToTarget.GetSafeNormal() * MaxTravelThisTick;
	CachedNavigationLocalPosition = CurrentLocal + Step.GetClampedToMaxSize(DistanceToTarget);
	bHasCachedNavigationLocalPosition = true;
	SyncOwnerToCachedNavigationLocalPosition();

	if (bEnableStuckRecovery && bHasActiveTarget)
	{
		StuckCheckTimer += DeltaTime;
		if (StuckCheckTimer >= StuckCheckInterval)
		{
			StuckCheckTimer = 0.0f;
			const FVector NewWorldPosition = Owner->GetActorLocation();
			const bool bSameWaypoint = LastProgressWaypointIndex == CurrentWaypointIndex;
			const float ProgressDistance = FVector::Dist(NewWorldPosition, LastProgressWorldPosition);
			if (bSameWaypoint && ProgressDistance < MinProgressDistance)
			{
				++SameWaypointStuckChecks;
				if (SameWaypointStuckChecks >= MaxSameWaypointStuckChecks)
				{
					if (bLogPathRequests)
					{
					UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s stuck near waypoint=%d progress=%.2f, repathing to target=%s"),
							*GetNameSafe(GetOwner()),
							CurrentWaypointIndex,
							ProgressDistance,
							*ActiveTargetLocalPosition.ToCompactString());
					}
					SameWaypointStuckChecks = 0;
					RepathToActiveTarget(false);
					return;
				}
			}
			else
			{
				SameWaypointStuckChecks = 0;
				LastProgressWaypointIndex = CurrentWaypointIndex;
				LastProgressWorldPosition = NewWorldPosition;
			}
		}
	}
}

bool UMobileSurfaceNavAgentComponent::PickRandomNavigablePoint(FVector& OutLocalPoint)
{
	if (!NavigationComponent)
	{
		return false;
	}

	const FMobileSurfaceNavData& NavData = NavigationComponent->GetNavigationData();
	if (!NavData.bIsValid || NavData.Triangles.IsEmpty())
	{
		return false;
	}

	for (int32 AttemptIndex = 0; AttemptIndex < 16; ++AttemptIndex)
	{
		const int32 TriangleIndex = RandomStream.RandRange(0, NavData.Triangles.Num() - 1);
		OutLocalPoint = NavData.Triangles[TriangleIndex].Center;

		FMobileSurfaceNavPath TestPath;
		if (NavigationComponent->FindPathLocal(OutLocalPoint, OutLocalPoint, TestPath, AgentRadius))
		{
			return true;
		}
	}

	return false;
}
