#include "MobileSurfaceNavAgent.h"

#include "MobileSurfaceNavAgentComponent.h"
#include "MobileSurfaceNavComponent.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AMobileSurfaceNavAgent::AMobileSurfaceNavAgent()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SceneRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AgentComponent = CreateDefaultSubobject<UMobileSurfaceNavAgentComponent>(TEXT("MobileSurfaceNavAgentComponent"));

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

void AMobileSurfaceNavAgent::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAgentVisualScale();
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
