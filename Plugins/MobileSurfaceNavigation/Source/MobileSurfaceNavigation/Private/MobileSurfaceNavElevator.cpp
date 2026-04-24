#include "MobileSurfaceNavElevator.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileSurfaceNavElevator, Log, All);

AMobileSurfaceNavElevator::AMobileSurfaceNavElevator()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PlatformRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PlatformRoot"));
	PlatformRoot->SetupAttachment(SceneRoot);

	PlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlatformMesh"));
	PlatformMesh->SetupAttachment(PlatformRoot);
	PlatformMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DebugLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugLabel"));
	DebugLabel->SetupAttachment(PlatformRoot);
	DebugLabel->SetHorizontalAlignment(EHTA_Center);
	DebugLabel->SetVerticalAlignment(EVRTA_TextCenter);
	DebugLabel->SetWorldSize(DebugLabelWorldSize);
	DebugLabel->SetHiddenInGame(false);
	DebugLabel->SetTextRenderColor(FColor::White);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		PlatformMesh->SetStaticMesh(CubeMeshFinder.Object);
		PlatformMesh->SetRelativeScale3D(FVector(1.5f, 1.5f, 0.2f));
	}
}

void AMobileSurfaceNavElevator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (PlatformRoot)
	{
		HomeWorldLocation = PlatformRoot->GetComponentLocation();
		CurrentServiceWorldLocation = HomeWorldLocation;
		TargetServiceWorldLocation = HomeWorldLocation;
	}

	UpdateDebugLabel();
}

void AMobileSurfaceNavElevator::SetHomeWorldLocation(const FVector& WorldLocation, const bool bSnapImmediately)
{
	HomeWorldLocation = WorldLocation;

	if (bSnapImmediately && PlatformRoot && QueuedRequests.IsEmpty() && !HasActiveRiders())
	{
		PlatformRoot->SetWorldLocation(WorldLocation);
		CurrentServiceWorldLocation = WorldLocation;
		TargetServiceWorldLocation = WorldLocation;
		ElevatorState = EMobileSurfaceNavElevatorState::Idle;
		BoardingTimer = 0.0f;
		IdleTimer = 0.0f;
		bBoardedPassengerThisStop = false;
		bDisembarkedPassengerThisStop = false;
		bStopRequiresBoardingWindow = false;
		ActiveTravelRouteWorldLocations.Reset();
		ActiveTravelRouteTargetIndex = INDEX_NONE;
	}

	UpdateDebugLabel();
}

void AMobileSurfaceNavElevator::ConfigureServiceLocations(
	const TArray<FVector>& InServiceWorldLocations,
	const EMobileSurfaceNavLinkTraversalMode InTraversalMode,
	const bool bSnapToHome)
{
	ServiceWorldLocations = InServiceWorldLocations;
	TraversalMode = InTraversalMode;

	if (ServiceWorldLocations.Num() > 0)
	{
		SetHomeWorldLocation(ServiceWorldLocations[0], bSnapToHome);
	}
}

void AMobileSurfaceNavElevator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CleanupInvalidEntries();

	switch (ElevatorState)
	{
	case EMobileSurfaceNavElevatorState::Moving:
		UpdateMoving(DeltaSeconds);
		break;

	case EMobileSurfaceNavElevatorState::Boarding:
		ProcessBoarding();

		if (bStopRequiresBoardingWindow || bBoardedPassengerThisStop || bDisembarkedPassengerThisStop)
		{
			BoardingTimer += DeltaSeconds;
		}
		else
		{
			BoardingTimer = 0.0f;
		}

		if ((bStopRequiresBoardingWindow || bBoardedPassengerThisStop || bDisembarkedPassengerThisStop) && BoardingTimer >= BoardingWaitTime)
		{
			AdvanceAfterBoardingStop();
		}
		else if (!bStopRequiresBoardingWindow && !bBoardedPassengerThisStop && !bDisembarkedPassengerThisStop)
		{
			AdvanceAfterBoardingStop();
		}
		break;

	case EMobileSurfaceNavElevatorState::Idle:
	default:
		if (QueuedRequests.Num() > 0)
		{
			if (HasQueuedRequestsAtLocation(CurrentServiceWorldLocation))
			{
				BeginBoardingAtLocation(CurrentServiceWorldLocation, TraversalMode == EMobileSurfaceNavLinkTraversalMode::Sequential);
			}
			else
			{
				BeginMoveToLocation(QueuedRequests[0].EntryWorldLocation);
			}
		}
		else
		{
			ReturnHomeIfNeeded(DeltaSeconds);
		}
		break;
	}

	UpdateDebugLabel();
}

bool AMobileSurfaceNavElevator::RequestBoarding(AActor* Agent, const FVector& EntryWorldLocation, const FVector& ExitWorldLocation)
{
	TArray<FVector> RouteWorldLocations;
	RouteWorldLocations.Add(EntryWorldLocation);
	RouteWorldLocations.Add(ExitWorldLocation);
	return RequestBoardingWithRoute(Agent, RouteWorldLocations);
}

bool AMobileSurfaceNavElevator::RequestBoardingWithRoute(AActor* Agent, const TArray<FVector>& RouteWorldLocations)
{
	if (!Agent)
	{
		UE_LOG(LogMobileSurfaceNavElevator, Warning, TEXT("%s request rejected: null agent"), *GetName());
		return false;
	}

	if (RouteWorldLocations.Num() < 2)
	{
		return false;
	}

	const FVector EntryWorldLocation = RouteWorldLocations[0];
	const FVector ExitWorldLocation = RouteWorldLocations.Last();
	if (AreLocationsEquivalent(EntryWorldLocation, ExitWorldLocation))
	{
		UE_LOG(LogMobileSurfaceNavElevator, Warning, TEXT("%s request rejected for %s: entry and exit are equivalent"),
			*GetName(),
			*GetNameSafe(Agent));
		return false;
	}

	for (const FMobileSurfaceNavElevatorRequest& Request : QueuedRequests)
	{
		if (Request.Agent.Get() == Agent)
		{
			return true;
		}
	}

	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (Rider.Agent.Get() == Agent)
		{
			return true;
		}
	}

	FMobileSurfaceNavElevatorRequest& Request = QueuedRequests.AddDefaulted_GetRef();
	Request.Agent = Agent;
	Request.EntryWorldLocation = EntryWorldLocation;
	Request.ExitWorldLocation = ExitWorldLocation;
	Request.RouteWorldLocations = RouteWorldLocations;

	UE_LOG(LogMobileSurfaceNavElevator, Log, TEXT("%s queued %s entry=%s exit=%s queued=%d riders=%d state=%d"),
		*GetName(),
		*GetNameSafe(Agent),
		*EntryWorldLocation.ToCompactString(),
		*ExitWorldLocation.ToCompactString(),
		QueuedRequests.Num(),
		Riders.Num(),
		static_cast<int32>(ElevatorState));

	if (ElevatorState == EMobileSurfaceNavElevatorState::Idle &&
		AreLocationsEquivalent(EntryWorldLocation, PlatformRoot->GetComponentLocation()))
	{
		BeginBoardingAtLocation(PlatformRoot->GetComponentLocation(), true);
	}

	return true;
}

void AMobileSurfaceNavElevator::CancelRequest(AActor* Agent)
{
	if (!Agent)
	{
		return;
	}

	QueuedRequests.RemoveAllSwap([Agent](const FMobileSurfaceNavElevatorRequest& Request)
	{
		return Request.Agent.Get() == Agent;
	});
}

bool AMobileSurfaceNavElevator::IsAgentBoarded(AActor* Agent) const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (Rider.Agent.Get() == Agent)
		{
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::IsTraversalComplete(AActor* Agent) const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (Rider.Agent.Get() == Agent)
		{
			return Rider.bTraversalComplete;
		}
	}

	return false;
}

void AMobileSurfaceNavElevator::FinishTraversal(AActor* Agent)
{
	if (!Agent)
	{
		return;
	}

	Riders.RemoveAllSwap([Agent](const FMobileSurfaceNavElevatorRider& Rider)
	{
		return Rider.Agent.Get() == Agent;
	});
}

bool AMobileSurfaceNavElevator::GetAgentRideWorldLocation(AActor* Agent, FVector& OutWorldLocation) const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (Rider.Agent.Get() == Agent)
		{
			OutWorldLocation = PlatformRoot->GetComponentLocation() + GetRiderOffset(Rider.SlotIndex);
			return true;
		}
	}

	return false;
}

FVector AMobileSurfaceNavElevator::GetCurrentPlatformWorldLocation() const
{
	return PlatformRoot ? PlatformRoot->GetComponentLocation() : GetActorLocation();
}

void AMobileSurfaceNavElevator::BeginBoardingAtLocation(const FVector& WorldLocation, const bool bForceBoardingWindow)
{
	CurrentServiceWorldLocation = WorldLocation;
	BoardingTimer = 0.0f;
	bBoardedPassengerThisStop = false;
	bDisembarkedPassengerThisStop = false;
	bStopRequiresBoardingWindow = bForceBoardingWindow;
	ElevatorState = EMobileSurfaceNavElevatorState::Boarding;
	ProcessBoarding();
}

void AMobileSurfaceNavElevator::BeginMoveToLocation(const FVector& WorldLocation)
{
	ActiveTravelRouteWorldLocations = BuildRouteToLocation(WorldLocation);
	ActiveTravelRouteTargetIndex = 0;
	if (!ActiveTravelRouteWorldLocations.IsValidIndex(ActiveTravelRouteTargetIndex))
	{
		ElevatorState = EMobileSurfaceNavElevatorState::Idle;
		return;
	}

	TargetServiceWorldLocation = ActiveTravelRouteWorldLocations[ActiveTravelRouteTargetIndex];
	bBoardedPassengerThisStop = false;
	bStopRequiresBoardingWindow = false;
	ElevatorState = EMobileSurfaceNavElevatorState::Moving;

	UE_LOG(LogMobileSurfaceNavElevator, Log, TEXT("%s moving from=%s to=%s riders=%d queued=%d"),
		*GetName(),
		*GetCurrentPlatformWorldLocation().ToCompactString(),
		*TargetServiceWorldLocation.ToCompactString(),
		Riders.Num(),
		QueuedRequests.Num());
}

bool AMobileSurfaceNavElevator::ResumeActiveTravelRoute()
{
	if (!ActiveTravelRouteWorldLocations.IsValidIndex(ActiveTravelRouteTargetIndex))
	{
		return false;
	}

	TargetServiceWorldLocation = ActiveTravelRouteWorldLocations[ActiveTravelRouteTargetIndex];
	bBoardedPassengerThisStop = false;
	bDisembarkedPassengerThisStop = false;
	bStopRequiresBoardingWindow = false;
	ElevatorState = EMobileSurfaceNavElevatorState::Moving;
	return true;
}

void AMobileSurfaceNavElevator::AdvanceAfterBoardingStop()
{
	if (ResumeActiveTravelRoute())
	{
		return;
	}

	FVector NextExitLocation = FVector::ZeroVector;
	if (GetNextActiveRiderExitLocation(NextExitLocation))
	{
		BeginMoveToLocation(NextExitLocation);
		return;
	}

	if (QueuedRequests.Num() > 0)
	{
		if (HasQueuedRequestsAtLocation(CurrentServiceWorldLocation))
		{
			BeginBoardingAtLocation(CurrentServiceWorldLocation, TraversalMode == EMobileSurfaceNavLinkTraversalMode::Sequential);
		}
		else
		{
			BeginMoveToLocation(QueuedRequests[0].EntryWorldLocation);
		}
		return;
	}

	ElevatorState = EMobileSurfaceNavElevatorState::Idle;
	IdleTimer = 0.0f;
}

void AMobileSurfaceNavElevator::ProcessBoarding()
{
	for (FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (AdvanceRiderRouteIfNeeded(Rider, CurrentServiceWorldLocation))
		{
			Rider.bTraversalComplete = true;
			bDisembarkedPassengerThisStop = true;
		}
	}

	int32 QueueIndex = 0;
	while (QueueIndex < QueuedRequests.Num() && Riders.Num() < Capacity)
	{
		const FMobileSurfaceNavElevatorRequest& Request = QueuedRequests[QueueIndex];
		if (!Request.Agent.IsValid() || !AreLocationsEquivalent(Request.EntryWorldLocation, CurrentServiceWorldLocation))
		{
			++QueueIndex;
			continue;
		}

		FMobileSurfaceNavElevatorRider& Rider = Riders.AddDefaulted_GetRef();
		Rider.Agent = Request.Agent;
		Rider.ExitWorldLocation = Request.ExitWorldLocation;
		Rider.RouteWorldLocations = Request.RouteWorldLocations;
		Rider.CurrentRouteTargetIndex = Rider.RouteWorldLocations.Num() > 0 ? Rider.RouteWorldLocations.Num() - 1 : INDEX_NONE;
		Rider.SlotIndex = AllocateSlotIndex();
		bBoardedPassengerThisStop = true;
		QueuedRequests.RemoveAtSwap(QueueIndex, 1, EAllowShrinking::No);
	}
}

void AMobileSurfaceNavElevator::UpdateMoving(const float DeltaSeconds)
{
	const FVector CurrentLocation = GetCurrentPlatformWorldLocation();
	const FVector Delta = TargetServiceWorldLocation - CurrentLocation;
	const double Distance = Delta.Length();

	if (Distance <= UE_KINDA_SMALL_NUMBER || Distance <= MovementArrivalTolerance)
	{
		PlatformRoot->SetWorldLocation(TargetServiceWorldLocation);
		CurrentServiceWorldLocation = TargetServiceWorldLocation;
		++ActiveTravelRouteTargetIndex;

		if (ActiveTravelRouteWorldLocations.IsValidIndex(ActiveTravelRouteTargetIndex))
		{
			TargetServiceWorldLocation = ActiveTravelRouteWorldLocations[ActiveTravelRouteTargetIndex];
			if (ShouldServiceLocation(CurrentServiceWorldLocation))
			{
				BeginBoardingAtLocation(CurrentServiceWorldLocation, true);
			}
			return;
		}

		ActiveTravelRouteWorldLocations.Reset();
		ActiveTravelRouteTargetIndex = INDEX_NONE;

		if (ShouldServiceLocation(CurrentServiceWorldLocation))
		{
			BeginBoardingAtLocation(CurrentServiceWorldLocation, true);
		}
		else if (HasActiveRiders())
		{
			FVector NextExitLocation = FVector::ZeroVector;
			if (GetNextActiveRiderExitLocation(NextExitLocation))
			{
				BeginMoveToLocation(NextExitLocation);
			}
			else
			{
				ElevatorState = EMobileSurfaceNavElevatorState::Idle;
			}
		}
		else if (QueuedRequests.Num() > 0)
		{
			BeginMoveToLocation(QueuedRequests[0].EntryWorldLocation);
		}
		else
		{
			ElevatorState = EMobileSurfaceNavElevatorState::Idle;
			IdleTimer = 0.0f;
		}
		return;
	}

	const FVector Step = Delta.GetSafeNormal() * MoveSpeed * DeltaSeconds;
	PlatformRoot->SetWorldLocation(CurrentLocation + Step.GetClampedToMaxSize(Distance));
}

void AMobileSurfaceNavElevator::ReturnHomeIfNeeded(const float DeltaSeconds)
{
	if (!bReturnToHomeWhenIdle || AreLocationsEquivalent(GetCurrentPlatformWorldLocation(), HomeWorldLocation))
	{
		return;
	}

	IdleTimer += DeltaSeconds;
	if (IdleTimer >= IdleReturnDelay)
	{
		BeginMoveToLocation(HomeWorldLocation);
	}
}

void AMobileSurfaceNavElevator::CleanupInvalidEntries()
{
	QueuedRequests.RemoveAllSwap([](const FMobileSurfaceNavElevatorRequest& Request)
	{
		return !Request.Agent.IsValid();
	});

	Riders.RemoveAllSwap([](const FMobileSurfaceNavElevatorRider& Rider)
	{
		return !Rider.Agent.IsValid();
	});
}

FVector AMobileSurfaceNavElevator::GetRiderOffset(const int32 SlotIndex) const
{
	static const FVector Offsets[] =
	{
		FVector(0.0f, 0.0f, 40.0f),
		FVector(60.0f, 0.0f, 40.0f),
		FVector(-60.0f, 0.0f, 40.0f),
		FVector(0.0f, 60.0f, 40.0f),
		FVector(0.0f, -60.0f, 40.0f),
		FVector(60.0f, 60.0f, 40.0f),
		FVector(-60.0f, -60.0f, 40.0f)
	};

	return Offsets[SlotIndex % UE_ARRAY_COUNT(Offsets)];
}

int32 AMobileSurfaceNavElevator::AllocateSlotIndex() const
{
	TSet<int32> UsedSlots;
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		UsedSlots.Add(Rider.SlotIndex);
	}

	for (int32 SlotIndex = 0; SlotIndex < Capacity; ++SlotIndex)
	{
		if (!UsedSlots.Contains(SlotIndex))
		{
			return SlotIndex;
		}
	}

	return Riders.Num();
}

bool AMobileSurfaceNavElevator::HasQueuedRequestsAtLocation(const FVector& WorldLocation) const
{
	for (const FMobileSurfaceNavElevatorRequest& Request : QueuedRequests)
	{
		if (AreLocationsEquivalent(Request.EntryWorldLocation, WorldLocation))
		{
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::HasCompletedRiders() const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (Rider.bTraversalComplete)
		{
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::HasActiveRiders() const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (!Rider.bTraversalComplete)
		{
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::GetNextActiveRiderExitLocation(FVector& OutWorldLocation) const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (!Rider.bTraversalComplete && Rider.RouteWorldLocations.IsValidIndex(Rider.CurrentRouteTargetIndex))
		{
			OutWorldLocation = Rider.RouteWorldLocations[Rider.CurrentRouteTargetIndex];
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::AdvanceRiderRouteIfNeeded(FMobileSurfaceNavElevatorRider& Rider, const FVector& CurrentLocation) const
{
	if (Rider.bTraversalComplete)
	{
		return true;
	}

	while (Rider.RouteWorldLocations.IsValidIndex(Rider.CurrentRouteTargetIndex) &&
		AreLocationsEquivalent(Rider.RouteWorldLocations[Rider.CurrentRouteTargetIndex], CurrentLocation))
	{
		++Rider.CurrentRouteTargetIndex;
	}

	return !Rider.RouteWorldLocations.IsValidIndex(Rider.CurrentRouteTargetIndex);
}

bool AMobileSurfaceNavElevator::HasRiderExitingAtLocation(const FVector& WorldLocation) const
{
	for (const FMobileSurfaceNavElevatorRider& Rider : Riders)
	{
		if (!Rider.bTraversalComplete &&
			Rider.RouteWorldLocations.IsValidIndex(Rider.CurrentRouteTargetIndex) &&
			AreLocationsEquivalent(Rider.RouteWorldLocations[Rider.CurrentRouteTargetIndex], WorldLocation))
		{
			return true;
		}
	}

	return false;
}

bool AMobileSurfaceNavElevator::ShouldServiceLocation(const FVector& WorldLocation) const
{
	return HasRiderExitingAtLocation(WorldLocation) || HasQueuedRequestsAtLocation(WorldLocation);
}

bool AMobileSurfaceNavElevator::AreLocationsEquivalent(const FVector& A, const FVector& B) const
{
	return FVector::DistSquared(A, B) <= FMath::Square(ServiceLocationTolerance);
}

int32 AMobileSurfaceNavElevator::FindServiceLocationIndex(const FVector& WorldLocation) const
{
	int32 BestIndex = INDEX_NONE;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < ServiceWorldLocations.Num(); ++Index)
	{
		const double DistanceSquared = FVector::DistSquared(ServiceWorldLocations[Index], WorldLocation);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

TArray<FVector> AMobileSurfaceNavElevator::BuildRouteToLocation(const FVector& TargetWorldLocation) const
{
	TArray<FVector> Route;
	if (TraversalMode != EMobileSurfaceNavLinkTraversalMode::Sequential || ServiceWorldLocations.Num() < 2)
	{
		Route.Add(TargetWorldLocation);
		return Route;
	}

	const int32 FromIndex = FindServiceLocationIndex(GetCurrentPlatformWorldLocation());
	const int32 ToIndex = FindServiceLocationIndex(TargetWorldLocation);
	if (FromIndex == INDEX_NONE || ToIndex == INDEX_NONE || FromIndex == ToIndex)
	{
		Route.Add(TargetWorldLocation);
		return Route;
	}

	const int32 Step = ToIndex > FromIndex ? 1 : -1;
	for (int32 Index = FromIndex + Step; ; Index += Step)
	{
		Route.Add(ServiceWorldLocations[Index]);
		if (Index == ToIndex)
		{
			break;
		}
	}

	return Route;
}

void AMobileSurfaceNavElevator::UpdateDebugLabel()
{
	if (!DebugLabel)
	{
		return;
	}

	DebugLabel->SetVisibility(bShowDebugLabel);
	if (!bShowDebugLabel)
	{
		return;
	}

	DebugLabel->SetRelativeLocation(FVector(0.0f, 0.0f, DebugLabelHeight));
	DebugLabel->SetWorldSize(DebugLabelWorldSize);

	const TCHAR* StateText = TEXT("Idle");
	FColor LabelColor = FColor::Green;
	float RemainingTime = 0.0f;

	switch (ElevatorState)
	{
	case EMobileSurfaceNavElevatorState::Boarding:
		StateText = TEXT("Boarding");
		LabelColor = FColor::Yellow;
		RemainingTime = FMath::Max(0.0f, BoardingWaitTime - BoardingTimer);
		break;

	case EMobileSurfaceNavElevatorState::Moving:
		StateText = TEXT("Moving");
		LabelColor = FColor::Cyan;
		break;

	case EMobileSurfaceNavElevatorState::Idle:
	default:
		StateText = TEXT("Idle");
		LabelColor = FColor::Green;
		RemainingTime = bReturnToHomeWhenIdle && !AreLocationsEquivalent(GetCurrentPlatformWorldLocation(), HomeWorldLocation)
			? FMath::Max(0.0f, IdleReturnDelay - IdleTimer)
			: 0.0f;
		break;
	}

	const FString TimerLine = ElevatorState == EMobileSurfaceNavElevatorState::Boarding
		? FString::Printf(TEXT("Board %.1fs"), RemainingTime)
		: (bReturnToHomeWhenIdle && ElevatorState == EMobileSurfaceNavElevatorState::Idle && !AreLocationsEquivalent(GetCurrentPlatformWorldLocation(), HomeWorldLocation))
			? FString::Printf(TEXT("Return %.1fs"), RemainingTime)
			: FString();

	const FString TargetLine = ElevatorState == EMobileSurfaceNavElevatorState::Moving
		? FString::Printf(TEXT("To %s"), *TargetServiceWorldLocation.ToCompactString())
		: FString::Printf(TEXT("At %s"), *GetCurrentPlatformWorldLocation().ToCompactString());

	const FString CountLine = FString::Printf(TEXT("R %d/%d Q %d"), Riders.Num(), Capacity, QueuedRequests.Num());
	const FString LabelText = TimerLine.IsEmpty()
		? FString::Printf(TEXT("%s\n%s\n%s"), StateText, *TargetLine, *CountLine)
		: FString::Printf(TEXT("%s\n%s\n%s\n%s"), StateText, *TargetLine, *CountLine, *TimerLine);

	DebugLabel->SetText(FText::FromString(LabelText));
	DebugLabel->SetTextRenderColor(LabelColor);

	if (UWorld* World = GetWorld())
	{
		if (APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			const FVector ToCamera = CameraManager->GetCameraLocation() - DebugLabel->GetComponentLocation();
			if (!ToCamera.IsNearlyZero())
			{
				DebugLabel->SetWorldRotation(ToCamera.Rotation());
			}
		}
	}
}
