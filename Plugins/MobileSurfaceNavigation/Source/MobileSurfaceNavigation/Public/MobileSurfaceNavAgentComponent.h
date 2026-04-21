#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MobileSurfaceNavSubsystem.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavAgentComponent.generated.h"

class UMobileSurfaceNavComponent;

UCLASS(ClassGroup = (Navigation), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class MOBILESURFACENAVIGATION_API UMobileSurfaceNavAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMobileSurfaceNavAgentComponent();

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void InitializeAgent(UMobileSurfaceNavComponent* InNavigationComponent, float InAgentRadius, float InMoveSpeed, int32 InRandomSeed);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool RequestMoveToLocal(const FVector& TargetLocalPosition);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool RequestRandomPath();

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	float GetAgentRadius() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool PickRandomNavigablePoint(FVector& OutLocalPoint);
	bool RequestPathImmediate(const FVector& TargetLocalPosition);
	bool PollPendingPathRequest();
	void ClearPendingPathRequest();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMobileSurfaceNavComponent> NavigationComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AgentRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MoveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AcceptanceRadius = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float RepathDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	bool bRequestRandomPathOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	bool bUseQueuedPathRequests = true;

	UPROPERTY(Transient)
	FMobileSurfaceNavPath CurrentPath;

	UPROPERTY(Transient)
	FMobileSurfacePathRequestHandle PendingPathRequest;

	UPROPERTY(Transient)
	FVector PendingTargetLocalPosition = FVector::ZeroVector;

	UPROPERTY(Transient)
	int32 CurrentWaypointIndex = 0;

	UPROPERTY(Transient)
	float RepathTimer = 0.0f;

	UPROPERTY(Transient)
	FRandomStream RandomStream;
};
