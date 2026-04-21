#include "MobileSurfaceNavSubsystem.h"

#include "MobileSurfaceNavComponent.h"
#include "MobileSurfacePathfinder.h"

#include "Stats/Stats.h"

void UMobileSurfaceNavSubsystem::Tick(float DeltaTime)
{
	const int32 RequestsToProcess = FMath::Min(FMath::Max(0, MaxPathRequestsPerTick), PendingRequests.Num());
	for (int32 RequestIndex = 0; RequestIndex < RequestsToProcess; ++RequestIndex)
	{
		const FMobileSurfaceQueuedPathRequest& Request = PendingRequests[RequestIndex];
		FMobileSurfaceQueuedPathResult& Result = ResultsByRequestId.FindOrAdd(Request.RequestId);
		Result.bIsReady = true;
		Result.bSucceeded = false;
		Result.Path = FMobileSurfaceNavPath();

		if (UMobileSurfaceNavComponent* NavComponent = Request.NavComponent.Get())
		{
			Result.bSucceeded = FindPathImmediate(
				NavComponent,
				Request.StartLocalPosition,
				Request.EndLocalPosition,
				Request.Params,
			Result.Path);
		}
	}

	if (RequestsToProcess > 0)
	{
		PendingRequests.RemoveAt(0, RequestsToProcess, EAllowShrinking::No);
	}
}

TStatId UMobileSurfaceNavSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMobileSurfaceNavSubsystem, STATGROUP_Tickables);
}

void UMobileSurfaceNavSubsystem::RegisterNavSurface(UMobileSurfaceNavComponent* NavComponent)
{
	if (!NavComponent)
	{
		return;
	}

	RegisteredNavSurfaces.RemoveAll([](const TWeakObjectPtr<UMobileSurfaceNavComponent>& Existing)
	{
		return !Existing.IsValid();
	});

	RegisteredNavSurfaces.AddUnique(NavComponent);
}

void UMobileSurfaceNavSubsystem::UnregisterNavSurface(UMobileSurfaceNavComponent* NavComponent)
{
	if (!NavComponent)
	{
		return;
	}

	RegisteredNavSurfaces.Remove(NavComponent);
}

bool UMobileSurfaceNavSubsystem::FindPathImmediate(
	UMobileSurfaceNavComponent* NavComponent,
	const FVector& StartLocalPosition,
	const FVector& EndLocalPosition,
	const FMobileSurfacePathQueryParams& Params,
	FMobileSurfaceNavPath& OutPath) const
{
	if (!NavComponent || !NavComponent->HasValidNavigationData())
	{
		OutPath = FMobileSurfaceNavPath();
		return false;
	}

	return FMobileSurfacePathfinder::FindPath(
		NavComponent->GetNavigationData(),
		StartLocalPosition,
		EndLocalPosition,
		Params,
		OutPath);
}

FMobileSurfacePathRequestHandle UMobileSurfaceNavSubsystem::QueuePathRequest(
	UMobileSurfaceNavComponent* NavComponent,
	const FVector& StartLocalPosition,
	const FVector& EndLocalPosition,
	const FMobileSurfacePathQueryParams& Params)
{
	FMobileSurfacePathRequestHandle Handle;
	if (!NavComponent)
	{
		return Handle;
	}

	Handle.RequestId = NextRequestId++;

	FMobileSurfaceQueuedPathRequest& Request = PendingRequests.AddDefaulted_GetRef();
	Request.RequestId = Handle.RequestId;
	Request.NavComponent = NavComponent;
	Request.StartLocalPosition = StartLocalPosition;
	Request.EndLocalPosition = EndLocalPosition;
	Request.Params = Params;

	FMobileSurfaceQueuedPathResult& Result = ResultsByRequestId.FindOrAdd(Handle.RequestId);
	Result = FMobileSurfaceQueuedPathResult();
	return Handle;
}

bool UMobileSurfaceNavSubsystem::TryGetPathResult(
	FMobileSurfacePathRequestHandle Handle,
	FMobileSurfaceNavPath& OutPath,
	bool& bOutSucceeded)
{
	OutPath = FMobileSurfaceNavPath();
	bOutSucceeded = false;

	if (!Handle.IsValid())
	{
		return false;
	}

	FMobileSurfaceQueuedPathResult* Result = ResultsByRequestId.Find(Handle.RequestId);
	if (!Result || !Result->bIsReady)
	{
		return false;
	}

	OutPath = Result->Path;
	bOutSucceeded = Result->bSucceeded;
	ResultsByRequestId.Remove(Handle.RequestId);
	return true;
}

void UMobileSurfaceNavSubsystem::CancelPathRequest(FMobileSurfacePathRequestHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	PendingRequests.RemoveAllSwap([Handle](const FMobileSurfaceQueuedPathRequest& Request)
	{
		return Request.RequestId == Handle.RequestId;
	}, EAllowShrinking::No);

	ResultsByRequestId.Remove(Handle.RequestId);
}
