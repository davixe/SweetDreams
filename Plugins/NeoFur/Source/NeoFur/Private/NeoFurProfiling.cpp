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

#include "NeoFurProfiling.h"

#if WITH_EDITOR && NEOFUR_ENABLE_PROFILING

static int32 NeoFurScopeCounter = 0;

static inline const TCHAR *MakeSpaces(int32 Num)
{
	static TCHAR buf[1024];
	if(Num > 1023) Num = 1023;
	if(Num < 0) Num = 0;
	buf[Num] = 0;
	while(Num) {
		Num--;
		buf[Num] = ' ';
	}
	return buf;
}

FNeoFurScopeTimer::FNeoFurScopeTimer(const TCHAR *InName)
{
	Name = InName;
	StartTime = FPlatformTime::Seconds();
	UE_LOG(
		NeoFur, Log, TEXT("Profiler: %s%s {"),
		MakeSpaces(NeoFurScopeCounter * IndentSpaces), Name);
	NeoFurScopeCounter++;
}

FNeoFurScopeTimer::~FNeoFurScopeTimer()
{
	double TotalTime = FPlatformTime::Seconds() - StartTime;
	float ms = float(TotalTime) * 1000.0f;
	NeoFurScopeCounter--;
	UE_LOG(
		NeoFur, Log, TEXT("Profiler: %s} (%s): %f"),
		MakeSpaces(NeoFurScopeCounter * IndentSpaces), Name, ms);
}

#endif




