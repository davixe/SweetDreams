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

// FIXME: Replace this with UE4's actual profiling stuff.

#pragma once

#include "NeoFur.h"

#include "Engine.h"

#if WITH_EDITOR && NEOFUR_ENABLE_PROFILING

class FNeoFurScopeTimer
{
public:

	const int32 IndentSpaces = 4;
	
	FNeoFurScopeTimer(const TCHAR *InName);
	~FNeoFurScopeTimer();

private:

	double StartTime;
	const TCHAR *Name;
};


#define NEOFUR_PROFILE_SCOPE(name) FNeoFurScopeTimer __scopeTimer__##__COUNTER__(TEXT(name))
#else
#define NEOFUR_PROFILE_SCOPE(name) do { } while(0)
#endif





