// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AttackAnimSequence.generated.h"

/**
 * Wrapper around an AnimSequence for use in a DataTable
 */
USTRUCT(BlueprintType)
struct CYBERNINJAVR_API FAttackAnimSequence : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UAnimSequence> AttackAnim = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxDistanceFromTarget = 0.0f;
};
