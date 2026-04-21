#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobileSurfaceNavigationTypes.h"
#include "MobileSurfaceNavAgent.generated.h"

class UMobileSurfaceNavComponent;
class UMobileSurfaceNavAgentComponent;
class UStaticMeshComponent;
class USceneComponent;

UCLASS(BlueprintType, Blueprintable)
class MOBILESURFACENAVIGATION_API AMobileSurfaceNavAgent : public AActor
{
	GENERATED_BODY()

public:
	AMobileSurfaceNavAgent();

	void InitializeAgent(UMobileSurfaceNavComponent* InNavigationComponent, float InAgentRadius, float InMoveSpeed, int32 InRandomSeed);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void ApplyAgentVisualScale();

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<UStaticMeshComponent> VisualMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Mobile Surface Navigation")
	TObjectPtr<UMobileSurfaceNavAgentComponent> AgentComponent = nullptr;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation", meta = (ClampMin = "0.0"))
	float AgentRadius = 30.0f;

	UPROPERTY(EditAnywhere, Category = "Mobile Surface Navigation", meta = (ClampMin = "1.0"))
	float MoveSpeed = 150.0f;
};
