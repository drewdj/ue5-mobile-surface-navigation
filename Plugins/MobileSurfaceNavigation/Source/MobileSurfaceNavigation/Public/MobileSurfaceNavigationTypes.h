#pragma once

#include "CoreMinimal.h"
#include "MobileSurfaceNavigationTypes.generated.h"

UENUM(BlueprintType)
enum class EMobileSurfaceBoundaryKind : uint8
{
	Unknown = 0,
	Outer = 1,
	Hole = 2
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavVertex
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector LocalPosition = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavTriangle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FIntVector VertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FIntVector NeighborTriangleIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector Normal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 RegionId = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavEdge
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FIntPoint VertexIndices = FIntPoint(INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FIntPoint TriangleIndices = FIntPoint(INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	bool bIsBoundary = false;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavBoundaryLoop
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<int32> VertexIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 RegionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	EMobileSurfaceBoundaryKind Kind = EMobileSurfaceBoundaryKind::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	bool bClosed = false;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavRegion
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<int32> TriangleIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<int32> BoundaryLoopIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector AverageNormal = FVector::UpVector;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavRegionRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	FName AreaTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.001"))
	float CostMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavPortal
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 TriangleA = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 TriangleB = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FIntPoint VertexIndices = FIntPoint(INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector LeftPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector RightPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	float Width = 0.0f;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavPortalRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	bool bOpen = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	FName PortalTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.001"))
	float CostMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.0"))
	float ExtraCost = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.0"))
	float EffectiveWidthOverride = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	bool bUserModified = false;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceTriangleAdjacency
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 NeighborTriangleIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 PortalIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	float TravelCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	float PortalWidth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	float BoundaryClearance = 0.0f;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceTriangleAdjacencyList
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceTriangleAdjacency> Neighbors;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceTriangleBounds
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FBox LocalBounds = FBox(EForceInit::ForceInit);
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfacePathQueryParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	float AgentRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	bool bRequireStartAndEndClearance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	bool bCanUseClosedPortals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FName> AllowedAreaTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FName> ExcludedAreaTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FName> AllowedPortalTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FName> ExcludedPortalTags;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavSpecialLink
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	FName LinkId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	int32 FromRegionId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	int32 ToRegionId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	FVector FromLocalPosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	FVector ToLocalPosition = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavVertex> Vertices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavTriangle> Triangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavEdge> Edges;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavBoundaryLoop> BoundaryLoops;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavRegion> Regions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavRegionRuntimeState> RegionRuntimeStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavPortal> Portals;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavPortalRuntimeState> PortalRuntimeStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceTriangleAdjacencyList> TriangleAdjacency;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceTriangleBounds> TriangleBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation")
	TArray<FMobileSurfaceNavSpecialLink> SpecialLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	FString BuildNotes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 RuntimeStateRevision = 0;

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Edges.Reset();
		BoundaryLoops.Reset();
		Regions.Reset();
		RegionRuntimeStates.Reset();
		Portals.Reset();
		PortalRuntimeStates.Reset();
		TriangleAdjacency.Reset();
		TriangleBounds.Reset();
		LocalBounds = FBox(EForceInit::ForceInit);
		bIsValid = false;
		BuildNotes.Reset();
		RuntimeStateRevision = 0;
	}
};

USTRUCT(BlueprintType)
struct MOBILESURFACENAVIGATION_API FMobileSurfaceNavPath
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 StartTriangleIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 EndTriangleIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<int32> TriangleIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FVector> RawWaypoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	TArray<FVector> Waypoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	float EstimatedLength = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation")
	int32 RuntimeStateRevision = 0;
};
