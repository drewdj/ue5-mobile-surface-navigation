#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavComponent.generated.h"

class UStaticMeshComponent;
class USceneComponent;

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
	void ClearNavigationData();

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	bool HasValidNavigationData() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	bool WasLastBuildSuccessful() const;

	const FMobileSurfaceNavData& GetNavigationData() const;

	const FString& GetLastBuildError() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UStaticMeshComponent* GetNavigationSourceMeshComponent() const;
	USceneComponent* GetNavigationSpaceComponent() const;
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
	bool bDrawVertices = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawBoundaryEdges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawTriangles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawTriangleNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobile Surface Navigation|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DebugDuration = 15.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Mobile Surface Navigation", Transient, meta = (AllowPrivateAccess = "true"))
	FMobileSurfaceNavData NavigationData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	FString LastBuildError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mobile Surface Navigation", meta = (AllowPrivateAccess = "true"))
	bool bLastBuildSucceeded = false;
};
