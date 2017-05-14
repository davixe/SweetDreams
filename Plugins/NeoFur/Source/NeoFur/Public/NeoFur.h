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

// The #defines in this file can affect the build for source versions
// of NeoFur in various ways. Changing them on binary builds will, at
// best, break those builds because the header version and
// built-binary version of some structures will change.

#pragma once

#include "Engine.h"
#include "ModuleManager.h"

#include "EngineModule.h"
#include "Modules/ModuleVersion.h"

// Compute shader support per-platform. In theory UE4's OpenGL ES 3.1
// support can run compute shaders on mobile (Android), but the fur
// simulation compute shader is not yet compatible on mobile devices
// or Mac. On these platforms a fallback CPU simulation is used
// instead.
#if PLATFORM_HTML5
#define NEOFUR_NO_COMPUTE_SHADERS 1
#elif PLATFORM_ANDROID
#define NEOFUR_NO_COMPUTE_SHADERS 1
#elif PLATFORM_IOS
#define NEOFUR_NO_COMPUTE_SHADERS 1
#elif PLATFORM_MAC
#if ENGINE_MINOR_VERSION < 13
#define NEOFUR_NO_COMPUTE_SHADERS 1
#else
#define NEOFUR_NO_COMPUTE_SHADERS 0
#endif
#else // Linux, Windows, other
#define NEOFUR_NO_COMPUTE_SHADERS 0
#endif

// NeoFur FBX support enabled or disabled. Disabling FBX support
// simplifies the build, because the FBX SDK is not needed, but spline
// import from FBX data will be unavailable. This value is set
// externally by NeoFur.Build.cs, and this #define is only used when
// it has not been set by that.
#ifndef NEOFUR_FBX
#define NEOFUR_FBX 0
#endif

// This is for testing the compiled-in shaders, even in the presence
// of external shader files.
#ifndef NEOFUR_BUILTIN_SHADERS_ONLY
#define NEOFUR_BUILTIN_SHADERS_ONLY 0
#endif

#ifndef NEOFUR_MAX_BONE_COUNT
#define NEOFUR_MAX_BONE_COUNT 256
#endif

#define NEOFUR_ENABLE_PROFILING 0
#define NEOFUR_USE_CUSTOM_RNG 0

class FNeoFurModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

DECLARE_LOG_CATEGORY_EXTERN(NeoFur, Log, All);




