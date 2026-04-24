#include "MovingPlatformTestActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMovingPlatformTestActor::AMovingPlatformTestActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(SceneRoot);
	PreviewMesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		PreviewMesh->SetStaticMesh(CubeMeshFinder.Object);
		PreviewMesh->SetRelativeScale3D(FVector(4.0f, 4.0f, 0.5f));
	}
}

void AMovingPlatformTestActor::BeginPlay()
{
	Super::BeginPlay();

	InitialLocation = GetActorLocation();
	InitialRotation = GetActorRotation();
	CurrentTargetLocation = InitialLocation;
	CurrentTargetRotation = InitialRotation;
	RandomStream.Initialize(RandomSeed);

	PickNewTargets();
}

void AMovingPlatformTestActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMovePlatform)
	{
		const FVector NewLocation = FMath::VInterpConstantTo(
			GetActorLocation(),
			CurrentTargetLocation,
			DeltaSeconds,
			MoveSpeed);
		SetActorLocation(NewLocation);
	}

	if (bRotatePlatform)
	{
		const FRotator NewRotation = FMath::RInterpConstantTo(
			GetActorRotation(),
			CurrentTargetRotation,
			DeltaSeconds,
			RotationSpeedDegrees);
		SetActorRotation(NewRotation);
	}

	if (!bUseTimerRetarget && bRetargetOnArrival && HasReachedLocationTarget() && HasReachedRotationTarget())
	{
		PickNewTargets();
	}
}

void AMovingPlatformTestActor::PickNewTargets()
{
	CurrentTargetLocation = bMovePlatform ? BuildRandomTargetLocation() : GetActorLocation();
	CurrentTargetRotation = bRotatePlatform ? BuildRandomTargetRotation() : GetActorRotation();
	ScheduleNextRetarget();
}

FVector AMovingPlatformTestActor::BuildRandomTargetLocation()
{
	const float EffectiveMaxOffsetX = MaxMoveOffsetX > 0.0f ? MaxMoveOffsetX : MoveRadius;
	const float EffectiveMaxOffsetY = MaxMoveOffsetY > 0.0f ? MaxMoveOffsetY : MoveRadius;
	const float EffectiveMaxOffsetZ = MaxMoveOffsetZ > 0.0f ? MaxMoveOffsetZ : MoveRadius;

	FVector TargetLocation = InitialLocation;
	TargetLocation.X += bMoveAlongX ? RandomStream.FRandRange(-EffectiveMaxOffsetX, EffectiveMaxOffsetX) : 0.0f;
	TargetLocation.Y += bMoveAlongY ? RandomStream.FRandRange(-EffectiveMaxOffsetY, EffectiveMaxOffsetY) : 0.0f;

	if (bKeepInitialZ || !bMoveAlongZ)
	{
		TargetLocation.Z = InitialLocation.Z;
	}
	else
	{
		TargetLocation.Z += RandomStream.FRandRange(-EffectiveMaxOffsetZ, EffectiveMaxOffsetZ);
	}

	return TargetLocation;
}

FRotator AMovingPlatformTestActor::BuildRandomTargetRotation()
{
	return FRotator(
		bRotatePitch ? InitialRotation.Pitch + RandomStream.FRandRange(-MaxRandomPitch, MaxRandomPitch) : InitialRotation.Pitch,
		bRotateYaw ? InitialRotation.Yaw + RandomStream.FRandRange(-MaxRandomYawOffset, MaxRandomYawOffset) : InitialRotation.Yaw,
		bRotateRoll ? InitialRotation.Roll + RandomStream.FRandRange(-MaxRandomRoll, MaxRandomRoll) : InitialRotation.Roll);
}

void AMovingPlatformTestActor::ScheduleNextRetarget()
{
	if (!bUseTimerRetarget)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float MinInterval = FMath::Min(RetargetIntervalMin, RetargetIntervalMax);
		const float MaxInterval = FMath::Max(RetargetIntervalMin, RetargetIntervalMax);
		const float Delay = MaxInterval > 0.0f ? RandomStream.FRandRange(MinInterval, MaxInterval) : 0.0f;
		World->GetTimerManager().SetTimer(
			RetargetTimerHandle,
			this,
			&AMovingPlatformTestActor::PickNewTargets,
			Delay,
			false);
	}
}

bool AMovingPlatformTestActor::HasReachedLocationTarget() const
{
	return !bMovePlatform ||
		FVector::DistSquared(GetActorLocation(), CurrentTargetLocation) <= FMath::Square(LocationArrivalTolerance);
}

bool AMovingPlatformTestActor::HasReachedRotationTarget() const
{
	if (!bRotatePlatform)
	{
		return true;
	}

	return GetActorRotation().Equals(CurrentTargetRotation, 1.0f);
}
