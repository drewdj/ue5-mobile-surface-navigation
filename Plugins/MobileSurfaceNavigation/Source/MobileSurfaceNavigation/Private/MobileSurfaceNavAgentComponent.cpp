#include "MobileSurfaceNavAgentComponent.h"

#include "MobileSurfaceNavElevator.h"
#include "MobileSurfaceNavComponent.h"
#include "MobileSurfaceNavSubsystem.h"

#include "Components/SceneComponent.h"
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
}

static const TCHAR* LexBoolText(const bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
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

static bool ResolveSegmentLinkNodeIndices(
	const FMobileSurfaceNavPathSegment& Segment,
	const FMobileSurfaceNavSpecialLink& Link,
	int32& OutEntryNodeIndex,
	int32& OutExitNodeIndex)
{
	OutEntryNodeIndex = Segment.SpecialLinkEntryNodeIndex;
	OutExitNodeIndex = Segment.SpecialLinkExitNodeIndex;

	if (OutEntryNodeIndex == INDEX_NONE || OutExitNodeIndex == INDEX_NONE)
	{
		OutEntryNodeIndex = 0;
		OutExitNodeIndex = 1;
	}

	return OutEntryNodeIndex != OutExitNodeIndex &&
		Link.GetNodeCount() > 1 &&
		OutEntryNodeIndex >= 0 &&
		OutExitNodeIndex >= 0 &&
		OutEntryNodeIndex < Link.GetNodeCount() &&
		OutExitNodeIndex < Link.GetNodeCount();
}

static TArray<FVector> BuildSpecialLinkRouteWorldLocations(
	const USceneComponent* SpaceComponent,
	const FMobileSurfaceNavPathSegment& Segment,
	const FMobileSurfaceNavSpecialLink& Link)
{
	TArray<FVector> RouteWorldLocations;
	if (!SpaceComponent)
	{
		return RouteWorldLocations;
	}

	if (Segment.SpecialLinkTraversalNodeIndices.Num() > 0)
	{
		for (const int32 NodeIndex : Segment.SpecialLinkTraversalNodeIndices)
		{
			RouteWorldLocations.Add(SpaceComponent->GetComponentTransform().TransformPosition(Link.GetNodeLocalPosition(NodeIndex)));
		}
		return RouteWorldLocations;
	}

	int32 EntryNodeIndex = INDEX_NONE;
	int32 ExitNodeIndex = INDEX_NONE;
	if (!ResolveSegmentLinkNodeIndices(Segment, Link, EntryNodeIndex, ExitNodeIndex))
	{
		return RouteWorldLocations;
	}

	RouteWorldLocations.Add(SpaceComponent->GetComponentTransform().TransformPosition(Link.GetNodeLocalPosition(EntryNodeIndex)));
	RouteWorldLocations.Add(SpaceComponent->GetComponentTransform().TransformPosition(Link.GetNodeLocalPosition(ExitNodeIndex)));
	return RouteWorldLocations;
}

static int32 ResolveSpecialLinkDirectionSign(const FMobileSurfaceNavPathSegment& Segment)
{
	if (Segment.SpecialLinkEntryNodeIndex == INDEX_NONE || Segment.SpecialLinkExitNodeIndex == INDEX_NONE)
	{
		return 0;
	}

	if (Segment.SpecialLinkExitNodeIndex > Segment.SpecialLinkEntryNodeIndex)
	{
		return 1;
	}

	if (Segment.SpecialLinkExitNodeIndex < Segment.SpecialLinkEntryNodeIndex)
	{
		return -1;
	}

	return 0;
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
	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
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
	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
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
	if (bLogPathRequests)
	{
		UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s immediate path success: triangles=%d waypoints=%d length=%.1f revision=%d"),
			*GetNameSafe(GetOwner()),
			CurrentPath.TriangleIndices.Num(),
			CurrentPath.Waypoints.Num(),
			CurrentPath.EstimatedLength,
			ObservedRuntimeStateRevision);
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
	if (bLogPathRequests)
	{
		UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s queued path success: triangles=%d waypoints=%d length=%.1f revision=%d"),
			*GetNameSafe(GetOwner()),
			CurrentPath.TriangleIndices.Num(),
			CurrentPath.Waypoints.Num(),
			CurrentPath.EstimatedLength,
			ObservedRuntimeStateRevision);
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
		RequestRandomPath();
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

bool UMobileSurfaceNavAgentComponent::BeginCurrentSpecialLink()
{
	if (!CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		return false;
	}

	const FMobileSurfaceNavPathSegment& Segment = CurrentPath.Segments[CurrentSegmentIndex];
	if (!NavigationComponent)
	{
		return false;
	}

	if (Segment.SegmentType == EMobileSurfaceNavPathSegmentType::Elevator)
	{
		if (!NavigationComponent->GetNavigationData().SpecialLinks.IsValidIndex(Segment.SpecialLinkIndex))
		{
			if (bLogPathRequests)
			{
				UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s elevator segment has invalid special link index=%d"),
					*GetNameSafe(GetOwner()),
					Segment.SpecialLinkIndex);
			}
			AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
			return false;
		}

		const FMobileSurfaceNavSpecialLink& Link = NavigationComponent->GetNavigationData().SpecialLinks[Segment.SpecialLinkIndex];
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s begin elevator segment: linkIndex=%d linkId=%s actor=%s entryNode=%d exitNode=%d startWp=%d endWp=%d"),
				*GetNameSafe(GetOwner()),
				Segment.SpecialLinkIndex,
				*Link.LinkId.ToString(),
				*GetNameSafe(Link.ElevatorActor.Get()),
				Segment.SpecialLinkEntryNodeIndex,
				Segment.SpecialLinkExitNodeIndex,
				Segment.StartWaypointIndex,
				Segment.EndWaypointIndex);
		}

		if (AMobileSurfaceNavElevator* ElevatorActor = Link.ElevatorActor.Get())
		{
			AActor* Owner = GetOwner();
			const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
			if (!Owner || !SpaceComponent)
			{
				return false;
			}

			int32 EntryNodeIndex = INDEX_NONE;
			int32 ExitNodeIndex = INDEX_NONE;
			if (!ResolveSegmentLinkNodeIndices(Segment, Link, EntryNodeIndex, ExitNodeIndex))
			{
				if (bLogPathRequests)
				{
					UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s elevator link '%s' has invalid node setup entryNode=%d exitNode=%d"),
						*GetNameSafe(Owner),
						*Link.LinkId.ToString(),
						Segment.SpecialLinkEntryNodeIndex,
						Segment.SpecialLinkExitNodeIndex);
				}
				AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
				return false;
			}

			if (ActiveElevatorActor.Get() != ElevatorActor)
			{
				ResetActiveSpecialLinkState();
				ActiveElevatorActor = ElevatorActor;
			}

			if (!bElevatorBoardingRequested)
			{
				const FVector EntryWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.StartWaypointIndex]);
				const FVector ExitWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.EndWaypointIndex]);
				const TArray<FVector> RouteWorldLocations = BuildSpecialLinkRouteWorldLocations(SpaceComponent, Segment, Link);
				if (bLogPathRequests)
				{
					UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s requesting elevator boarding: elevator=%s entryNode=%d exitNode=%d entry=%s exit=%s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor),
						EntryNodeIndex,
						ExitNodeIndex,
						*EntryWorld.ToCompactString(),
						*ExitWorld.ToCompactString());
				}
				if (!ElevatorActor->RequestBoardingWithRoute(Owner, RouteWorldLocations))
				{
					if (bLogPathRequests)
					{
						UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s elevator boarding request rejected by %s"),
							*GetNameSafe(Owner),
							*GetNameSafe(ElevatorActor));
					}
					return false;
				}
				bElevatorBoardingRequested = true;
			}

			AgentState = ElevatorActor->IsAgentBoarded(Owner)
				? EMobileSurfaceNavAgentState::UsingSpecialLink
				: EMobileSurfaceNavAgentState::WaitingForElevator;
			if (bLogPathRequests)
			{
				UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s elevator state after begin: %s"),
					*GetNameSafe(Owner),
					LexAgentStateText(AgentState));
			}
			return true;
		}

		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s elevator link '%s' has no ElevatorActor assigned"),
				*GetNameSafe(GetOwner()),
				*Link.LinkId.ToString());
		}
		AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
		return false;
	}

	if (Segment.SegmentType == EMobileSurfaceNavPathSegmentType::Ladder)
	{
		if (!NavigationComponent->GetNavigationData().SpecialLinks.IsValidIndex(Segment.SpecialLinkIndex))
		{
			return false;
		}

		const FMobileSurfaceNavSpecialLink& Link = NavigationComponent->GetNavigationData().SpecialLinks[Segment.SpecialLinkIndex];
		return TryBeginLadderTraversal(Segment, Link);
	}

	AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
	return true;
}

bool UMobileSurfaceNavAgentComponent::TickCurrentSpecialLink(const float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner || !NavigationComponent || !CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		return false;
	}

	const FMobileSurfaceNavPathSegment& Segment = CurrentPath.Segments[CurrentSegmentIndex];
	if (!CurrentPath.Waypoints.IsValidIndex(Segment.EndWaypointIndex))
	{
		return false;
	}

	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
	if (!SpaceComponent)
	{
		return false;
	}

	const FVector TargetWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.EndWaypointIndex]);

	if (Segment.SegmentType == EMobileSurfaceNavPathSegmentType::Elevator)
	{
		if (!NavigationComponent->GetNavigationData().SpecialLinks.IsValidIndex(Segment.SpecialLinkIndex))
		{
			AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
			return true;
		}

		const FMobileSurfaceNavSpecialLink& Link = NavigationComponent->GetNavigationData().SpecialLinks[Segment.SpecialLinkIndex];
		if (AMobileSurfaceNavElevator* ElevatorActor = Link.ElevatorActor.Get())
		{
			int32 EntryNodeIndex = INDEX_NONE;
			int32 ExitNodeIndex = INDEX_NONE;
			if (!ResolveSegmentLinkNodeIndices(Segment, Link, EntryNodeIndex, ExitNodeIndex))
			{
				AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
				return true;
			}

			if (!bElevatorBoardingRequested || ActiveElevatorActor.Get() != ElevatorActor)
			{
				ActiveElevatorActor = ElevatorActor;
				const FVector EntryWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.StartWaypointIndex]);
				const FVector ExitWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.EndWaypointIndex]);
				const TArray<FVector> RouteWorldLocations = BuildSpecialLinkRouteWorldLocations(SpaceComponent, Segment, Link);
				if (bLogPathRequests)
				{
					UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s refresh elevator boarding request: elevator=%s entryNode=%d exitNode=%d entry=%s exit=%s requested=%s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor),
						EntryNodeIndex,
						ExitNodeIndex,
						*EntryWorld.ToCompactString(),
						*ExitWorld.ToCompactString(),
						LexBoolText(bElevatorBoardingRequested));
				}
				if (!ElevatorActor->RequestBoardingWithRoute(Owner, RouteWorldLocations))
				{
					if (bLogPathRequests)
					{
						UE_LOG(LogMobileSurfaceNavAgent, Warning, TEXT("%s elevator refresh request rejected by %s"),
							*GetNameSafe(Owner),
							*GetNameSafe(ElevatorActor));
					}
					AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
					return true;
				}
				bElevatorBoardingRequested = true;
			}

			FVector RideWorldLocation = FVector::ZeroVector;
			if (ElevatorActor->IsTraversalComplete(Owner))
			{
				if (bLogPathRequests)
				{
					UE_LOG(LogMobileSurfaceNavAgent, Log, TEXT("%s elevator traversal complete on %s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor));
				}
				Owner->SetActorLocation(TargetWorld);
				ElevatorActor->FinishTraversal(Owner);
				ActiveElevatorActor.Reset();
				bElevatorBoardingRequested = false;
				CurrentWaypointIndex = Segment.EndWaypointIndex;
				++CurrentSegmentIndex;
				AgentState = EMobileSurfaceNavAgentState::Moving;
				if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
				{
					const FMobileSurfaceNavPathSegment& NextSegment = CurrentPath.Segments[CurrentSegmentIndex];
					if (NextSegment.SegmentType == EMobileSurfaceNavPathSegmentType::Walk)
					{
						CurrentWaypointIndex = FMath::Max(CurrentWaypointIndex + 1, NextSegment.StartWaypointIndex + 1);
					}
				}
				else
				{
					HandleMoveCompleted();
				}

				if (bHasDeferredMoveRequest)
				{
					ConsumeDeferredMoveRequest();
				}
				return true;
			}

			if (ElevatorActor->GetAgentRideWorldLocation(Owner, RideWorldLocation))
			{
				if (bLogPathRequests)
				{
					UE_LOG(LogMobileSurfaceNavAgent, Verbose, TEXT("%s riding elevator %s at %s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor),
						*RideWorldLocation.ToCompactString());
				}
				Owner->SetActorLocation(RideWorldLocation);
				AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
				return true;
			}

			const FVector EntryWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.StartWaypointIndex]);
			if (FVector::DistSquared(Owner->GetActorLocation(), EntryWorld) > FMath::Square(AcceptanceRadius))
			{
				const FVector ToEntry = EntryWorld - Owner->GetActorLocation();
				const double DistanceToEntry = ToEntry.Length();
				const FVector StepToEntry = ToEntry.GetSafeNormal() * MoveSpeed * DeltaTime;
				Owner->SetActorLocation(Owner->GetActorLocation() + StepToEntry.GetClampedToMaxSize(DistanceToEntry));
			}
			if (bLogPathRequests)
			{
				UE_LOG(LogMobileSurfaceNavAgent, Verbose, TEXT("%s waiting for elevator %s near entry=%s current=%s"),
					*GetNameSafe(Owner),
					*GetNameSafe(ElevatorActor),
					*EntryWorld.ToCompactString(),
					*Owner->GetActorLocation().ToCompactString());
			}
			AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
			return true;
		}

		AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
		return true;
	}

	if (Segment.SegmentType == EMobileSurfaceNavPathSegmentType::Ladder)
	{
		if (!NavigationComponent->GetNavigationData().SpecialLinks.IsValidIndex(Segment.SpecialLinkIndex))
		{
			return false;
		}

		const FMobileSurfaceNavSpecialLink& Link = NavigationComponent->GetNavigationData().SpecialLinks[Segment.SpecialLinkIndex];
		return TickCurrentLadderTraversal(Segment, Link, DeltaTime);
	}

	AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;

	const FVector CurrentWorld = Owner->GetActorLocation();
	const FVector ToTarget = TargetWorld - CurrentWorld;
	const double DistanceToTarget = ToTarget.Length();
	const float TraversalSpeed = GetSpecialLinkTraversalSpeed(Segment);

	if (DistanceToTarget <= AcceptanceRadius)
	{
		Owner->SetActorLocation(TargetWorld);
		CurrentWaypointIndex = Segment.EndWaypointIndex;
		++CurrentSegmentIndex;
		AgentState = EMobileSurfaceNavAgentState::Moving;
		if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
		{
			const FMobileSurfaceNavPathSegment& NextSegment = CurrentPath.Segments[CurrentSegmentIndex];
			if (NextSegment.SegmentType == EMobileSurfaceNavPathSegmentType::Walk)
			{
				CurrentWaypointIndex = FMath::Max(CurrentWaypointIndex + 1, NextSegment.StartWaypointIndex + 1);
			}
		}
		else
		{
			HandleMoveCompleted();
		}
		return true;
	}

	const FVector Step = ToTarget.GetSafeNormal() * TraversalSpeed * DeltaTime;
	Owner->SetActorLocation(CurrentWorld + Step.GetClampedToMaxSize(DistanceToTarget));
	return true;
}

bool UMobileSurfaceNavAgentComponent::IsElevatorCurrentlyAvailable(const FMobileSurfaceNavPathSegment& Segment) const
{
	if (Segment.SegmentType != EMobileSurfaceNavPathSegmentType::Elevator)
	{
		return true;
	}

	if (ElevatorAvailabilityInterval <= UE_SMALL_NUMBER || ElevatorAvailabilityWindow >= ElevatorAvailabilityInterval)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const float CycleTime = FMath::Fmod(World->GetTimeSeconds(), ElevatorAvailabilityInterval);
	return CycleTime <= ElevatorAvailabilityWindow;
}

float UMobileSurfaceNavAgentComponent::GetSpecialLinkTraversalSpeed(const FMobileSurfaceNavPathSegment& Segment) const
{
	switch (Segment.SegmentType)
	{
	case EMobileSurfaceNavPathSegmentType::Ladder:
		return LadderTraversalSpeed;
	case EMobileSurfaceNavPathSegmentType::Elevator:
		return ElevatorTraversalSpeed;
	case EMobileSurfaceNavPathSegmentType::Jump:
		return JumpTraversalSpeed;
	default:
		return MoveSpeed;
	}
}

bool UMobileSurfaceNavAgentComponent::TryBeginLadderTraversal(
	const FMobileSurfaceNavPathSegment& Segment,
	const FMobileSurfaceNavSpecialLink& Link)
{
	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = NavigationComponent && NavigationComponent->GetOwner()
		? NavigationComponent->GetOwner()->GetRootComponent()
		: nullptr;
	if (!Owner || !NavigationComponent || !SpaceComponent)
	{
		return false;
	}

	if (ActiveLadderLinkIndex != Segment.SpecialLinkIndex)
	{
		ActiveSpecialLinkRouteWorldLocations.Reset();
		ActiveSpecialLinkRouteTargetIndex = INDEX_NONE;
		ActiveLadderLinkIndex = INDEX_NONE;
		ActiveLadderDirectionSign = 0;
	}

	const int32 DirectionSign = ResolveSpecialLinkDirectionSign(Segment);
	if (ActiveLadderLinkIndex == INDEX_NONE)
	{
		if (!NavigationComponent->TryAcquireLadderTraversal(Segment.SpecialLinkIndex, Owner, DirectionSign))
		{
			AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
			return true;
		}

		ActiveLadderLinkIndex = Segment.SpecialLinkIndex;
		ActiveLadderDirectionSign = DirectionSign;
		ActiveSpecialLinkRouteWorldLocations = BuildSpecialLinkRouteWorldLocations(SpaceComponent, Segment, Link);
		ActiveSpecialLinkRouteTargetIndex = 0;
		while (ActiveSpecialLinkRouteWorldLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex) &&
			FVector::DistSquared(Owner->GetActorLocation(), ActiveSpecialLinkRouteWorldLocations[ActiveSpecialLinkRouteTargetIndex]) <= FMath::Square(AcceptanceRadius))
		{
			++ActiveSpecialLinkRouteTargetIndex;
		}
	}

	AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
	return true;
}

bool UMobileSurfaceNavAgentComponent::TickCurrentLadderTraversal(
	const FMobileSurfaceNavPathSegment& Segment,
	const FMobileSurfaceNavSpecialLink& Link,
	const float DeltaTime)
{
	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = NavigationComponent && NavigationComponent->GetOwner()
		? NavigationComponent->GetOwner()->GetRootComponent()
		: nullptr;
	if (!Owner || !NavigationComponent || !SpaceComponent)
	{
		return false;
	}

	if (ActiveLadderLinkIndex != Segment.SpecialLinkIndex)
	{
		if (!TryBeginLadderTraversal(Segment, Link))
		{
			return false;
		}
	}

	if (ActiveLadderLinkIndex != Segment.SpecialLinkIndex)
	{
		AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
		return true;
	}

	if (!ActiveSpecialLinkRouteWorldLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex))
	{
		CompleteCurrentSpecialLinkSegment();
		return true;
	}

	const FVector CurrentWorld = Owner->GetActorLocation();
	const FVector TargetRouteWorld = ActiveSpecialLinkRouteWorldLocations[ActiveSpecialLinkRouteTargetIndex];
	const FVector ToTarget = TargetRouteWorld - CurrentWorld;
	const double DistanceToTarget = ToTarget.Length();
	const float TraversalSpeed = GetSpecialLinkTraversalSpeed(Segment);

	if (DistanceToTarget <= AcceptanceRadius)
	{
		Owner->SetActorLocation(TargetRouteWorld);
		++ActiveSpecialLinkRouteTargetIndex;
		if (bHasDeferredMoveRequest && ConsumeDeferredMoveRequestFromCurrentLocation())
		{
			return true;
		}
		if (!ActiveSpecialLinkRouteWorldLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex))
		{
			CompleteCurrentSpecialLinkSegment();
		}
		return true;
	}

	const FVector Step = ToTarget.GetSafeNormal() * TraversalSpeed * DeltaTime;
	Owner->SetActorLocation(CurrentWorld + Step.GetClampedToMaxSize(DistanceToTarget));
	AgentState = EMobileSurfaceNavAgentState::UsingSpecialLink;
	return true;
}

void UMobileSurfaceNavAgentComponent::CompleteCurrentSpecialLinkSegment()
{
	if (!CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		return;
	}

	const FMobileSurfaceNavPathSegment& Segment = CurrentPath.Segments[CurrentSegmentIndex];
	if (Segment.SegmentType == EMobileSurfaceNavPathSegmentType::Ladder && NavigationComponent)
	{
		if (AActor* Owner = GetOwner())
		{
			NavigationComponent->ReleaseLadderTraversal(Segment.SpecialLinkIndex, Owner);
		}
		ActiveLadderLinkIndex = INDEX_NONE;
		ActiveLadderDirectionSign = 0;
		ActiveSpecialLinkRouteWorldLocations.Reset();
		ActiveSpecialLinkRouteTargetIndex = INDEX_NONE;
	}

	CurrentWaypointIndex = Segment.EndWaypointIndex;
	++CurrentSegmentIndex;
	AgentState = EMobileSurfaceNavAgentState::Moving;
	if (CurrentPath.Segments.IsValidIndex(CurrentSegmentIndex))
	{
		const FMobileSurfaceNavPathSegment& NextSegment = CurrentPath.Segments[CurrentSegmentIndex];
		if (NextSegment.SegmentType == EMobileSurfaceNavPathSegmentType::Walk)
		{
			CurrentWaypointIndex = FMath::Max(CurrentWaypointIndex + 1, NextSegment.StartWaypointIndex + 1);
		}
	}
	else
	{
		HandleMoveCompleted();
	}
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

void UMobileSurfaceNavAgentComponent::ResetActiveSpecialLinkState()
{
	AActor* Owner = GetOwner();
	if (NavigationComponent && ActiveLadderLinkIndex != INDEX_NONE && Owner)
	{
		NavigationComponent->ReleaseLadderTraversal(ActiveLadderLinkIndex, Owner);
	}

	if (AMobileSurfaceNavElevator* ElevatorActor = ActiveElevatorActor.Get())
	{
		if (Owner)
		{
			ElevatorActor->CancelRequest(Owner);
			if (ElevatorActor->IsAgentBoarded(Owner) || ElevatorActor->IsTraversalComplete(Owner))
			{
				ElevatorActor->FinishTraversal(Owner);
			}
		}
	}

	ActiveElevatorActor.Reset();
	bElevatorBoardingRequested = false;
	ActiveLadderLinkIndex = INDEX_NONE;
	ActiveLadderDirectionSign = 0;
	ActiveSpecialLinkRouteWorldLocations.Reset();
	ActiveSpecialLinkRouteTargetIndex = INDEX_NONE;
}

bool UMobileSurfaceNavAgentComponent::IsBoardedOnActiveElevator() const
{
	const AActor* Owner = GetOwner();
	const AMobileSurfaceNavElevator* ElevatorActor = ActiveElevatorActor.Get();
	return Owner && ElevatorActor && ElevatorActor->IsAgentBoarded(const_cast<AActor*>(Owner));
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

	const FVector TargetWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[CurrentWaypointIndex]);
	const FVector CurrentWorld = Owner->GetActorLocation();
	const FVector ToTarget = TargetWorld - CurrentWorld;
	const double DistanceToTarget = ToTarget.Length();

	if (DistanceToTarget <= AcceptanceRadius)
	{
		++CurrentWaypointIndex;
		LastProgressWaypointIndex = CurrentWaypointIndex;
		LastProgressWorldPosition = CurrentWorld;
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

	const FVector Step = ToTarget.GetSafeNormal() * MoveSpeed * DeltaTime;
	Owner->SetActorLocation(CurrentWorld + Step.GetClampedToMaxSize(DistanceToTarget));

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
