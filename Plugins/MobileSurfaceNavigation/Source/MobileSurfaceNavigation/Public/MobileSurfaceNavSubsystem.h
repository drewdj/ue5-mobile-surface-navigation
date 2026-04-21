#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MobileSurfaceNavSubsystem.generated.h"

class UMobileSurfaceNavComponent;

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfacePathRequestHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 RequestId = INDEX_NONE;

	bool IsValid() const
	{
		return RequestId != INDEX_NONE;
	}
};

USTRUCT()
struct FMobileSurfaceQueuedPathRequest
{
	GENERATED_BODY()

	UPROPERTY()
	int32 RequestId = INDEX_NONE;

	UPROPERTY()
	TWeakObjectPtr<UMobileSurfaceNavComponent> NavComponent;

	UPROPERTY()
	FVector StartLocalPosition = FVector::ZeroVector;

	UPROPERTY()
	FVector EndLocalPosition = FVector::ZeroVector;

	UPROPERTY()
	FMobileSurfacePathQueryParams Params;
};

USTRUCT()
struct FMobileSurfaceQueuedPathResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsReady = false;

	UPROPERTY()
	bool bSucceeded = false;

	UPROPERTY()
	FMobileSurfaceNavPath Path;
};

UCLASS()
class MOBILESURFACENAVIGATION_API UMobileSurfaceNavSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void RegisterNavSurface(UMobileSurfaceNavComponent* NavComponent);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void UnregisterNavSurface(UMobileSurfaceNavComponent* NavComponent);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool FindPathImmediate(
		UMobileSurfaceNavComponent* NavComponent,
		const FVector& StartLocalPosition,
		const FVector& EndLocalPosition,
		const FMobileSurfacePathQueryParams& Params,
		FMobileSurfaceNavPath& OutPath) const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	FMobileSurfacePathRequestHandle QueuePathRequest(
		UMobileSurfaceNavComponent* NavComponent,
		const FVector& StartLocalPosition,
		const FVector& EndLocalPosition,
		const FMobileSurfacePathQueryParams& Params);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool TryGetPathResult(FMobileSurfacePathRequestHandle Handle, FMobileSurfaceNavPath& OutPath, bool& bOutSucceeded);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void CancelPathRequest(FMobileSurfacePathRequestHandle Handle);

private:
	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation")
	int32 MaxPathRequestsPerTick = 16;

	UPROPERTY()
	TArray<TWeakObjectPtr<UMobileSurfaceNavComponent>> RegisteredNavSurfaces;

	UPROPERTY()
	TArray<FMobileSurfaceQueuedPathRequest> PendingRequests;

	UPROPERTY()
	TMap<int32, FMobileSurfaceQueuedPathResult> ResultsByRequestId;

	UPROPERTY()
	int32 NextRequestId = 1;
};
