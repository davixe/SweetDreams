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
#include "NeoFurShaderInstallCheck.h"
#include "NeoFurComputeShader.h"

#if !NEOFUR_NO_COMPUTE_SHADERS

FNeoFurComputeShaderCS::FNeoFurComputeShaderCS()
{
	// I guess we're going to rely on deserialization to set up bindings.
}

FNeoFurComputeShaderCS::FNeoFurComputeShaderCS(const ShaderMetaType::CompiledShaderInitializerType &Initializer) :
	FGlobalShader(Initializer)
{
	In_BoneMatrices.Bind(Initializer.ParameterMap, TEXT("In_BoneMatrices"));
	In_ControlPoints.Bind(Initializer.ParameterMap, TEXT("In_ControlPoints"));
	In_OriginalMesh.Bind(Initializer.ParameterMap, TEXT("In_OriginalMesh"));
	
	In_PhysicsProperties.Bind(Initializer.ParameterMap, TEXT("In_PhysicsProperties"));

	Out_ControlPoints.Bind(Initializer.ParameterMap, TEXT("Out_ControlPoints"));
	Out_PostAnimVertexBuffer.Bind(Initializer.ParameterMap, TEXT("Out_PostAnimVertexBuffer"));

	In_MorphData.Bind(Initializer.ParameterMap, TEXT("In_MorphData"));

    In_PerFrameData.Bind(Initializer.ParameterMap, TEXT("In_PerFrameData"));
}

bool FNeoFurComputeShaderCS::Serialize( FArchive& Ar )
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize( Ar );
	Ar << In_BoneMatrices;
	Ar << In_ControlPoints;
	Ar << In_OriginalMesh;
	Ar << In_PhysicsProperties;
	Ar << Out_ControlPoints;
	Ar << Out_PostAnimVertexBuffer;
	Ar << In_MorphData;

	Ar << In_PerFrameData;

	return bShaderHasOutdatedParameters;
}

const TCHAR* FNeoFurComputeShaderCS::GetSourceFilename()
{
	NeoFurRunShaderCheck();
	return TEXT("NeoFurComputeShader");
}

const TCHAR* FNeoFurComputeShaderCS::GetFunctionName()
{
	return TEXT("NeoFurComputeShader_Main");
}

IMPLEMENT_SHADER_TYPE3(FNeoFurComputeShaderCS, SF_Compute);

#endif




