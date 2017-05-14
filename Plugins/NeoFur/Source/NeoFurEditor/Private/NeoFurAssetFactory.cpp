#include "NeoFurEditor.h"

#include "NeoFurAssetFactory.h"
#include "NeoFurAsset.h"

UNeoFurAssetFactory::UNeoFurAssetFactory(const FObjectInitializer &ObjectInitializer) :
	Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UNeoFurAsset::StaticClass();
}

UObject* UNeoFurAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<UNeoFurAsset>(InParent, InClass, InName, Flags | RF_Transactional);
}

bool UNeoFurAssetFactory::FactoryCanImport( const FString& Filename )
{
	return false;
}




