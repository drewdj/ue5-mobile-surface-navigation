#include "MobileSurfaceNavAgentComponent.h"

#include "MobileSurfaceNavComponent.h"
#include "MobileSurfaceNavSubsystem.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

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

bool UMobileSurfaceNavAgentComponent::RequestMoveToLocal(const FVector& TargetLocalPosition)
{
	if (!NavigationComponent || !NavigationComponent->HasValidNavigationData())
	{
		CurrentPath = FMobileSurfaceNavPath();
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
	FMobileSurfacePathQueryParams Params;
	Params.AgentRadius = AgentRadius;

	if (bUseQueuedPathRequests)
	{
		if (UWorld* World = GetWorld())
		{
			if (UMobileSurfaceNavSubsystem* Subsystem = World->GetSubsystem<UMobileSurfaceNavSubsystem>())
			{
				ClearPendingPathRequest();
				PendingPathRequest = Subsystem->QueuePathRequest(NavigationComponent, CurrentLocal, TargetLocalPosition, Params);
				PendingTargetLocalPosition = TargetLocalPosition;
				CurrentPath = FMobileSurfaceNavPath();
				CurrentWaypointIndex = 0;
				return PendingPathRequest.IsValid();
			}
		}
	}

	return RequestPathImmediate(TargetLocalPosition);
}

bool UMobileSurfaceNavAgentComponent::RequestPathImmediate(const FVector& TargetLocalPosition)
{
	if (!NavigationComponent || !NavigationComponent->HasValidNavigationData())
	{
		CurrentPath = FMobileSurfaceNavPath();
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
	const bool bPathFound = NavigationComponent->FindPathLocal(CurrentLocal, TargetLocalPosition, CurrentPath, AgentRadius);
	if (!bPathFound)
	{
		CurrentPath = FMobileSurfaceNavPath();
		return false;
	}

	ClearPendingPathRequest();
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
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
		return RequestPathImmediate(Target);
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
		CurrentPath = FMobileSurfaceNavPath();
		return false;
	}

	CurrentPath = Path;
	CurrentWaypointIndex = CurrentPath.Waypoints.Num() > 1 ? 1 : 0;
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
		if (PickRandomNavigablePoint(TargetLocal) && RequestMoveToLocal(TargetLocal))
		{
			return true;
		}
	}

	CurrentPath = FMobileSurfaceNavPath();
	return false;
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
		RequestRandomPath();
	}
}

void UMobileSurfaceNavAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPendingPathRequest();
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
		return;
	}

	if (!NavigationComponent || !CurrentPath.bIsValid || CurrentPath.Waypoints.Num() == 0)
	{
		RepathTimer -= DeltaTime;
		if (RepathTimer <= 0.0f)
		{
			RepathTimer = RepathDelay;
			RequestRandomPath();
		}
		return;
	}

	if (CurrentWaypointIndex >= CurrentPath.Waypoints.Num())
	{
		RequestRandomPath();
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
		if (CurrentWaypointIndex >= CurrentPath.Waypoints.Num())
		{
			RequestRandomPath();
		}
		return;
	}

	const FVector Step = ToTarget.GetSafeNormal() * MoveSpeed * DeltaTime;
	Owner->SetActorLocation(CurrentWorld + Step.GetClampedToMaxSize(DistanceToTarget));
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
