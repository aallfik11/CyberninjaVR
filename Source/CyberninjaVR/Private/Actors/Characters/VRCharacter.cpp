// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/Characters/VRCharacter.h"


// Sets default values
AVRCharacter::AVRCharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AVRCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AVRCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AVRCharacter::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void AVRCharacter::FellOutOfWorld(const class UDamageType &dmgType)
{
	// Don't want the actor to be destroyed in case of the player
	// Super::FellOutOfWorld(dmgType);
	
	// Since pretty much all of the character's implementation is in blueprints, I want this to be there as well.
	OnFellOutOfWorld(&dmgType);
}

void AVRCharacter::OnFellOutOfWorld_Implementation(const UDamageType *dmgType)
{
	UE_LOG(LogTemp, Warning, TEXT("C++ Version of Fell Out Of World called. This probably shouldn't be happening"));
}

