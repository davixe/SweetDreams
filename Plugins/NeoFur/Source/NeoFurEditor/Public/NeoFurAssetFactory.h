#pragma once

#include "NeoFurEditor.h"
#include "UnrealEd.h"
#include "NeoFurAssetFactory.generated.h"

UCLASS()
class UNeoFurAssetFactory : public UFactory
{
	GENERATED_UCLASS_BODY()
	
public:

	// UFactory
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool FactoryCanImport( const FString& Filename ) override;

};





