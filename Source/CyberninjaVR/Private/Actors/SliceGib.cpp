// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/SliceGib.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshSlicerComponent.h"
#include "Rendering/RenderCommandPipes.h"
#include "Subsystems/SkeletalMeshPoolSubsystem.h"


// Sets default values
ASliceGib::ASliceGib()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	SetRootComponent(Capsule);
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetupAttachment(Capsule);
	
	
	SkeletalMeshSlicer = CreateDefaultSubobject<USkeletalMeshSlicerComponent>(TEXT("MeshSlicer"));
}

// Called when the game starts or when spawned
void ASliceGib::BeginPlay()
{
	Super::BeginPlay();
}

void ASliceGib::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ASliceGib::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ASliceGib::SliceMesh_Implementation(UMeshComponent *MeshComponent,
	const FVector SlicePoint,
	const FVector SliceNormal,
	UMaterialInterface *CapMaterial,
	const FName BoneName)
{
	SkeletalMeshSlicer->SliceMesh(MeshComponent, SlicePoint, SliceNormal, CapMaterial, BoneName);
}

