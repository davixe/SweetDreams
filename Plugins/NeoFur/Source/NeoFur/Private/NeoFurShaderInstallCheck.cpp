// ---------------------------------------------------------------------------
//
// Copyright (c) 2016 Neoglyphic Entertainment, Inc. All rights reserved.
//
// This is part of the NeoFur fur and hair rendering and simulation
// plugin for Unreal Engine.
//
// Do not redistribute NeoFur without the express permission of
// Neoglyphic Entertainment. See your license for specific details.
//
// -------------------------- END HEADER -------------------------------------

#include "NeoFur.h"

#include "NeoFurComponentSceneProxy.h"
#include "NeoFurComputeShader.h"

#include "LocalVertexFactory.h"
#include "GlobalShader.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "RHICommandList.h"
#include "ShaderParameterUtils.h"

#include "NeoFurComponent.h"
#include "NeoFurComponentSceneProxy.h"
#include "NeoFurAsset.h"

#include "RHICommandList.h"

#include "EngineModule.h"
#include "Modules/ModuleVersion.h"

#if WITH_EDITOR
#include "NeoFurStaticBuffers.h"
#endif

// ----------------------------------------------------------------------
// Shader installation checker
// ----------------------------------------------------------------------

// UGLY HACK ALERT!

// This is the system we use in the editor, which is expected to run
// at module initialization time, WHILE CONSTRUCTORS FOR THIS MODULE'S
// GLOBAL/STATIC OBJECTS ARE FIRING.

// This system ensures that the NeoFur compute shader and others are
// installed in the engine's Engine/Shaders directory, because UE4
// refuses to load shaders from plugin directories. This is the best
// (so far) solution that doesn't involve manual work by the user but
// also works okay from the plugin's point of view.

static bool NeoFurShaderCheckHasBeenRun = false;

static FString AttemptToLoadShader(const TArray<FString> &locationList)
{
	FString ret("");
	for(int i = 0; i < locationList.Num(); i++) {
		if(FFileHelper::LoadFileToString(ret, *locationList[i])) {
			return ret;
		}
	}
	return ret;
}

void NeoFurCheckSpecificShader(
	const FString &ShaderBaseFilename,
	const unsigned char* StaticBufferVersion)
{
  #if WITH_EDITOR
  
	// Load the shader from the Engine/Shaders directory, for
	// comparison purposes.
	FString ShaderDir = FPlatformProcess::ShaderDir();
	FString OldShaderContents;
	FString CombinedPathToEngineShader = FPaths::Combine(*ShaderDir, *ShaderBaseFilename);
	FFileHelper::LoadFileToString(
		OldShaderContents,
		*CombinedPathToEngineShader);

	FString PluginShaderFileContents;

	// There doesn't seem to be a nice way to find the directory that
	// the shader file is stored in, so let's just check a bunch of
	// likely paths, then fall back on the internal buffer version.
	TArray<FString> pathList;
	pathList.Add(*FPaths::Combine(*FPaths::EnginePluginsDir(), TEXT("Neoglyphic/NeoFur/Shaders"), *ShaderBaseFilename));
	pathList.Add(*FPaths::Combine(*FPaths::EnginePluginsDir(), TEXT("NeoFur/Shaders"), *ShaderBaseFilename));
	pathList.Add(*FPaths::Combine(*FPaths::GamePluginsDir(),   TEXT("Neoglyphic/NeoFur/Shaders"), *ShaderBaseFilename));
	pathList.Add(*FPaths::Combine(*FPaths::GamePluginsDir(),   TEXT("NeoFur/Shaders"), *ShaderBaseFilename));
	#if !NEOFUR_BUILTIN_SHADERS_ONLY
	PluginShaderFileContents = AttemptToLoadShader(pathList);
	#endif

	// If none of those worked, fall back in the internal version.
	if(!PluginShaderFileContents.Len()) {
		PluginShaderFileContents = (char*)StaticBufferVersion;
	}

	// We definitely should have something by now, otherwise we
	// have some issues.
	check(PluginShaderFileContents.Len());

	// Automatically tag the engine version in this shader so we can do some #if trickery later.
	
	PluginShaderFileContents =
		FString("#define NEOFUR_UE4_ENGINE_MINOR_VERSION ") +
		FString::FromInt(ENGINE_MINOR_VERSION) +
		FString("\n\n") + PluginShaderFileContents;

	if(PluginShaderFileContents != OldShaderContents) {
	    
		// Mismatched contents, one way or another.

		FText AreYouSureMessage = FText::FromString(FString("The NeoFur shader ") + ShaderBaseFilename + FString(" in ") +
			FPaths::ConvertRelativePathToFull(ShaderDir) +
			FString(" is not present or the wrong version.\nThis file is necessary to loading the NeoFur plugin.\n"
				"Would you like to install this file now?\nThis will overwrite any existing copy of the file."));
		FText AreYouSureTitle = FText::FromString(FString("NeoFur"));

		EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::YesNo,
			AreYouSureMessage,
			&AreYouSureTitle);

		if(Response == EAppReturnType::Type::Yes) {

			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::FromString(FString("Writing shader file: ") + CombinedPathToEngineShader),
				&AreYouSureTitle);

			FFileHelper::SaveStringToFile(PluginShaderFileContents, *CombinedPathToEngineShader);
		}
	}
	
  #endif
}

void NeoFurRunShaderCheck()
{
	if(!NeoFurShaderCheckHasBeenRun) {
		#if WITH_EDITOR
		NeoFurCheckSpecificShader("NeoFurComputeShader.usf", NeoFurComputeShader_StaticBuffer);
		NeoFurCheckSpecificShader("NeoFurVertexFactory.usf", NeoFurVertexFactory_StaticBuffer);
		#endif
		NeoFurShaderCheckHasBeenRun = true;
	}
}

bool NeoFurRunShaderCheck_HijackBoolParameter(bool b)
{
	return b;
}

// This just makes sure at least one thing runs this at static/global
// object initialization time.
class FNeoFurInitTimeShaderInstallChecker
{
public:
	FNeoFurInitTimeShaderInstallChecker()
	{
		NeoFurRunShaderCheck();
	}

} NeoFurInitTimeShaderInstallChecker;






