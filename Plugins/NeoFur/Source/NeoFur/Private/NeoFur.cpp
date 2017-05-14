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
#include "NeoFurPrivatePCH.h"
#include "NeoFurVersion.h"

#define LOCTEXT_NAMESPACE "FNeoFurModule"

void FNeoFurModule::StartupModule()
{
	UE_LOG(NeoFur, Log, TEXT("NeoFur version %s"), TEXT(NEOFUR_VERSION));
}

void FNeoFurModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNeoFurModule, NeoFur)

DEFINE_LOG_CATEGORY(NeoFur);




