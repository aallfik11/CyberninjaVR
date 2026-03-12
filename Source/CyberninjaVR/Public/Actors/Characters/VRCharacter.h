// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VRCharacter.generated.h"

UCLASS()
class CYBERNINJAVR_API AVRCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AVRCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent *PlayerInputComponent) override;


	virtual void FellOutOfWorld(const class UDamageType &dmgType) override;

	UFUNCTION(BlueprintNativeEvent, DisplayName="Fell Out Of World")
	void OnFellOutOfWorld(const UDamageType* dmgType);

	virtual void OnFellOutOfWorld_Implementation(const UDamageType* dmgType);
};
