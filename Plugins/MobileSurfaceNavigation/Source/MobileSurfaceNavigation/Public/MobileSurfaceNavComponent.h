#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavComponent.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class AMobileSurfaceNavAgent;
class UTextRenderComponent;

UCLASS(ClassGroup = (Navigation), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class MOBILESURFACENAVIGATION_API UMobileSurfaceNavComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMobileSurfaceNavComponent();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation")
	void RebuildNavigationData();

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void DrawNavigationDebug(float Duration = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool FindPathLocal(const FVector& StartLocalPosition, const FVector& EndLocalPosition, FMobileSurfaceNavPath& OutPath, float AgentRadius = 0.0f) const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool FindPathLocalWithParams(const FVector& StartLocalPosition, const FVector& EndLocalPosition, const FMobileSurfacePathQueryParams& Params, FMobileSurfaceNavPath& OutPath) const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	int32 FindContainingTriangle(const FVector& LocalPosition) const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	int32 FindNearestTriangle(const FVector& LocalPosition) const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void ClearNavigationData();

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	bool HasValidNavigationData() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	bool WasLastBuildSuccessful() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	int32 GetPortalCount() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	int32 GetRegionCount() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	int32 GetRuntimeStateRevision() const;

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetPortalOpen(int32 PortalIndex, bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetPortalCostMultiplier(int32 PortalIndex, float CostMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetPortalExtraCost(int32 PortalIndex, float ExtraCost);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetPortalEffectiveWidthOverride(int32 PortalIndex, float EffectiveWidthOverride);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetPortalTag(int32 PortalIndex, FName PortalTag);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool ResetPortalRuntimeState(int32 PortalIndex);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetRegionEnabled(int32 RegionId, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetRegionCostMultiplier(int32 RegionId, float CostMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool SetRegionAreaTag(int32 RegionId, FName AreaTag);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	bool ResetRegionRuntimeState(int32 RegionId);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Runtime State")
	void ResetAllRuntimeState();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void OpenSelectedPortal();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void CloseSelectedPortal();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void ToggleSelectedPortal();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void ResetSelectedPortalRuntimeState();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void EnableSelectedRegion();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void DisableSelectedRegion();

	const FMobileSurfaceNavData& GetNavigationData() const;

	const FString& GetLastBuildError() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UStaticMeshComponent* GetNavigationSourceMeshComponent() const;
	USceneComponent* GetNavigationSpaceComponent() const;
	void MarkRuntimeStateDirty();
	void GenerateDebugPath();
	void DrawDebugPath(float Duration) const;
	void SpawnDebugAgents();
	void DestroyDebugAgents();
	void RefreshPortalLabelComponents();
	void DestroyPortalLabelComponents();
	void UpdateTickState();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true", UseComponentPicker, AllowedClasses = "/Script/Engine.StaticMeshComponent"))
	FComponentReference NavigationSourceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Build", meta = (AllowPrivateAccess = "true", ClampMin = "0.0001"))
	float VertexWeldTolerance = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Build", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", ClampMax = "45.0"))
	float PlanarRegionAngleToleranceDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Build", meta = (AllowPrivateAccess = "true", ClampMin = "0.001"))
	float PlanarRegionDistanceTolerance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Build", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float BoundarySimplificationTolerance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawDebugEveryTick = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bAutoDrawDebugAfterRebuild = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bAutoDrawDebugOnRuntimeStateChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawVertices = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawBoundaryEdges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawTriangles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawTriangleNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawPortals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawPortalLabels = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Editor Tools", meta = (AllowPrivateAccess = "true", ClampMin = "-1"))
	int32 SelectedPortalIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Editor Tools", meta = (AllowPrivateAccess = "true", ClampMin = "-1"))
	int32 SelectedRegionId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true"))
	bool bRebuildOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true"))
	bool bAutoGenerateDebugPaths = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true"))
	bool bDrawRawDebugPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float DebugPathGenerationInterval = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugPathDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Paths", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 DebugPathRandomSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true"))
	bool bSpawnDebugAgentsOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 DebugAgentCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugAgentRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float DebugAgentMoveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 DebugAgentRandomSeed = 777;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AMobileSurfaceNavAgent> DebugAgentClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugDuration = 15.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Mobile Surface Navigation", Transient, meta = (AllowPrivateAccess = "true"))
	FMobileSurfaceNavData NavigationData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	FString LastBuildError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	bool bLastBuildSucceeded = false;

	UPROPERTY(Transient)
	FMobileSurfaceNavPath CurrentDebugPath;

	UPROPERTY(Transient)
	float DebugPathTimeAccumulator = 0.0f;

	UPROPERTY(Transient)
	FRandomStream DebugPathRandomStream;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMobileSurfaceNavAgent>> SpawnedDebugAgents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> PortalLabelComponents;
};
