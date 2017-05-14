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

#include "LocalVertexFactory.h"
#include "GlobalShader.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "RHICommandList.h"
#include "ShaderParameterUtils.h"

// ----------------------------------------------------------------------
// Private data
// ----------------------------------------------------------------------

struct FNeoFurProxyPrivateData
{
	int32 ActiveShellCount;
	int32 LastSimulatedActiveShellCount[2]; // Record of ActiveShellCount when it was used to simulate each of the two control point buffers.
    int32 ShellCount;
	float ShellDistance;
	float ShellFade;
	
#if !NEOFUR_NO_COMPUTE_SHADERS
	TShaderMapRef<FNeoFurComputeShaderCS> ComputeShader;
#endif
	FMaterialRenderProxy *MaterialRenderProxy;

	FNeoFurPhysicsParameters PhysicsParameters;
	FNeoFurFramePhysicsInputs PhysicsFrameInputs;

	// FIXME: Remove old mesh data.
	// Source data copied from the original skeletal mesh.
	FVertexBuffer SkeletalMeshVertexBufferCopy_GPU;
	TArray<FNeoFurComponentSceneProxy::VertexType> SkeletalMeshVertexBufferCopy_CPU;
	FIndexBuffer SkeletalMeshIndexBufferCopy; // Don't need a CPU version of this (not used in simulation).

	// New mesh data.
	TArray<FNeoFurComponentSceneProxy::VertexType_Static> StaticVertexData_CPU;
	FVertexBuffer StaticVertexData_GPU;

	// Control point data (last frame and next frame).
	FVertexBuffer ControlPointVertexBuffers_GPU[2];
	TArray<FNeoFurComponentSceneProxy::ControlPointVertexType> ControlPointVertexBuffers_CPU[2];
	int32 ControlPointVertexBufferFrame;

	// Ready-to-render data with shells.
	FNeoFurVertexFactory VertexFactories[2];
	FVertexBuffer PostAnimationVertexBuffer; // No CPU version needed (write-only).
	FIndexBuffer  PostAnimationIndexBuffer;
	
	FVertexBuffer BoneMatsVertexBuffer;
	FVertexBuffer MorphDataVertexBuffer;

	bool bForceSimulateOnCPU;
	bool bSkipRendering;
	bool bSkipSimulation;
	
	float LastDeltaTime;

	FUnorderedAccessViewRHIRef ControlPointsUAV[2];
	FShaderResourceViewRHIRef SkeletalMeshSRV;
	FShaderResourceViewRHIRef ControlPointsSRV[2];
	FShaderResourceViewRHIRef PhysicsPropertiesSRV;
	FShaderResourceViewRHIRef MorphDataSRV;
	FVertexBufferRHIRef PhysicsPropertiesVertBuffer;
	FUnorderedAccessViewRHIRef PostAnimVertexBufferUAV;
	FShaderResourceViewRHIRef BoneMatsSRV;

	FShaderResourceViewRHIRef PerFrameDataVertBufferSRV;
    FVertexBufferRHIRef PerFrameDataVertBuffer;

	uint32 MemoryUsage;

    float VisibleLengthScale;

    FNeoFurProxyPrivateData()
#if !NEOFUR_NO_COMPUTE_SHADERS
        : ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5))  // FIXME: Might need the actual feature level of the machine here?
#endif
    {
    }
};





