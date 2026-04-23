#include "MobileSurfaceNavAgentComponent.h"

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

bool UMobileSurfaceNavAgentComponent::RequestMoveToLocal(const FVector& TargetLocalPosition)
{
	return RequestMoveToLocalInternal(TargetLocalPosition, false);
}

bool UMobileSurfaceNavAgentComponent::RequestMoveToLocalInternal(
	const FVector& TargetLocalPosition,
	const bool bPreserveCurrentPathUntilSuccess)
{
	bHasActiveTarget = true;
	bActiveTargetIsRandom = false;
	ActiveTargetLocalPosition = TargetLocalPosition;

	if (!NavigationComponent || !NavigationComponent->HasValidNavigationData())
	{
		ClearCurrentPath();
		ClearPendingPathRequest();
		bHasActiveTarget = false;
		bActiveTargetIsRandom = false;
		return false;
	}

	const AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = NavigationComponent->GetOwner() ? NavigationComponent->GetOwner()->GetRootComponent() : nullptr;
	if (!Owner || !SpaceComponent)
	{
		bHasActiveTarget = false;
		bActiveTargetIsRandom = false;
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
		ObservedRuntimeStateRevision = NavigationComponent->GetRuntimeStateRevision();
		if (!bPreserveCurrentPathUntilSuccess)
		{
			ClearCurrentPath();
		}
		return false;
	}

	CurrentPath = NewPath;
	ClearPendingPathRequest();
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
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
		ObservedRuntimeStateRevision = NavigationComponent ? NavigationComponent->GetRuntimeStateRevision() : INDEX_NONE;
		bPendingPathPreservesCurrentPath = false;
		return false;
	}

	CurrentPath = Path;
	bPendingPathPreservesCurrentPath = false;
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
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
	CurrentPath = FMobileSurfaceNavPath();
	CurrentWaypointIndex = 0;
	LastProgressWaypointIndex = INDEX_NONE;
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
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
	ClearPendingPathRequest();
	ClearCurrentPath();
}

float UMobileSurfaceNavAgentComponent::GetAgentRadius() const
{
	return AgentRadius;
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

	if (CurrentWaypointIndex >= CurrentPath.Waypoints.Num())
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
