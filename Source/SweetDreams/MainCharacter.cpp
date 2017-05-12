// Fill out your copyright notice in the Description page of Project Settings.

#include "SweetDreams.h"
#include "MainCharacter.h"
#include "CustomCharacterMovementComponent.h"


// Sets default values
AMainCharacter::AMainCharacter(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer.SetDefaultSubobjectClass<UCustomCharacterMovementComponent>(ACharacter::CharacterMovementComponentName)) {

 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AMainCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AMainCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AMainCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	InputComponent->BindAxis("MoveForward", this, &AMainCharacter::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &AMainCharacter::MoveRight);

}

void AMainCharacter::MoveForward(float Value)
{
	if (Controller != NULL && Value != 0.0f)
	{
		// Add forward movement.
		AddMovementInput(FVector::VectorPlaneProject(FRotationMatrix(Controller->GetControlRotation()).GetScaledAxis(EAxis::X),
			GetCapsuleComponent()->GetComponentQuat().GetAxisZ()).GetSafeNormal(), Value);
	}
}

void AMainCharacter::MoveRight(float Value)
{
	if (Controller != NULL && Value != 0.0f)
	{
		const FVector CapsuleUp = GetCapsuleComponent()->GetComponentQuat().GetAxisZ();

		// Add side movement.
		AddMovementInput(CapsuleUp ^ FVector::VectorPlaneProject(FRotationMatrix(Controller->GetControlRotation()).GetScaledAxis(EAxis::X), CapsuleUp).GetSafeNormal(), Value);
	}
}
