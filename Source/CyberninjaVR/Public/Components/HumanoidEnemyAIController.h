// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "HumanoidEnemyAIController.generated.h"

UCLASS()
class CYBERNINJAVR_API AHumanoidEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHumanoidEnemyAIController();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moveset", meta = (RequiredAssetDataTags = "RowStructure=/Script/CyberninjaVR.AttackAnimSequence"))
	TObjectPtr<UDataTable> MovesetDataTable;

	UPROPERTY(BlueprintReadOnly, Category = "Moveset")
	TArray<UAnimSequence*> MovesetAnimations;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
