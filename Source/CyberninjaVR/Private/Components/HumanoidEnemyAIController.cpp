// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/HumanoidEnemyAIController.h"

#include "AttackAnimSequence.h"
#include "GameFramework/Character.h"


// Sets default values
AHumanoidEnemyAIController::AHumanoidEnemyAIController()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AHumanoidEnemyAIController::BeginPlay()
{
	Super::BeginPlay();
	if(MovesetDataTable)
	{
		TArray<FAttackAnimSequence *> Temp; 
		MovesetDataTable->GetAllRows("", Temp);
		MovesetAnimations.Reserve(Temp.Num());
		for(const FAttackAnimSequence* AttackAnimStruct : Temp)
		{
			MovesetAnimations.Add(AttackAnimStruct->AttackAnim);
		}
	}
}


// Called every frame
void AHumanoidEnemyAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

