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
#include "NeoFurComponentSceneProxy.h"
#include "GlobalShader.h"

#if !NEOFUR_NO_COMPUTE_SHADERS

struct FNeoFurComputeShaderPerFrameData
{
    uint32_t ControlPointCount;
    uint32_t ShellCount;
    uint32_t TotalShellCount;
    float ShellDistance;
    float DeltaTime;
    float ShellFade;
    FVector LocalSpaceGravity;
    FMatrix TransformFromLastFrame;
    float VisibleLengthScale;
};

// FIXME: Redundant name.
class FNeoFurComputeShaderCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNeoFurComputeShaderCS, Global);
	
public:

	// TODO: Make private?

	FShaderResourceParameter In_BoneMatrices;
	FShaderResourceParameter In_ControlPoints;
	FShaderResourceParameter In_OriginalMesh;
	FShaderResourceParameter In_MorphData;
	FShaderResourceParameter In_PhysicsProperties;

	FShaderResourceParameter Out_ControlPoints;
	FShaderResourceParameter Out_PostAnimVertexBuffer;

    FShaderResourceParameter In_PerFrameData;

	FNeoFurComputeShaderCS();
	explicit FNeoFurComputeShaderCS(const ShaderMetaType::CompiledShaderInitializerType &Initializer);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return RHISupportsComputeShaders(Platform);
	}
	
	virtual bool Serialize( FArchive& Ar ) override;

	static const TCHAR* GetSourceFilename();
	static const TCHAR* GetFunctionName();
};

#endif





