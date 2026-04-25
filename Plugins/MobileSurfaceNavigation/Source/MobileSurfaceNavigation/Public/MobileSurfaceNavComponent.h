#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavComponent.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class AMobileSurfaceNavAgent;
class AMobileSurfaceNavElevator;
class UTextRenderComponent;

struct FMobileSurfaceNavLadderRuntimeState
{
	TArray<TWeakObjectPtr<AActor>> ActiveAgents;
	int32 ActiveDirectionSign = 0;
};

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
	bool HasNavigationDataForAgentRadius(float AgentRadius) const;

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

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	int32 AddSpecialLinkFromLocalPoints(
		FName LinkId,
		const FVector& FromLocalPosition,
		const FVector& ToLocalPosition,
		EMobileSurfaceNavSpecialLinkType LinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct,
		bool bBidirectional = true,
		float Cost = 100.0f,
		FName LinkTag = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	int32 AddSpecialLinkFromWorldPoints(
		FName LinkId,
		const FVector& FromWorldPosition,
		const FVector& ToWorldPosition,
		EMobileSurfaceNavSpecialLinkType LinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct,
		bool bBidirectional = true,
		float Cost = 100.0f,
		FName LinkTag = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	int32 AddSpecialLinkFromLocalNodes(
		FName LinkId,
		const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes,
		EMobileSurfaceNavSpecialLinkType LinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct,
		bool bBidirectional = true,
		float Cost = 100.0f,
		FName LinkTag = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	int32 AddSpecialLinkFromWorldNodes(
		FName LinkId,
		const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes,
		EMobileSurfaceNavSpecialLinkType LinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct,
		bool bBidirectional = true,
		float Cost = 100.0f,
		FName LinkTag = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	int32 AddSpecialLinkFromSceneComponents(
		FName LinkId,
		const TArray<USceneComponent*>& SceneComponents,
		EMobileSurfaceNavSpecialLinkType LinkType = EMobileSurfaceNavSpecialLinkType::Ladder,
		EMobileSurfaceNavLinkTraversalMode TraversalMode = EMobileSurfaceNavLinkTraversalMode::Direct,
		bool bBidirectional = true,
		float Cost = 100.0f,
		FName LinkTag = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	bool SetSpecialLinkEnabled(int32 LinkIndex, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	bool SetSpecialLinkTraversalMode(int32 LinkIndex, EMobileSurfaceNavLinkTraversalMode TraversalMode);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	bool SetSpecialLinkElevatorActor(int32 LinkIndex, AMobileSurfaceNavElevator* ElevatorActor);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	bool SetSpecialLinkNodes(int32 LinkIndex, const TArray<FMobileSurfaceNavSpecialLinkNode>& Nodes);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	bool RemoveSpecialLink(int32 LinkIndex);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation|Special Links")
	void ClearSpecialLinks();

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation|Special Links")
	int32 GetSpecialLinkCount() const;

	bool TryAcquireLadderTraversal(int32 LinkIndex, AActor* Agent, int32 DirectionSign);
	void ReleaseLadderTraversal(int32 LinkIndex, AActor* Agent);

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

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void EnableSelectedSpecialLink();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mobile Surface Navigation|Editor Tools")
	void DisableSelectedSpecialLink();

	const FMobileSurfaceNavData& GetNavigationData() const;
	const FMobileSurfaceNavData* GetNavigationDataForAgentRadius(float AgentRadius) const;

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
	bool ResolveSpecialLinkTriangles(FMobileSurfaceNavSpecialLink& Link) const;
	void GenerateDebugPath();
	void DrawDebugPath(float Duration) const;
	void SpawnDebugAgents();
	void DestroyDebugAgents();
	void RefreshPortalLabelComponents();
	void UpdatePortalLabelFacing();
	void DestroyPortalLabelComponents();
	void UpdateTickState();
	void EnsureSpecialLinkRuntimeStateSize();
	void RebuildAgentRadiusLayers();
	void SyncRuntimeStateToLayers();
	void SyncSpecialLinksToLayers();
	const FMobileSurfaceNavData* ResolveNavigationDataForAgentRadius(float AgentRadius) const;
	FMobileSurfaceNavData* FindNavigationLayerDataByRadius(float AgentRadius);
	const FMobileSurfaceNavData* FindNavigationLayerDataByRadius(float AgentRadius) const;
	FMobileSurfaceNavLadderRuntimeState* GetLadderRuntimeState(int32 LinkIndex);
	const FMobileSurfaceNavLadderRuntimeState* GetLadderRuntimeState(int32 LinkIndex) const;
	void CleanupLadderRuntimeState(FMobileSurfaceNavLadderRuntimeState& RuntimeState) const;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Build", meta = (AllowPrivateAccess = "true"))
	TArray<float> SupportedAgentRadii;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawSpecialLinks = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Editor Tools", meta = (AllowPrivateAccess = "true", ClampMin = "-1"))
	int32 SelectedPortalIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Editor Tools", meta = (AllowPrivateAccess = "true", ClampMin = "-1"))
	int32 SelectedRegionId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Editor Tools", meta = (AllowPrivateAccess = "true", ClampMin = "-1"))
	int32 SelectedSpecialLinkIndex = INDEX_NONE;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugAgentRandomPathDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 DebugAgentRandomSeed = 777;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug Agents", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AMobileSurfaceNavAgent> DebugAgentClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugDuration = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugDrawAgentRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawAllAgentRadiusLayers = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugLayerVerticalSpacing = 40.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Mobile Surface Navigation", Transient, meta = (AllowPrivateAccess = "true"))
	FMobileSurfaceNavData NavigationData;

	UPROPERTY(BlueprintReadOnly, Category = "Mobile Surface Navigation", Transient, meta = (AllowPrivateAccess = "true"))
	TArray<FMobileSurfaceNavDataLayer> NavigationDataLayers;

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

	TArray<FMobileSurfaceNavLadderRuntimeState> LadderRuntimeStates;
};
