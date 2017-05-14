// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/Character.h"
#include "Ogro.generated.h"

UCLASS()
class SWEETDREAMS_API AOgro : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AOgro(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;


	UFUNCTION(Category = "Pawn|CustomCharacter", BlueprintCallable)
		virtual void LaunchCharacterRotated(FVector LaunchVelocity, bool bHorizontalOverride, bool bVerticalOverride);
	virtual void ApplyDamageMomentum(float DamageTaken, const FDamageEvent& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser) override;
	virtual FVector GetPawnViewLocation() const override;
	virtual void PostNetReceiveLocationAndRotation() override;
	FORCEINLINE class UCustomCharacterMovementComponent* GetCustomCharacterMovement() const;
};
