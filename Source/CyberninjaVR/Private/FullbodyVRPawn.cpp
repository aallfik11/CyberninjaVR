// Fill out your copyright notice in the Description page of Project Settings.


#include "FullbodyVRPawn.h"

#include "MotionControllerComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"

template <typename T>
static T GetYawFromQuat(const UE::Math::TQuat<T>& Quat)
{
	const T X = Quat.X;
	const T Y = Quat.Y;
	const T Z = Quat.Z;
	const T W = Quat.W;

	const T Numerator = 2 * (W * Z + X * Y);
	const T Denominator = 1 - (2 * (FMath::Square(Y) + FMath::Square(Z)));
	return FMath::Atan2(Numerator, Denominator);
}

// Sets default values
AFullbodyVRPawn::AFullbodyVRPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	//creating defaults
	VROrigin = CreateDefaultSubobject<USceneComponent>("VROrigin");
	VRCamera = CreateDefaultSubobject<UCameraComponent>("VRCamera");
	MotionControllerLeftGrip = CreateDefaultSubobject<UMotionControllerComponent>("MotionControllerLeftGrip");
	MotionControllerRightGrip = CreateDefaultSubobject<UMotionControllerComponent>("MotionControllerRightGrip");
	MotionControllerLeftAim = CreateDefaultSubobject<UMotionControllerComponent>("MotionControllerLeftAim");
	MotionControllerRightAim = CreateDefaultSubobject<UMotionControllerComponent>("MotionControllerRightAim");
	PlayerMesh = CreateDefaultSubobject<USkeletalMeshComponent>("PlayerMesh");
	PlayerHeight = 173.f;
	PreviousYaw = 0.f;
	
	//now set up attachment
	SetRootComponent(VROrigin);
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;
	MotionControllerLeftGrip->SetupAttachment(VROrigin);
	MotionControllerRightGrip->SetupAttachment(VROrigin);
	MotionControllerLeftAim->SetupAttachment(VROrigin);
	MotionControllerRightAim->SetupAttachment(VROrigin);
	
	//for now keep it detached
	// PlayerMesh->SetupAttachment(VROrigin);
}

// Called when the game starts or when spawned
void AFullbodyVRPawn::BeginPlay()
{
	Super::BeginPlay();

	PlayerMesh->HideBoneByName(TEXT("head"), PBO_None);
}

// Called every frame
void AFullbodyVRPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FQuat CameraQuat = VRCamera->GetComponentQuat().GetNormalized();
	if(GEngine)
	{
		GEngine->AddOnScreenDebugMessage(124, 10.f, FColor::Blue, CameraQuat.ToString());
	}

	const float X = CameraQuat.X;
	const float Y = CameraQuat.Y;
	const float Z = CameraQuat.Z;
	const float W = CameraQuat.W;

	float Yaw = 180.0f * (FMath::Atan2(2.0f * (X * Z + X * Y), 1.0f - 2.0f * (Y * Y + Z * Z)) / UKismetMathLibrary::GetPI());

	// CameraQuat.X = 0;
	// CameraQuat.Y = 0;
	// CameraQuat.Normalize();

	// Yaw = 2.0f * FMath::Acos(CameraQuat.Z);
	Yaw = CameraQuat.Rotator().Yaw;
	Yaw = 180.f * GetYawFromQuat(CameraQuat) / UKismetMathLibrary::GetPI();
	Yaw -= 90.f; //take into account the offset of the mesh
	GEngine->AddOnScreenDebugMessage(123, 10.f, FColor::Red, FString::Printf(TEXT("Yaw: %f"), Yaw));

	//check if the yaw changed abruptly, if it did, we most likely didn't want to update it
	// if (FMath::Abs(Yaw - PreviousYaw) > 60.f)
	// {
		// return;
	// }
	PreviousYaw = Yaw;
	const FRotator MeshRotator = PlayerMesh->GetComponentRotation();
	const float MeshYaw = PlayerMesh->GetComponentRotation().Yaw;

	const float InterpYaw = FMath::FInterpTo(MeshYaw, Yaw, DeltaTime, 4.0f);

	// PlayerMesh->SetWorldRotation(CameraQuat, false, nullptr, ETeleportType::TeleportPhysics);
	// PlayerMesh->SetWorldRotation(FRotator(MeshRotator.Pitch, InterpYaw, MeshRotator.Roll));
	PlayerMesh->SetWorldRotation(UKismetMathLibrary::ComposeRotators({MeshRotator.Pitch, -90, MeshRotator.Roll}, {0, CameraQuat.Rotator().Yaw, 0}));
	FVector NewForward = UKismetMathLibrary::Quat_RotateVector(CameraQuat, PlayerMesh->GetForwardVector());
	NewForward.Z = 0.0f;

	const FVector CameraLocation = VRCamera->GetComponentLocation();
	
	PlayerMesh->SetWorldLocation({CameraLocation.X, CameraLocation.Y, CameraLocation.Z - PlayerHeight});
}	

// Called to bind functionality to input
void AFullbodyVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

