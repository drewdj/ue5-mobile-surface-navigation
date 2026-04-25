#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavAgent.generated.h"

class UMobileSurfaceNavComponent;
class UMobileSurfaceNavAgentComponent;
class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;

UCLASS(BlueprintType, Blueprintable)
class MOBILESURFACENAVIGATION_API AMobileSurfaceNavAgent : public AActor
{
	GENERATED_BODY()

public:
	AMobileSurfaceNavAgent();

	void InitializeAgent(UMobileSurfaceNavComponent* InNavigationComponent, float InAgentRadius, float InMoveSpeed, int32 InRandomSeed);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool RequestMoveToWorld(const FVector& TargetWorldPosition);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	bool RequestMoveToLocal(const FVector& TargetLocalPosition);

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void StopMovement();

	UFUNCTION(BlueprintCallable, Category = "Mobile Surface Navigation")
	void SetStateLabelEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	UMobileSurfaceNavComponent* GetNavigationComponent() const;

	UFUNCTION(BlueprintPure, Category = "Mobile Surface Navigation")
	UMobileSurfaceNavAgentComponent* GetNavigationAgentComponent() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyAgentVisualScale();
	void UpdateStateLabel();

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<UStaticMeshComponent> VisualMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<UMobileSurfaceNavAgentComponent> AgentComponent = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<UTextRenderComponent> StateLabel = nullptr;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.0"))
	float AgentRadius = 30.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation", meta = (ClampMin = "1.0"))
	float MoveSpeed = 150.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Debug")
	bool bShowStateLabel = true;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation|Debug", meta = (ClampMin = "0.0"))
	float StateLabelHeight = 80.0f;
};
