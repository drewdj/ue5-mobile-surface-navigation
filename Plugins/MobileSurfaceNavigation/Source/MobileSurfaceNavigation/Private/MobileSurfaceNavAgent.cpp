#include "MobileSurfaceNavAgent.h"

#include "MobileSurfaceNavAgentComponent.h"
#include "MobileSurfaceNavComponent.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

static const TCHAR* LexAgentStateLabel(const EMobileSurfaceNavAgentState State)
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

AMobileSurfaceNavAgent::AMobileSurfaceNavAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SceneRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AgentComponent = CreateDefaultSubobject<UMobileSurfaceNavAgentComponent>(TEXT("MobileSurfaceNavAgentComponent"));

	StateLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StateLabel"));
	StateLabel->SetupAttachment(SceneRoot);
	StateLabel->SetHorizontalAlignment(EHTA_Center);
	StateLabel->SetVerticalAlignment(EVRTA_TextCenter);
	StateLabel->SetWorldSize(18.0f);
	StateLabel->SetHiddenInGame(false);
	StateLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshFinder.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMeshFinder.Object);
	}
}

void AMobileSurfaceNavAgent::InitializeAgent(
	UMobileSurfaceNavComponent* InNavigationComponent,
	const float InAgentRadius,
	const float InMoveSpeed,
	const int32 InRandomSeed)
{
	AgentRadius = InAgentRadius;
	MoveSpeed = InMoveSpeed;
	if (AgentComponent)
	{
		AgentComponent->InitializeAgent(InNavigationComponent, InAgentRadius, InMoveSpeed, InRandomSeed);
		AgentComponent->RequestRandomPath();
	}
	ApplyAgentVisualScale();
	UpdateStateLabel();
}

bool AMobileSurfaceNavAgent::RequestMoveToWorld(const FVector& TargetWorldPosition)
{
	return AgentComponent ? AgentComponent->RequestMoveToWorld(TargetWorldPosition) : false;
}

bool AMobileSurfaceNavAgent::RequestMoveToLocal(const FVector& TargetLocalPosition)
{
	return AgentComponent ? AgentComponent->RequestMoveToLocal(TargetLocalPosition) : false;
}

void AMobileSurfaceNavAgent::StopMovement()
{
	if (AgentComponent)
	{
		AgentComponent->StopMovement();
	}
}

UMobileSurfaceNavComponent* AMobileSurfaceNavAgent::GetNavigationComponent() const
{
	return AgentComponent ? AgentComponent->GetNavigationComponent() : nullptr;
}

UMobileSurfaceNavAgentComponent* AMobileSurfaceNavAgent::GetNavigationAgentComponent() const
{
	return AgentComponent;
}

void AMobileSurfaceNavAgent::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAgentVisualScale();
	UpdateStateLabel();
}

void AMobileSurfaceNavAgent::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateStateLabel();
}

void AMobileSurfaceNavAgent::ApplyAgentVisualScale()
{
	if (!VisualMesh)
	{
		return;
	}

	const float Diameter = FMath::Max(AgentRadius * 2.0f, 1.0f);
	const float SphereMeshDiameter = 100.0f;
	VisualMesh->SetWorldScale3D(FVector(Diameter / SphereMeshDiameter));
}

void AMobileSurfaceNavAgent::UpdateStateLabel()
{
	if (!StateLabel)
	{
		return;
	}

	StateLabel->SetVisibility(bShowStateLabel);
	if (!bShowStateLabel)
	{
		return;
	}

	const EMobileSurfaceNavAgentState State = AgentComponent ? AgentComponent->GetAgentState() : EMobileSurfaceNavAgentState::Idle;
	StateLabel->SetText(FText::FromString(LexAgentStateLabel(State)));
	StateLabel->SetRelativeLocation(FVector(0.0f, 0.0f, StateLabelHeight));

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0))
	{
		const FVector ToCamera = CameraManager->GetCameraLocation() - StateLabel->GetComponentLocation();
		if (!ToCamera.IsNearlyZero())
		{
			StateLabel->SetWorldRotation(ToCamera.Rotation());
		}
	}
}
