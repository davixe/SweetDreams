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

// ----------------------------------------------------------------------
// Custom vertex factory type
// ----------------------------------------------------------------------

class FNeoFurVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FNeoFurVertexFactory);
public:

	FNeoFurVertexFactory();
	
	virtual void InitRHI() override;
	static FVertexFactoryShaderParameters *ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	void SetFurVertexBuffer(
		FVertexBuffer *NewStaticVertexBuffer,
		FVertexBuffer *NewVertexBuffer_ControlPoints_Old,
		FVertexBuffer *NewVertexBuffer_ControlPoints_New);

	static bool ShouldCache(
		EShaderPlatform Platform,
		const class FMaterial* Material,
		const class FShaderType* ShaderType);

private:

	FVertexBuffer *VertexBuffer_Static;
	FVertexBuffer *VertexBuffer_ControlPoints_Old;
	FVertexBuffer *VertexBuffer_ControlPoints_New;
};

#if ENGINE_MINOR_VERSION >= 13
// FIXME: In the preview version of 4.13 at this moment (8/18/2016)
// we're having a linker error with references to
// UMorphTarget::GetMorphTargetDelta(), but all the data it pulls out
// is public, so we'll just grab it directly.
inline FMorphTargetDelta *GetDeltaFromMorphTarget(UMorphTarget *MorphTarget, int32 LODIndex, int32 &OutNumDeltas)
{
	if(LODIndex >= MorphTarget->MorphLODModels.Num()) return nullptr;
	OutNumDeltas = MorphTarget->MorphLODModels[LODIndex].Vertices.Num();
	return MorphTarget->MorphLODModels[LODIndex].Vertices.GetData();
}
#endif





