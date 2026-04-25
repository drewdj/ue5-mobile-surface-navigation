#include "MobileSurfaceNavComponent.h"

#include "MobileSurfaceNavAgent.h"
#include "MobileSurfaceNavAgentComponent.h"

#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

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
