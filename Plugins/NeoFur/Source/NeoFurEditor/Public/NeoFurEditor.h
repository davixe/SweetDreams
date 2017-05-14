#pragma once
#include "Engine.h"
#include "NeoFur.h"

class FNeoFurEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};





