#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MovingPlatformTestActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class PATHFINDINGTEST_API AMovingPlatformTestActor : public AActor
{
	GENERATED_BODY()

public:
	AMovingPlatformTestActor();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Moving Platform Test")
	void PickNewTargets();

protected:
	virtual void BeginPlay() override;

private:
	FVector BuildRandomTargetLocation();
	FRotator BuildRandomTargetRotation();
	void ScheduleNextRetarget();
	bool HasReachedLocationTarget() const;
	bool HasReachedRotationTarget() const;

	UPROPERTY(VisibleAnywhere, Category = "Moving Platform Test")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Moving Platform Test")
	TObjectPtr<UStaticMeshComponent> PreviewMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test")
	bool bMovePlatform = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test")
	bool bRotatePlatform = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test")
	bool bUseTimerRetarget = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test")
	bool bRetargetOnArrival = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test")
	bool bKeepInitialZ = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement")
	bool bMoveAlongX = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement")
	bool bMoveAlongY = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement")
	bool bMoveAlongZ = false;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float MoveRadius = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float MaxMoveOffsetX = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float MaxMoveOffsetY = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float MaxMoveOffsetZ = 150.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float MoveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Movement", meta = (ClampMin = "0.0"))
	float LocationArrivalTolerance = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation", meta = (ClampMin = "0.0"))
	float RotationSpeedDegrees = 45.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation")
	bool bRotatePitch = false;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation")
	bool bRotateYaw = true;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation")
	bool bRotateRoll = false;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxRandomPitch = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxRandomRoll = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Rotation", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float MaxRandomYawOffset = 180.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Timing", meta = (ClampMin = "0.0"))
	float RetargetIntervalMin = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Timing", meta = (ClampMin = "0.0"))
	float RetargetIntervalMax = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Moving Platform Test|Random")
	int32 RandomSeed = 1337;

	UPROPERTY(Transient)
	FVector InitialLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator InitialRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	FVector CurrentTargetLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator CurrentTargetRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	FRandomStream RandomStream;

	FTimerHandle RetargetTimerHandle;
};
