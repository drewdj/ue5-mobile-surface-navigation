#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavElevator.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;

USTRUCT()
struct FMobileSurfaceNavElevatorRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Agent;

	UPROPERTY()
	FVector EntryWorldLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector ExitWorldLocation = FVector::ZeroVector;

	UPROPERTY()
	TArray<FVector> RouteWorldLocations;
};

USTRUCT()
struct FMobileSurfaceNavElevatorRider
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Agent;

	UPROPERTY()
	FVector ExitWorldLocation = FVector::ZeroVector;

	UPROPERTY()
	TArray<FVector> RouteWorldLocations;

	UPROPERTY()
	int32 CurrentRouteTargetIndex = INDEX_NONE;

	UPROPERTY()
	int32 SlotIndex = 0;

	UPROPERTY()
	bool bTraversalComplete = false;
};

UENUM()
enum class EMobileSurfaceNavElevatorState : uint8
{
	Idle = 0,
	Boarding = 1,
	Moving = 2
};

UCLASS(BlueprintType, Blueprintable)
class MOBILESURFACENAVIGATION_API AMobileSurfaceNavElevator : public AActor
{
	GENERATED_BODY()

public:
	AMobileSurfaceNavElevator();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	bool RequestBoarding(AActor* Agent, const FVector& EntryWorldLocation, const FVector& ExitWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	bool RequestBoardingWithRoute(AActor* Agent, const TArray<FVector>& RouteWorldLocations);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	void CancelRequest(AActor* Agent);

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Elevator")
	bool IsAgentBoarded(AActor* Agent) const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Elevator")
	bool IsTraversalComplete(AActor* Agent) const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	void FinishTraversal(AActor* Agent);

	bool GetAgentRideWorldLocation(AActor* Agent, FVector& OutWorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Elevator")
	FVector GetCurrentPlatformWorldLocation() const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	void SetHomeWorldLocation(const FVector& WorldLocation, bool bSnapImmediately = true);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Elevator")
	void ConfigureServiceLocations(const TArray<FVector>& InServiceWorldLocations, EMobileSurfaceNavLinkTraversalMode InTraversalMode, bool bSnapToHome = true);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void UpdateDebugLabel();
	void BeginBoardingAtLocation(const FVector& WorldLocation, bool bForceBoardingWindow = false);
	void BeginMoveToLocation(const FVector& WorldLocation);
	bool ResumeActiveTravelRoute();
	void AdvanceAfterBoardingStop();
	void ProcessBoarding();
	void UpdateMoving(float DeltaSeconds);
	void ReturnHomeIfNeeded(float DeltaSeconds);
	void CleanupInvalidEntries();
	FVector GetRiderOffset(int32 SlotIndex) const;
	int32 AllocateSlotIndex() const;
	bool HasQueuedRequestsAtLocation(const FVector& WorldLocation) const;
	bool HasCompletedRiders() const;
	bool HasActiveRiders() const;
	bool GetNextActiveRiderExitLocation(FVector& OutWorldLocation) const;
	bool AdvanceRiderRouteIfNeeded(FMobileSurfaceNavElevatorRider& Rider, const FVector& CurrentLocation) const;
	bool HasRiderExitingAtLocation(const FVector& WorldLocation) const;
	bool ShouldServiceLocation(const FVector& WorldLocation) const;
	bool AreLocationsEquivalent(const FVector& A, const FVector& B) const;
	int32 FindServiceLocationIndex(const FVector& WorldLocation) const;
	TArray<FVector> BuildRouteToLocation(const FVector& TargetWorldLocation) const;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation|Elevator")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation|Elevator")
	TObjectPtr<USceneComponent> PlatformRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation|Elevator")
	TObjectPtr<UStaticMeshComponent> PlatformMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation|Elevator|Debug")
	TObjectPtr<UTextRenderComponent> DebugLabel = nullptr;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator|Debug")
	bool bShowDebugLabel = true;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator|Debug", meta = (ClampMin = "0.0"))
	float DebugLabelHeight = 120.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator|Debug", meta = (ClampMin = "1.0"))
	float DebugLabelWorldSize = 18.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "1"))
	int32 Capacity = 4;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "0.0"))
	float BoardingWaitTime = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "1.0"))
	float MoveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "1.0"))
	float ServiceLocationTolerance = 35.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "0.0"))
	float MovementArrivalTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator")
	bool bReturnToHomeWhenIdle = false;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Elevator", meta = (ClampMin = "0.0"))
	float IdleReturnDelay = 4.0f;

	UPROPERTY(Transient)
	TArray<FMobileSurfaceNavElevatorRequest> QueuedRequests;

	UPROPERTY(Transient)
	TArray<FMobileSurfaceNavElevatorRider> Riders;

	UPROPERTY(Transient)
	EMobileSurfaceNavElevatorState ElevatorState = EMobileSurfaceNavElevatorState::Idle;

	UPROPERTY(Transient)
	FVector CurrentServiceWorldLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector TargetServiceWorldLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector HomeWorldLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	TArray<FVector> ServiceWorldLocations;

	UPROPERTY(Transient)
	EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct;

	UPROPERTY(Transient)
	TArray<FVector> ActiveTravelRouteWorldLocations;

	UPROPERTY(Transient)
	int32 ActiveTravelRouteTargetIndex = INDEX_NONE;

	UPROPERTY(Transient)
	float BoardingTimer = 0.0f;

	UPROPERTY(Transient)
	float IdleTimer = 0.0f;

	UPROPERTY(Transient)
	bool bBoardedPassengerThisStop = false;

	UPROPERTY(Transient)
	bool bDisembarkedPassengerThisStop = false;

	UPROPERTY(Transient)
	bool bStopRequiresBoardingWindow = false;
};
