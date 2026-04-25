#include "MobileSurfaceNavAgentComponent.h"

#include "MobileSurfaceNavComponent.h"
#include "MobileSurfaceNavElevator.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfaceNavAgentSpecialLinks, Log, All);

namespace
{
	static const TCHAR* LexSpecialLinkBoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	static const TCHAR* LexSpecialLinkAgentStateText(const EMobileSurfaceNavAgentState State)
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

	static TArray<FVector> BuildSpecialLinkRouteLocalLocations(
		const FMobileSurfaceNavPathSegment& Segment,
		const FMobileSurfaceNavSpecialLink& Link)
	{
		TArray<FVector> RouteLocalLocations;

		if (Segment.SpecialLinkTraversalNodeIndices.Num() > 0)
		{
			for (const int32 NodeIndex : Segment.SpecialLinkTraversalNodeIndices)
			{
				RouteLocalLocations.Add(Link.GetNodeLocalPosition(NodeIndex));
			}
			return RouteLocalLocations;
		}

		int32 EntryNodeIndex = INDEX_NONE;
		int32 ExitNodeIndex = INDEX_NONE;
		if (!ResolveSegmentLinkNodeIndices(Segment, Link, EntryNodeIndex, ExitNodeIndex))
		{
			return RouteLocalLocations;
		}

		RouteLocalLocations.Add(Link.GetNodeLocalPosition(EntryNodeIndex));
		RouteLocalLocations.Add(Link.GetNodeLocalPosition(ExitNodeIndex));
		return RouteLocalLocations;
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
				UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Warning, TEXT("%s elevator segment has invalid special link index=%d"),
					*GetNameSafe(GetOwner()),
					Segment.SpecialLinkIndex);
			}
			AgentState = EMobileSurfaceNavAgentState::WaitingForElevator;
			return false;
		}

		const FMobileSurfaceNavSpecialLink& Link = NavigationComponent->GetNavigationData().SpecialLinks[Segment.SpecialLinkIndex];
		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Log, TEXT("%s begin elevator segment: linkIndex=%d linkId=%s actor=%s entryNode=%d exitNode=%d startWp=%d endWp=%d"),
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
			const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
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
					UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Warning, TEXT("%s elevator link '%s' has invalid node setup entryNode=%d exitNode=%d"),
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
					UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Log, TEXT("%s requesting elevator boarding: elevator=%s entryNode=%d exitNode=%d entry=%s exit=%s"),
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
						UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Warning, TEXT("%s elevator boarding request rejected by %s"),
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
				UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Log, TEXT("%s elevator state after begin: %s"),
					*GetNameSafe(Owner),
					LexSpecialLinkAgentStateText(AgentState));
			}
			return true;
		}

		if (bLogPathRequests)
		{
			UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Warning, TEXT("%s elevator link '%s' has no ElevatorActor assigned"),
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

	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!SpaceComponent)
	{
		return false;
	}

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
			const FVector TargetWorld = SpaceComponent->GetComponentTransform().TransformPosition(CurrentPath.Waypoints[Segment.EndWaypointIndex]);
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
					UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Log, TEXT("%s refresh elevator boarding request: elevator=%s entryNode=%d exitNode=%d entry=%s exit=%s requested=%s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor),
						EntryNodeIndex,
						ExitNodeIndex,
						*EntryWorld.ToCompactString(),
						*ExitWorld.ToCompactString(),
						LexSpecialLinkBoolText(bElevatorBoardingRequested));
				}
				if (!ElevatorActor->RequestBoardingWithRoute(Owner, RouteWorldLocations))
				{
					if (bLogPathRequests)
					{
						UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Warning, TEXT("%s elevator refresh request rejected by %s"),
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
					UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Log, TEXT("%s elevator traversal complete on %s"),
						*GetNameSafe(Owner),
						*GetNameSafe(ElevatorActor));
				}
				Owner->SetActorLocation(TargetWorld);
				CacheCurrentNavigationLocalPosition();
				ElevatorActor->FinishTraversal(Owner);
				ActiveElevatorActor.Reset();
				bElevatorBoardingRequested = false;
				CurrentWaypointIndex = Segment.EndWaypointIndex;
				++CurrentSegmentIndex;
				AgentState = EMobileSurfaceNavAgentState::Moving;
				AdvanceBeyondCurrentSegmentOrComplete();

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
					UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Verbose, TEXT("%s riding elevator %s at %s"),
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
				UE_LOG(LogMobileSurfaceNavAgentSpecialLinks, Verbose, TEXT("%s waiting for elevator %s near entry=%s current=%s"),
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

	const FVector CurrentLocal = bHasCachedNavigationLocalPosition
		? CachedNavigationLocalPosition
		: SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	const FVector TargetLocal = CurrentPath.Waypoints[Segment.EndWaypointIndex];
	const FVector ToTarget = TargetLocal - CurrentLocal;
	const double DistanceToTarget = ToTarget.Length();
	const float TraversalSpeed = GetSpecialLinkTraversalSpeed(Segment);
	const float MaxTravelThisTick = TraversalSpeed * DeltaTime;
	const double SnapDistance = FMath::Max(static_cast<double>(PositionSnapTolerance), static_cast<double>(MaxTravelThisTick));

	if (DistanceToTarget <= SnapDistance)
	{
		CachedNavigationLocalPosition = TargetLocal;
		bHasCachedNavigationLocalPosition = true;
		SyncOwnerToCachedNavigationLocalPosition();
		CurrentWaypointIndex = Segment.EndWaypointIndex;
		++CurrentSegmentIndex;
		AgentState = EMobileSurfaceNavAgentState::Moving;
		AdvanceBeyondCurrentSegmentOrComplete();
		return true;
	}

	const FVector Step = ToTarget.GetSafeNormal() * MaxTravelThisTick;
	CachedNavigationLocalPosition = CurrentLocal + Step.GetClampedToMaxSize(DistanceToTarget);
	bHasCachedNavigationLocalPosition = true;
	SyncOwnerToCachedNavigationLocalPosition();
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
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !NavigationComponent || !SpaceComponent)
	{
		return false;
	}

	if (ActiveLadderLinkIndex != Segment.SpecialLinkIndex)
	{
		ActiveSpecialLinkRouteWorldLocations.Reset();
		ActiveSpecialLinkRouteLocalLocations.Reset();
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
		ActiveSpecialLinkRouteLocalLocations = BuildSpecialLinkRouteLocalLocations(Segment, Link);
		ActiveSpecialLinkRouteTargetIndex = 0;
		const FVector CurrentLocal = bHasCachedNavigationLocalPosition
			? CachedNavigationLocalPosition
			: SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
		while (ActiveSpecialLinkRouteLocalLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex) &&
			FVector::DistSquared(CurrentLocal, ActiveSpecialLinkRouteLocalLocations[ActiveSpecialLinkRouteTargetIndex]) <= FMath::Square(AcceptanceRadius))
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
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
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

	if (!ActiveSpecialLinkRouteLocalLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex))
	{
		CompleteCurrentSpecialLinkSegment();
		return true;
	}

	const FVector CurrentLocal = bHasCachedNavigationLocalPosition
		? CachedNavigationLocalPosition
		: SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	const FVector TargetRouteLocal = ActiveSpecialLinkRouteLocalLocations[ActiveSpecialLinkRouteTargetIndex];
	const FVector ToTarget = TargetRouteLocal - CurrentLocal;
	const double DistanceToTarget = ToTarget.Length();
	const float TraversalSpeed = GetSpecialLinkTraversalSpeed(Segment);
	const float MaxTravelThisTick = TraversalSpeed * DeltaTime;
	const double SnapDistance = FMath::Max(static_cast<double>(PositionSnapTolerance), static_cast<double>(MaxTravelThisTick));

	if (DistanceToTarget <= SnapDistance)
	{
		CachedNavigationLocalPosition = TargetRouteLocal;
		bHasCachedNavigationLocalPosition = true;
		SyncOwnerToCachedNavigationLocalPosition();
		++ActiveSpecialLinkRouteTargetIndex;
		if (bHasDeferredMoveRequest && ConsumeDeferredMoveRequestFromCurrentLocation())
		{
			return true;
		}
		if (!ActiveSpecialLinkRouteLocalLocations.IsValidIndex(ActiveSpecialLinkRouteTargetIndex))
		{
			CompleteCurrentSpecialLinkSegment();
		}
		return true;
	}

	const FVector Step = ToTarget.GetSafeNormal() * MaxTravelThisTick;
	CachedNavigationLocalPosition = CurrentLocal + Step.GetClampedToMaxSize(DistanceToTarget);
	bHasCachedNavigationLocalPosition = true;
	SyncOwnerToCachedNavigationLocalPosition();
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
		ActiveSpecialLinkRouteLocalLocations.Reset();
		ActiveSpecialLinkRouteTargetIndex = INDEX_NONE;
	}

	CurrentWaypointIndex = Segment.EndWaypointIndex;
	++CurrentSegmentIndex;
	AgentState = EMobileSurfaceNavAgentState::Moving;
	AdvanceBeyondCurrentSegmentOrComplete();
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
	ActiveSpecialLinkRouteLocalLocations.Reset();
	ActiveSpecialLinkRouteTargetIndex = INDEX_NONE;
}

bool UMobileSurfaceNavAgentComponent::IsBoardedOnActiveElevator() const
{
	const AActor* Owner = GetOwner();
	const AMobileSurfaceNavElevator* ElevatorActor = ActiveElevatorActor.Get();
	return Owner && ElevatorActor && ElevatorActor->IsAgentBoarded(const_cast<AActor*>(Owner));
}

