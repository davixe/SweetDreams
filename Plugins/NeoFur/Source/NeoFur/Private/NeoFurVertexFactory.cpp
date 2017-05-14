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

#include "NeoFurVertexFactory.h"
#include "NeoFurComponentSceneProxy.h"
#include "NeoFurProxyPrivateData.h"

#include "GlobalShader.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "RHICommandList.h"
#include "ShaderParameterUtils.h"

#include "NeoFurShaderInstallCheck.h"

// Need this for some #ifdefs based on engine versions.
#include "Modules/ModuleVersion.h"

class FNeoFurVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
public:
    virtual void Bind(const FShaderParameterMap &ParameterMap) override;
    virtual void Serialize(FArchive &Ar) override;
    virtual void SetMesh(
        FRHICommandList& RHICmdList,
        FShader *Shader,
        const FVertexFactory *VertexFactory,
        const FSceneView& View,
        const FMeshBatchElement &BatchElement,
        uint32 DataFlags) const override;
    virtual uint32 GetSize() const override;
    
    FShaderParameter ShellCount;
    FShaderParameter VertsPerShell;
	FShaderParameter ActiveShellCount;

	FShaderParameter Bendiness;
	FShaderParameter ShellFade;
	FShaderParameter VisibleLengthScale;
	FShaderParameter ShellDistance;

	FShaderParameter NormalDirectionBlend;
};

void FNeoFurVertexFactoryShaderParameters::Bind(const FShaderParameterMap &ParameterMap)
{
    ShellCount.Bind(ParameterMap, TEXT("ShellCount"));
	ActiveShellCount.Bind(ParameterMap, TEXT("ActiveShellCount"));
    VertsPerShell.Bind(ParameterMap, TEXT("VertsPerShell"));

	Bendiness.Bind(ParameterMap, TEXT("Bendiness"));
	ShellFade.Bind(ParameterMap, TEXT("ShellFade"));
	VisibleLengthScale.Bind(ParameterMap, TEXT("VisibleLengthScale"));
	ShellDistance.Bind(ParameterMap, TEXT("ShellDistance"));

	NormalDirectionBlend.Bind(ParameterMap, TEXT("NormalDirectionBlend"));
}

void FNeoFurVertexFactoryShaderParameters::Serialize(FArchive &Ar)
{
    Ar << ShellCount;
    Ar << VertsPerShell;
	Ar << ActiveShellCount;

	Ar << Bendiness;
	Ar << ShellFade;
	Ar << VisibleLengthScale;
	Ar << ShellDistance;
	
	Ar << NormalDirectionBlend;
}

void FNeoFurVertexFactoryShaderParameters::SetMesh(
    FRHICommandList &RHICmdList,
    FShader *Shader,
    const FVertexFactory *VertexFactory,
    const FSceneView& View,
    const FMeshBatchElement &BatchElement,
    uint32 DataFlags) const
{
    FRHIVertexShader* ShaderRHI = Shader->GetVertexShader();
    if(ShaderRHI) {

        FNeoFurProxyPrivateData *UserData = (FNeoFurProxyPrivateData*)BatchElement.UserData;
        
        SetShaderValue(RHICmdList, ShaderRHI, ShellCount, UserData->ShellCount);
        SetShaderValue(RHICmdList, ShaderRHI, ActiveShellCount, UserData->ActiveShellCount);

		SetShaderValue(RHICmdList, ShaderRHI, Bendiness, UserData->PhysicsParameters.Bendiness);
		SetShaderValue(RHICmdList, ShaderRHI, ShellFade, UserData->ShellFade);
		SetShaderValue(RHICmdList, ShaderRHI, VisibleLengthScale, UserData->VisibleLengthScale);
		SetShaderValue(RHICmdList, ShaderRHI, ShellDistance, UserData->ShellDistance);

		SetShaderValue(RHICmdList, ShaderRHI, NormalDirectionBlend, UserData->PhysicsParameters.NormalDirectionBlend);

        int32 NumVertsPerLayer =
            (UserData->StaticVertexData_GPU.VertexBufferRHI->GetSize() /
                sizeof(FNeoFurComponentSceneProxy::VertexType_Static)) /
			UserData->ShellCount;

        SetShaderValue(RHICmdList, ShaderRHI, VertsPerShell, NumVertsPerLayer);
    }
}

uint32 FNeoFurVertexFactoryShaderParameters::GetSize() const
{
    return sizeof(FNeoFurVertexFactoryShaderParameters);
}

// ----------------------------------------------------------------------

FVertexFactoryShaderParameters *FNeoFurVertexFactory::ConstructShaderParameters(
    EShaderFrequency ShaderFrequency)
{
	if(ShaderFrequency != SF_Vertex) return nullptr;
    return new FNeoFurVertexFactoryShaderParameters();
}

FNeoFurVertexFactory::FNeoFurVertexFactory()
{
	VertexBuffer_Static = nullptr;
	VertexBuffer_ControlPoints_New = nullptr;
	VertexBuffer_ControlPoints_Old = nullptr;
}

void FNeoFurVertexFactory::SetFurVertexBuffer(
	FVertexBuffer *NewStaticVertexBuffer,
	FVertexBuffer *NewVertexBuffer_ControlPoints_Old,
	FVertexBuffer *NewVertexBuffer_ControlPoints_New)
{
	this->VertexBuffer_Static = NewStaticVertexBuffer;
	this->VertexBuffer_ControlPoints_New = NewVertexBuffer_ControlPoints_New;
	this->VertexBuffer_ControlPoints_Old = NewVertexBuffer_ControlPoints_Old;
	UpdateRHI();
}

void FNeoFurVertexFactory::InitRHI()
{
	FVertexDeclarationElementList Elements;

	// Attribute IDs here are from the FVertexFactoryInput structure
	// in LocalVertexFactory.usf.
	
	// Note: We don't do instancing with the fur, so we can probably
	// hijack attributes 8-12 for our own purposes. Hopefully Epic
	// doesn't change the LocalVertexFactory vertex format very much,
	// or we'll have to modify this to compensate for it.

	// Root (skinned) position (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_New,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, RootPosition),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 0));

	// Control point position (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_New,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, Position),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 8));
				
	// Previous root (skinned) position (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_Old,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, RootPosition),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 9));

	// Previous control point position (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_Old,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, Position),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 10));

	// Skinned spline (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_New,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, SkinnedSplineDirection),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 11));
				
	// Skinned spline (old) (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_Old,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, SkinnedSplineDirection),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 12));

	// Tangents (static).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_Static,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::VertexType_Static, TanX),
				sizeof(FNeoFurComponentSceneProxy::VertexType_Static), VET_Float3), 1));
				
	// Skinned normals (dynamic).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_ControlPoints_New,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::ControlPointVertexType, SkinnedNormal),
				sizeof(FNeoFurComponentSceneProxy::ControlPointVertexType), VET_Float3), 2));

	// Vertex ID fallback for mobile (static).
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_Static,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::VertexType_Static, VertexID),
				sizeof(FNeoFurComponentSceneProxy::VertexType_Static), VET_Float1), 13));

	// Colors (FIXME: Not currently supported)
	Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color), 3));
	#if ENGINE_MINOR_VERSION >= 11
	ColorStreamIndex = Elements.Last().StreamIndex;
	#endif

	// Texture coordinates (static)
	for(int32 i = 0; i < MAX_TEXCOORDS; i++) {
		int32 BaseAttribIndex = 4;
		Elements.Add(
			AccessStreamComponent(
				FVertexStreamComponent(
					VertexBuffer_Static,
					STRUCT_OFFSET(FNeoFurComponentSceneProxy::VertexType_Static, UVs[i]),
					sizeof(FNeoFurComponentSceneProxy::VertexType_Static), VET_Float2),
				BaseAttribIndex + i));
	}
	
	// Texture coordinates (light map).
	// FIXME: Support actual light maps?
	Elements.Add(
		AccessStreamComponent(
			FVertexStreamComponent(
				VertexBuffer_Static,
				STRUCT_OFFSET(FNeoFurComponentSceneProxy::VertexType_Static, UVs[0]),
				sizeof(FNeoFurComponentSceneProxy::VertexType_Static), VET_Float2),
			15));

	// FIXME: Might break on 4.12? The Data parameter might be gone.
  #if ENGINE_MINOR_VERSION < 12
	InitDeclaration(Elements, Data);
  #else
	InitDeclaration(Elements);
  #endif
}

bool FNeoFurVertexFactory::ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
	return true;
}

IMPLEMENT_VERTEX_FACTORY_TYPE(
	FNeoFurVertexFactory, "NeoFurVertexFactory",
	NeoFurRunShaderCheck_HijackBoolParameter(true), // Ugly hack alert
	false,true,false,false);
	

	


