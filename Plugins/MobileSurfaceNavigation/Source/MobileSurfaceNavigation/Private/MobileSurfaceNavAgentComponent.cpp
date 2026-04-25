#include "MobileSurfaceNavAgentComponent.h"

#include "MobileSurfaceNavElevator.h"
#include "MobileSurfaceNavComponent.h"
#include "MobileSurfaceNavSubsystem.h"
#include "MobileSurfaceNavigationQuery.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfaceNavAgent, Log, All);

namespace
{
	static FVector EvaluateQuadraticBezier(const FVector& P0, const FVector& P1, const FVector& P2, const float T)
	{
		const float OneMinusT = 1.0f - T;
		return OneMinusT * OneMinusT * P0 + 2.0f * OneMinusT * T * P1 + T * T * P2;
	}
}

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

const TArray<FVector>& UMobileSurfaceNavAgentComponent::GetActivePathWaypoints() const
{
	return FollowPathWaypoints.IsEmpty() ? CurrentPath.Waypoints : FollowPathWaypoints;
}

const TArray<FMobileSurfaceNavPathSegment>& UMobileSurfaceNavAgentComponent::GetActivePathSegments() const
{
	return FollowPathSegments.IsEmpty() ? CurrentPath.Segments : FollowPathSegments;
}

void UMobileSurfaceNavAgentComponent::RebuildFollowPath()
{
	FollowPathWaypoints.Reset();
	FollowPathSegments.Reset();
	FollowPathEstimatedLength = 0.0f;

	if (!CurrentPath.bIsValid || CurrentPath.Waypoints.Num() < 2)
	{
		return;
	}

	const TArray<FVector>& SourceWaypoints = CurrentPath.Waypoints;
	const TArray<FMobileSurfaceNavPathSegment>& SourceSegments = CurrentPath.Segments;
	auto AppendWaypointIfNeeded = [](TArray<FVector>& Waypoints, const FVector& Point) -> int32
	{
		if (Waypoints.IsEmpty() || !Waypoints.Last().Equals(Point, UE_KINDA_SMALL_NUMBER))
		{
			Waypoints.Add(Point);
		}
		return Waypoints.Num() - 1;
	};

	for (const FMobileSurfaceNavPathSegment& SourceSegment : SourceSegments)
	{
		FMobileSurfaceNavPathSegment FollowSegment = SourceSegment;
		FollowSegment.StartWaypointIndex = INDEX_NONE;
		FollowSegment.EndWaypointIndex = INDEX_NONE;

		const bool bCanSmoothSegment =
			bEnablePathSmoothing &&
			SourceSegment.SegmentType == EMobileSurfaceNavPathSegmentType::Walk &&
			SourceWaypoints.IsValidIndex(SourceSegment.StartWaypointIndex) &&
			SourceWaypoints.IsValidIndex(SourceSegment.EndWaypointIndex) &&
			SourceSegment.EndWaypointIndex - SourceSegment.StartWaypointIndex >= 1;

		if (!bCanSmoothSegment)
		{
			for (int32 WaypointIndex = SourceSegment.StartWaypointIndex; WaypointIndex <= SourceSegment.EndWaypointIndex; ++WaypointIndex)
			{
				if (!SourceWaypoints.IsValidIndex(WaypointIndex))
				{
					continue;
				}

				const int32 AppendedIndex = AppendWaypointIfNeeded(FollowPathWaypoints, SourceWaypoints[WaypointIndex]);
				if (FollowSegment.StartWaypointIndex == INDEX_NONE)
				{
					FollowSegment.StartWaypointIndex = AppendedIndex;
				}
				FollowSegment.EndWaypointIndex = AppendedIndex;
			}

			if (FollowSegment.StartWaypointIndex != INDEX_NONE && FollowSegment.EndWaypointIndex != INDEX_NONE)
			{
				FollowPathSegments.Add(FollowSegment);
			}
			continue;
		}

		const int32 StartAppendedIndex = AppendWaypointIfNeeded(FollowPathWaypoints, SourceWaypoints[SourceSegment.StartWaypointIndex]);
		FollowSegment.StartWaypointIndex = StartAppendedIndex;
		FollowSegment.EndWaypointIndex = StartAppendedIndex;
		int32 SmoothedCornerCount = 0;

		for (int32 WaypointIndex = SourceSegment.StartWaypointIndex + 1; WaypointIndex < SourceSegment.EndWaypointIndex; ++WaypointIndex)
		{
			const FVector PrevPoint = SourceWaypoints[WaypointIndex - 1];
			const FVector CornerPoint = SourceWaypoints[WaypointIndex];
			const FVector NextPoint = SourceWaypoints[WaypointIndex + 1];
			const FVector PrevDir = CornerPoint - PrevPoint;
			const FVector NextDir = NextPoint - CornerPoint;
			const double PrevLength = PrevDir.Length();
			const double NextLength = NextDir.Length();
			const FVector PrevDirSafe = PrevDir.GetSafeNormal();
			const FVector NextDirSafe = NextDir.GetSafeNormal();
			const float CornerDot = FVector::DotProduct(PrevDirSafe, NextDirSafe);
			const double MaxTrimByLength = FMath::Min(PrevLength, NextLength) * CornerSmoothingSegmentFraction;
			const double DesiredTrim = FMath::Max(0.0, static_cast<double>(CornerSmoothingDistance > 0.0f ? CornerSmoothingDistance : AgentRadius * 1.5f));
			const double CornerTrim = FMath::Min(DesiredTrim, MaxTrimByLength);

			bool bSmoothedCorner = false;
			if (PrevLength > UE_KINDA_SMALL_NUMBER &&
				NextLength > UE_KINDA_SMALL_NUMBER &&
				CornerTrim > UE_KINDA_SMALL_NUMBER &&
				CornerDot <= CornerSmoothingMaxDot)
			{
				const FVector EntryPoint = CornerPoint - PrevDirSafe * CornerTrim;
				const FVector ExitPoint = CornerPoint + NextDirSafe * CornerTrim;
				FollowSegment.EndWaypointIndex = AppendWaypointIfNeeded(FollowPathWaypoints, EntryPoint);

				for (int32 SubdivisionIndex = 1; SubdivisionIndex <= CornerSmoothingSubdivisions; ++SubdivisionIndex)
				{
					const float T = static_cast<float>(SubdivisionIndex) / static_cast<float>(CornerSmoothingSubdivisions + 1);
					const FVector CurvePoint = EvaluateQuadraticBezier(EntryPoint, CornerPoint, ExitPoint, T);
					if (!CurvePoint.Equals(EntryPoint, UE_KINDA_SMALL_NUMBER) &&
						!CurvePoint.Equals(ExitPoint, UE_KINDA_SMALL_NUMBER))
					{
						FollowSegment.EndWaypointIndex = AppendWaypointIfNeeded(FollowPathWaypoints, CurvePoint);
					}
				}

				FollowSegment.EndWaypointIndex = AppendWaypointIfNeeded(FollowPathWaypoints, ExitPoint);
				bSmoothedCorner = true;
				++SmoothedCornerCount;
			}

			if (!bSmoothedCorner)
			{
				FollowSegment.EndWaypointIndex = AppendWaypointIfNeeded(FollowPathWaypoints, CornerPoint);
			}
		}

		FollowSegment.EndWaypointIndex = AppendWaypointIfNeeded(FollowPathWaypoints, SourceWaypoints[SourceSegment.EndWaypointIndex]);
		FollowPathSegments.Add(FollowSegment);

		if (bLogPathRequests && SmoothedCornerCount > 0)
		{
			UE_LOG(
				LogMobileSurfaceNavAgent,
				Log,
				TEXT("%s smoothed walk segment: sourceWp=%d followWp=%d corners=%d"),
				*GetNameSafe(GetOwner()),
				SourceSegment.EndWaypointIndex - SourceSegment.StartWaypointIndex + 1,
				FollowSegment.EndWaypointIndex - FollowSegment.StartWaypointIndex + 1,
				SmoothedCornerCount);
		}
	}

	if (FollowPathSegments.IsEmpty())
	{
		FollowPathWaypoints = SourceWaypoints;
		FollowPathSegments = SourceSegments;
	}

	for (int32 WaypointIndex = 0; WaypointIndex + 1 < FollowPathWaypoints.Num(); ++WaypointIndex)
	{
		FollowPathEstimatedLength += FVector::Distance(FollowPathWaypoints[WaypointIndex], FollowPathWaypoints[WaypointIndex + 1]);
	}
}

void UMobileSurfaceNavAgentComponent::UpdateFacing(const float DeltaTime)
{
	if (!bEnableSmoothRotation || MaxTurnRateDegreesPerSecond <= 0.0f)
	{
		return;
	}

	AActor* Owner = GetOwner();
	const USceneComponent* SpaceComponent = GetNavigationSpaceComponent();
	if (!Owner || !SpaceComponent)
	{
		return;
	}

	const TArray<FVector>& ActiveWaypoints = GetActivePathWaypoints();
	if (!ActiveWaypoints.IsValidIndex(CurrentWaypointIndex))
	{
		bHasLastFacingTargetWorld = false;
		return;
	}

	const FVector CurrentLocal = bHasCachedNavigationLocalPosition
		? CachedNavigationLocalPosition
		: SpaceComponent->GetComponentTransform().InverseTransformPosition(Owner->GetActorLocation());
	FVector DesiredTargetLocal = ActiveWaypoints[CurrentWaypointIndex];
	double RemainingLookAhead = RotationLookAheadDistance;
	FVector SegmentStartLocal = CurrentLocal;
	for (int32 LookAheadIndex = CurrentWaypointIndex; LookAheadIndex < ActiveWaypoints.Num(); ++LookAheadIndex)
	{
		const FVector SegmentEndLocal = ActiveWaypoints[LookAheadIndex];
		const FVector Segment = SegmentEndLocal - SegmentStartLocal;
		const double SegmentLength = Segment.Length();
		if (SegmentLength > UE_KINDA_SMALL_NUMBER)
		{
			if (RemainingLookAhead <= SegmentLength)
			{
				DesiredTargetLocal = SegmentStartLocal + Segment.GetSafeNormal() * RemainingLookAhead;
				break;
			}

			RemainingLookAhead -= SegmentLength;
			DesiredTargetLocal = SegmentEndLocal;
		}

		SegmentStartLocal = SegmentEndLocal;
	}

	const FVector DesiredLocalDirection = DesiredTargetLocal - CurrentLocal;
	if (DesiredLocalDirection.SizeSquared() < FMath::Square(RotationMinDirectionDistance))
	{
		return;
	}

	const FVector DesiredWorldTarget = SpaceComponent->GetComponentTransform().TransformPosition(DesiredTargetLocal);
	FVector DesiredWorldDirection = DesiredWorldTarget - Owner->GetActorLocation();
	if (bHasLastFacingTargetWorld)
	{
		const FVector SmoothedTargetWorld = FMath::VInterpTo(LastFacingTargetWorld, DesiredWorldTarget, DeltaTime, 10.0f);
		LastFacingTargetWorld = SmoothedTargetWorld;
		DesiredWorldDirection = SmoothedTargetWorld - Owner->GetActorLocation();
	}
	else
	{
		LastFacingTargetWorld = DesiredWorldTarget;
		bHasLastFacingTargetWorld = true;
	}

	if (DesiredWorldDirection.IsNearlyZero())
	{
		return;
	}

	const float DesiredYaw = DesiredWorldDirection.Rotation().Yaw;
	const FRotator CurrentRotation = Owner->GetActorRotation();
	const float MaxYawStep = MaxTurnRateDegreesPerSecond * DeltaTime;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, DesiredYaw);
	const float NewYaw = CurrentRotation.Yaw + FMath::Clamp(DeltaYaw, -MaxYawStep, MaxYawStep);
	Owner->SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
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
	RebuildFollowPath();
	ClearPendingPathRequest();
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = GetActivePathWaypoints().Num() > 1 ? 1 : 0;
	AgentState = CurrentPath.Segments.IsEmpty() ? EMobileSurfaceNavAgentState::Moving : EMobileSurfaceNavAgentState::Moving;
	LastProgressWaypointIndex = CurrentWaypointIndex;
	LastProgressWorldPosition = Owner->GetActorLocation();
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	CachedNavigationLocalPosition = CurrentPath.Waypoints.IsValidIndex(0)
		? CurrentPath.Waypoints[0]
		: CurrentLocal;
	bHasCachedNavigationLocalPosition = true;
	bHasLastFacingTargetWorld = false;
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
	RebuildFollowPath();
	bPendingPathPreservesCurrentPath = false;
	ObservedRuntimeStateRevision = CurrentPath.RuntimeStateRevision;
	bWaitingForNavigationChange = false;
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = GetActivePathWaypoints().Num() > 1 ? 1 : 0;
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
		bHasLastFacingTargetWorld = false;
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
	FollowPathWaypoints.Reset();
	FollowPathSegments.Reset();
	FollowPathEstimatedLength = 0.0f;
	CurrentSegmentIndex = 0;
	CurrentWaypointIndex = 0;
	LastProgressWaypointIndex = INDEX_NONE;
	SameWaypointStuckChecks = 0;
	StuckCheckTimer = 0.0f;
	AgentState = EMobileSurfaceNavAgentState::Idle;
	bHasCachedNavigationLocalPosition = false;
	bHasLastFacingTargetWorld = false;
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
			DrawDebugLine(World, Start, End, FColor::Red, false, Duration, DepthPriority, 4.0f);
			DrawDebugPoint(World, Start, 14.0f, WaypointIndex == 0 ? FColor::Green : FColor::Red, false, Duration, DepthPriority);
			if (bDrawCurrentPathTriangleLabels)
			{
				DrawDebugString(World, Start + FVector(0.0, 0.0, 24.0f), FString::Printf(TEXT("W%d"), WaypointIndex), nullptr, FColor::Red, Duration, false, 1.0f);
			}
		}
		DrawDebugPoint(World, LocalToWorld.TransformPosition(CurrentPath.Waypoints.Last()), 14.0f, FColor::Red, false, Duration, DepthPriority);
	}

	if (bDrawSmoothedPathDebug && FollowPathWaypoints.Num() >= 2)
	{
		for (int32 WaypointIndex = 0; WaypointIndex + 1 < FollowPathWaypoints.Num(); ++WaypointIndex)
		{
			const FVector Start = LocalToWorld.TransformPosition(FollowPathWaypoints[WaypointIndex]);
			const FVector End = LocalToWorld.TransformPosition(FollowPathWaypoints[WaypointIndex + 1]);
			DrawDebugLine(World, Start, End, FColor(0, 255, 180), false, Duration, DepthPriority, 7.0f);
			DrawDebugPoint(World, Start, 10.0f, WaypointIndex == 0 ? FColor(80, 255, 120) : FColor(0, 255, 180), false, Duration, DepthPriority);
		}
		DrawDebugPoint(World, LocalToWorld.TransformPosition(FollowPathWaypoints.Last()), 10.0f, FColor(0, 255, 180), false, Duration, DepthPriority);
	}

	if (bDrawFacingDebug && GetOwner())
	{
		const FVector Start = GetOwner()->GetActorLocation();
		const FVector Forward = GetOwner()->GetActorForwardVector();
		DrawDebugDirectionalArrow(World, Start, Start + Forward * FacingDebugLength, 18.0f, FColor::Yellow, false, Duration, DepthPriority, 3.0f);
		if (bHasLastFacingTargetWorld)
		{
			DrawDebugLine(
				World,
				Start,
				LastFacingTargetWorld,
				FColor(255, 220, 80),
				false,
				Duration,
				DepthPriority,
				2.0f);
			DrawDebugPoint(World, LastFacingTargetWorld, 10.0f, FColor(255, 220, 80), false, Duration, DepthPriority);
		}
	}

	if (GetOwner())
	{
		DrawDebugString(
			World,
			GetOwner()->GetActorLocation() + FVector(0.0, 0.0, AgentRadius + 40.0f),
			FString::Printf(TEXT("%s Seg=%d Wp=%d Len=%.1f"), LexAgentStateText(AgentState), CurrentSegmentIndex, CurrentWaypointIndex, FollowPathEstimatedLength > 0.0f ? FollowPathEstimatedLength : CurrentPath.EstimatedLength),
			nullptr,
			FColor::White,
			Duration,
			false,
			1.0f);
	}
}

void UMobileSurfaceNavAgentComponent::AdvanceBeyondCurrentSegmentOrComplete()
{
	const TArray<FMobileSurfaceNavPathSegment>& ActiveSegments = GetActivePathSegments();
	if (ActiveSegments.IsValidIndex(CurrentSegmentIndex))
	{
		const FMobileSurfaceNavPathSegment& NextSegment = ActiveSegments[CurrentSegmentIndex];
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

	const TArray<FVector>& ActiveWaypoints = GetActivePathWaypoints();
	const TArray<FMobileSurfaceNavPathSegment>& ActiveSegments = GetActivePathSegments();
	if (ActiveSegments.IsValidIndex(CurrentSegmentIndex))
	{
		const FMobileSurfaceNavPathSegment& CurrentSegment = ActiveSegments[CurrentSegmentIndex];
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

	if (CurrentWaypointIndex >= ActiveWaypoints.Num() || !ActiveSegments.IsValidIndex(CurrentSegmentIndex))
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
	const FVector TargetLocal = ActiveWaypoints[CurrentWaypointIndex];
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
		if (ActiveSegments.IsValidIndex(CurrentSegmentIndex) &&
			CurrentWaypointIndex > ActiveSegments[CurrentSegmentIndex].EndWaypointIndex)
		{
			++CurrentSegmentIndex;
			if (!ActiveSegments.IsValidIndex(CurrentSegmentIndex))
			{
				HandleMoveCompleted();
				return;
			}
		}
		if (CurrentWaypointIndex >= ActiveWaypoints.Num())
		{
			HandleMoveCompleted();
		}
		UpdateFacing(DeltaTime);
		return;
	}

	const FVector Step = ToTarget.GetSafeNormal() * MaxTravelThisTick;
	CachedNavigationLocalPosition = CurrentLocal + Step.GetClampedToMaxSize(DistanceToTarget);
	bHasCachedNavigationLocalPosition = true;
	SyncOwnerToCachedNavigationLocalPosition();
	UpdateFacing(DeltaTime);

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
