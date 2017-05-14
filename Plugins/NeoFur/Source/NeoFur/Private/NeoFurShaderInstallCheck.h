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

#pragma once

#include "NeoFur.h"

// UGLY HACK ALERT!

// This entire system and everything it does is to work around UE4's
// limitations regarding loading shaders. In the editor mode, we just
// try to make sure the shader is installed in the correct location by
// the time anything actually needs it. Because some of the things
// that need it are in static/global constructors, we need to do all
// this before the module's actual entry point, which means
// extra-hacky hijacking of constructor parameters to make sure this
// runs before those constructors.

void NeoFurRunShaderCheck();
bool NeoFurRunShaderCheck_HijackBoolParameter(bool b);






