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

#include "LocalVertexFactory.h"
#include "GlobalShader.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "RHICommandList.h"
#include "ShaderParameterUtils.h"

#include "NeoFurComponent.h"
#include "NeoFurComponentSceneProxy.h"
#include "NeoFurAsset.h"
#include "NeoFurShaderInstallCheck.h"
#include "NeoFurVertexFactory.h"
#include "NeoFurProxyPrivateData.h"

#include "NeoFurProfiling.h"

#include "RHICommandList.h"

// Need this for some #ifdefs based on engine versions.
#include "Modules/ModuleVersion.h"

// ---------------------------------------------------------------------------
// Resource setup/teardown
// ---------------------------------------------------------------------------

FNeoFurComponentSceneProxy::FNeoFurComponentSceneProxy(
	const UPrimitiveComponent* InComponent,
	FMaterialRenderProxy *InMaterialRenderProxy,
	int32 InShellCount,
	float InShellDistance,
	UNeoFurAsset *FurAsset,
	bool bForceCPU,
	bool bSkipSim,
	bool bSkipRender,
	FName ResourceName,
	ERHIFeatureLevel::Type FeatureLevel) :
	FPrimitiveSceneProxy(InComponent, ResourceName)
{
	ReadyToRender = false;
	
	for(int32 i = 0; i < NeoFurFrameTimeSmoothingFrames; i++) {
		PreviousFrameTimes[i] = -1.0f;
	}
	FrameTimeIndex = 0;

	bAlwaysHasVelocity = true;

    PrivateData = new FNeoFurProxyPrivateData;

	PrivateData->MemoryUsage = sizeof(*this);

	check(IsInGameThread());

    PrivateData->bForceSimulateOnCPU = bForceCPU;
    PrivateData->bSkipRendering      = bSkipRender;
    PrivateData->bSkipSimulation     = bSkipSim;
    PrivateData->ShellCount          = InShellCount;
    PrivateData->ActiveShellCount    = InShellCount;
	PrivateData->LastSimulatedActiveShellCount[0] = InShellCount;
	PrivateData->LastSimulatedActiveShellCount[1] = InShellCount;
    PrivateData->ShellDistance       = InShellDistance;
    
	PrivateData->MaterialRenderProxy = InMaterialRenderProxy;

	// We need to copy over skinned mesh data immediately (from the data passed
	// into THIS function, which might not be available later), so we're not
	// going to wait for the CreateRenderThreadResources() call.
	BeginInitResource(&PrivateData->SkeletalMeshVertexBufferCopy_GPU);
	BeginInitResource(&PrivateData->SkeletalMeshIndexBufferCopy);
	
	BeginInitResource(&PrivateData->ControlPointVertexBuffers_GPU[0]);
	BeginInitResource(&PrivateData->ControlPointVertexBuffers_GPU[1]);

	BeginInitResource(&PrivateData->PostAnimationVertexBuffer);
	BeginInitResource(&PrivateData->PostAnimationIndexBuffer);

	BeginInitResource(&PrivateData->BoneMatsVertexBuffer);
	BeginInitResource(&PrivateData->MorphDataVertexBuffer);

	BeginInitResource(&PrivateData->StaticVertexData_GPU);

	PrivateData->ControlPointVertexBufferFrame = 0;
	PrivateData->LastDeltaTime = 0.0167f;

    PrivateData->ControlPointsUAV[0]         = nullptr;
    PrivateData->ControlPointsUAV[1]         = nullptr;
    PrivateData->ControlPointsSRV[0]         = nullptr;
    PrivateData->ControlPointsSRV[1]         = nullptr;
    PrivateData->SkeletalMeshSRV             = nullptr;
    PrivateData->PhysicsPropertiesSRV        = nullptr;
    PrivateData->MorphDataSRV                = nullptr;
    PrivateData->PhysicsPropertiesVertBuffer = nullptr;
    PrivateData->PostAnimVertexBufferUAV     = nullptr;
    PrivateData->BoneMatsSRV                 = nullptr;
    PrivateData->PerFrameDataVertBuffer      = nullptr;
    PrivateData->PerFrameDataVertBufferSRV   = nullptr;
    
	memset(&PrivateData->PhysicsFrameInputs, 0, sizeof(PrivateData->PhysicsFrameInputs));

	PrivateData->ShellFade = 1.0f;
    PrivateData->VisibleLengthScale = 1.0f;

	GenerateBuffers(FurAsset);
}

FNeoFurComponentSceneProxy::~FNeoFurComponentSceneProxy()
{
	check(IsInRenderingThread());
	
	PrivateData->SkeletalMeshVertexBufferCopy_GPU.ReleaseResource();
	PrivateData->SkeletalMeshIndexBufferCopy.ReleaseResource();

	PrivateData->ControlPointVertexBuffers_GPU[0].ReleaseResource();
	PrivateData->ControlPointVertexBuffers_GPU[1].ReleaseResource();

	PrivateData->VertexFactories[0].ReleaseResource();
	PrivateData->VertexFactories[1].ReleaseResource();
	PrivateData->PostAnimationIndexBuffer.ReleaseResource();
	PrivateData->PostAnimationVertexBuffer.ReleaseResource();
	
	PrivateData->BoneMatsVertexBuffer.ReleaseResource();
	PrivateData->MorphDataVertexBuffer.ReleaseResource();

	PrivateData->StaticVertexData_GPU.ReleaseResource();
	
    delete PrivateData;
}

struct FNeoFurComponentSceneProxyGenerateCommand
{
	TArray<FNeoFurComponentSceneProxy::VertexType> AssetVertices;
	TArray<uint32> AssetIndices;
};

void FNeoFurComponentSceneProxy::GenerateBuffers(UNeoFurAsset *FurAsset)
{
	check(IsInGameThread());

	// We're copying all the bits of data we need here from the other
	// component, because we aren't allowed to access and of it from
	// the render thread except for the things handled through the RHI
	// refs.
	FNeoFurComponentSceneProxyGenerateCommand *NewCommand = new FNeoFurComponentSceneProxyGenerateCommand;
	NewCommand->AssetVertices = FurAsset->Vertices;
	NewCommand->AssetIndices  = FurAsset->Indices;

	check(FurAsset->Vertices.Num());
	check(FurAsset->Indices.Num());

	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		FurComponentSetupSkin,
		FNeoFurComponentSceneProxyGenerateCommand *, Command, NewCommand,
		FNeoFurComponentSceneProxy *,                Proxy,   this,
		{
			Proxy->GenerateBuffers_Renderthread_CPU(RHICmdList, Command);
			delete Command;
		}
	);
}

template<typename T>
void *NeoFurFillVertexBuffer(
	const TArray<T> &SourceVerts,
	FVertexBufferRHIRef &VertexBuffer,
	uint32 Duplications = 1,
	bool bLeaveLockedForTouchups = false)
{
	NEOFUR_PROFILE_SCOPE("NeoFurFillVertexBuffer");
	
	check(SourceVerts.Num() * sizeof(T) * Duplications == VertexBuffer->GetSize());

	void *RawStaticVerts = nullptr;
	
	{
		NEOFUR_PROFILE_SCOPE("Lock");
		RawStaticVerts = RHILockVertexBuffer(
			VertexBuffer, 0,
			VertexBuffer->GetSize(),
			RLM_WriteOnly);
	}

	{
		NEOFUR_PROFILE_SCOPE("Fill");
		for(uint32 i = 0; i < Duplications; i++) {
			memcpy(
				(char*)RawStaticVerts + (SourceVerts.Num() * sizeof(T)) * i,
				&SourceVerts[0],
				SourceVerts.Num() * sizeof(T));
		}
	}
	
	if(bLeaveLockedForTouchups) {
		return RawStaticVerts;
	}

	{
		NEOFUR_PROFILE_SCOPE("Unlock");
		RHIUnlockVertexBuffer(VertexBuffer);
	}
	
	return nullptr;
}

template<typename T>
FVertexBufferRHIRef NeoFurVertexBufferFromArray(
	const TArray<T> &SourceVerts,
	uint32 ExtraUsageFlags = 0,
	uint32 Duplications = 1,
	bool bLeaveLockedForTouchups = false,
	void **RawVertsForTouchups = nullptr)
{
	NEOFUR_PROFILE_SCOPE("NeoFurVertexBufferFromArray");

	FVertexBufferRHIRef ret;
	FRHIResourceCreateInfo CreateInfo;

	ret = RHICreateVertexBuffer(
		SourceVerts.Num() * sizeof(T) * Duplications,
		BUF_Static | BUF_ShaderResource | ExtraUsageFlags, CreateInfo);

	void *RawVerts = NeoFurFillVertexBuffer(SourceVerts, ret, Duplications, bLeaveLockedForTouchups);

	if(RawVertsForTouchups) {
		*RawVertsForTouchups = RawVerts;
	}

	return ret;
}

void FNeoFurComponentSceneProxy::GenerateBuffers_Renderthread_CPU(
	FRHICommandListImmediate &RHICmdList,
	FNeoFurComponentSceneProxyGenerateCommand *Command)
{
	double startTime = FPlatformTime::Seconds();
	UE_LOG(NeoFur, Log, TEXT("Generating buffers start"));
	
	{
		NEOFUR_PROFILE_SCOPE("GenerateBuffers_Renderthread_CPU");

		check(IsInRenderingThread());
		int32 NumSourceVerts = Command->AssetVertices.Num();
		FRHIResourceCreateInfo CreateInfo;
		
		// Memory usage for static mesh copy.
		PrivateData->MemoryUsage += NumSourceVerts * sizeof(VertexType);

		// Memory usage for control points.
		PrivateData->MemoryUsage += NumSourceVerts * sizeof(ControlPointVertexType);

		// Memory usage for renderable shell mesh.
		PrivateData->MemoryUsage += NumSourceVerts * sizeof(VertexType) * PrivateData->ShellCount;

		// Copy vertices over.
		{
			NEOFUR_PROFILE_SCOPE("Copy vertices");

			PrivateData->SkeletalMeshVertexBufferCopy_CPU.SetNumZeroed(NumSourceVerts);
			PrivateData->ControlPointVertexBuffers_CPU[0].SetNumZeroed(NumSourceVerts);
			PrivateData->ControlPointVertexBuffers_CPU[1].SetNumZeroed(NumSourceVerts);
			PrivateData->StaticVertexData_CPU.SetNumZeroed(NumSourceVerts);

			{
				NEOFUR_PROFILE_SCOPE("Iterate through source verts");
				
				for (int32 i = 0; i < NumSourceVerts; i++) {

					// Mesh
					VertexType *CopiedVert = &PrivateData->SkeletalMeshVertexBufferCopy_CPU[i];
					*CopiedVert = Command->AssetVertices[i];

					// Static verts
					VertexType_Static *StaticVert = &PrivateData->StaticVertexData_CPU[i];
					StaticVert->Position = Command->AssetVertices[i].Position;
					StaticVert->TanZ = Command->AssetVertices[i].TanZ;
					StaticVert->TanX = Command->AssetVertices[i].TanX;
					StaticVert->CombedDirectionAndLength = Command->AssetVertices[i].CombedDirectionAndLength;
					for(int32 k = 0; k < 4; k++) {
						StaticVert->InfluenceBones[k] = Command->AssetVertices[i].InfluenceBones[k];
						StaticVert->InfluenceWeights[k] = Command->AssetVertices[i].InfluenceWeights[k];
					}
					for(int32 k = 0; k < MAX_TEXCOORDS; k++) {
						StaticVert->UVs[k] = Command->AssetVertices[i].UVs[k];
					}
					
					// Control points
					for (int32 j = 0; j < 2; j++) {
						ControlPointVertexType *ControlPoint = nullptr;
						ControlPoint = &(PrivateData->ControlPointVertexBuffers_CPU[j][i]);
						ControlPoint->Position     = CopiedVert->Position + FVector(CopiedVert->TanZ) * PrivateData->ShellDistance;
						ControlPoint->RootPosition = CopiedVert->Position;
						ControlPoint->Velocity     = FVector(0.0f, 0.0f, 0.0f);
					}

				}
			}

			// Copy the static data buffer to the GPU, because we'll need
			// it in the place that the simulation happens in, and also
			// for the vertex factory to read from. We need to expand this
			// out to the entire mesh, including shells, so it's not
			// actually a mirror of the CPU-side vertex buffer!
			{
				NEOFUR_PROFILE_SCOPE("Static buffer to GPU");
				
				void *RawVerts = nullptr;

				// Generate vertex buffer.
				PrivateData->StaticVertexData_GPU.VertexBufferRHI =
					NeoFurVertexBufferFromArray(
						PrivateData->StaticVertexData_CPU,
						0,
						PrivateData->ShellCount,
						true,
						&RawVerts);
						

				// Touch up Vertex IDs now that everthing is in place.
				FNeoFurComponentSceneProxy::VertexType_Static *BufferVerts =
					(FNeoFurComponentSceneProxy::VertexType_Static *)RawVerts;
				check(BufferVerts);
				int32 TotalVertCount = PrivateData->StaticVertexData_CPU.Num() * PrivateData->ShellCount;
				for(int32 i = 0; i < TotalVertCount; i++) {
					BufferVerts[i].VertexID = float(i);
				}

				RHIUnlockVertexBuffer(PrivateData->StaticVertexData_GPU.VertexBufferRHI);
			}

			{
				NEOFUR_PROFILE_SCOPE("Create control point buffers");
				
				// Control points.
				for (int32 i = 0; i < 2; i++) {
					PrivateData->ControlPointVertexBuffers_GPU[i].VertexBufferRHI = NeoFurVertexBufferFromArray(
						PrivateData->ControlPointVertexBuffers_CPU[i],
						BUF_UnorderedAccess,
						PrivateData->ShellCount);
				}
			}

			// Move everything over to Vertex buffers if we're using the
			// GPU simulation. Clear out CPU-side buffers we don't need.
			if(ShouldUseGPUShader()) {
			    
				NEOFUR_PROFILE_SCOPE("Copy skeletal mesh VBO copy to GPU and clear CPU buffers");

				// Source skeletal mesh.
				PrivateData->SkeletalMeshVertexBufferCopy_GPU.VertexBufferRHI = NeoFurVertexBufferFromArray(
					PrivateData->SkeletalMeshVertexBufferCopy_CPU);
					
				// We don't need the static vertex data on the CPU side.
				PrivateData->StaticVertexData_CPU.SetNum(0, true);

				// We don't need the CPU-side control point array for the
				// GPU shader.
				PrivateData->ControlPointVertexBuffers_CPU[0].SetNum(0, true);
				PrivateData->ControlPointVertexBuffers_CPU[1].SetNum(0, true);
			}
			
			{
				NEOFUR_PROFILE_SCOPE("Generate initial shells");

				// Generate shells in our post animation vertex buffer.
				// Animated, renderable thing. (Changes every frame.)
				PrivateData->PostAnimationVertexBuffer.VertexBufferRHI = RHICreateVertexBuffer(
					NumSourceVerts * sizeof(VertexType) * PrivateData->ShellCount,
					BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo); // FIXME: Use BUF_Dynamic on ES2?
				void *RawPostAnimVerts = RHILockVertexBuffer(
					PrivateData->PostAnimationVertexBuffer.VertexBufferRHI, 0,
					PrivateData->PostAnimationVertexBuffer.VertexBufferRHI->GetSize(), RLM_WriteOnly);
				{
					for (int32 i = 0; i < NumSourceVerts; i++) {

						for (int32 j = 0; j < PrivateData->ShellCount; j++) {

							// FIXME: Fix these variable names.
							VertexType *SourceVertex = &(Command->AssetVertices[i]);
							VertexType *ShellVert = (VertexType*)RawPostAnimVerts + i + (j * NumSourceVerts);

							// FIXME: Squish this down to a direct copy, then
							// alter the position. Then make sure it's still
							// performant.

							ShellVert->Position = SourceVertex->Position;
							ShellVert->Position += FVector(SourceVertex->TanZ) * (float(j) / float(PrivateData->ShellCount)) * PrivateData->ShellDistance;
							
							ShellVert->TanX     = SourceVertex->TanX;
							ShellVert->TanZ     = SourceVertex->TanZ;
							for (int32 k = 0; k < MAX_TEXCOORDS - 1; k++) {
								ShellVert->UVs[k] = SourceVertex->UVs[k];
							}

							// FIXME: Do we even care about bones for the rendered verts?
							// FIXME: Support 4 or 8 bones!
							for (int32 k = 0; k < 4; k++) {
								ShellVert->InfluenceBones[k]   = SourceVertex->InfluenceBones[k];
								ShellVert->InfluenceWeights[k] = SourceVertex->InfluenceWeights[k];
							}
							
						}
					}
				}
				RHIUnlockVertexBuffer(PrivateData->PostAnimationVertexBuffer.VertexBufferRHI);
			}
		}

		{
			NEOFUR_PROFILE_SCOPE("Make bone mats VBO and morph data VBO");
			// ---

			// FIXME: Get actual number of bones instead of just the max (256).
			PrivateData->BoneMatsVertexBuffer.VertexBufferRHI = RHICreateVertexBuffer(
				sizeof(FMatrix) * NEOFUR_MAX_BONE_COUNT, BUF_Dynamic | BUF_ShaderResource, CreateInfo);
			PrivateData->MemoryUsage += sizeof(FMatrix) * NEOFUR_MAX_BONE_COUNT;

			// ---

			PrivateData->MorphDataVertexBuffer.VertexBufferRHI = RHICreateVertexBuffer(
				NumSourceVerts * sizeof(MorphDataVertexType),
				BUF_Dynamic | BUF_ShaderResource, CreateInfo);
			PrivateData->MemoryUsage += sizeof(MorphDataVertexType) * NumSourceVerts;
		}

		// ---
		
		{
			NEOFUR_PROFILE_SCOPE("Copy indices");

			// Copy indices over.
			int32 NumSourceIndices = Command->AssetIndices.Num();

			// Make our two buffers for the skinned mesh copy and rendering copy with shells.
			PrivateData->SkeletalMeshIndexBufferCopy.IndexBufferRHI = RHICreateIndexBuffer(
				sizeof(uint32), sizeof(uint32) * NumSourceIndices, BUF_Static, CreateInfo);
			PrivateData->MemoryUsage += sizeof(uint32) * NumSourceIndices;
			
			PrivateData->PostAnimationIndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(
				sizeof(uint32), sizeof(uint32) * NumSourceIndices * PrivateData->ShellCount, BUF_Static, CreateInfo);
			PrivateData->MemoryUsage += sizeof(uint32) * NumSourceIndices * PrivateData->ShellCount;

			// Lock skeletal mesh copy index buffer (for skinned anim copying).
			void *RawIndicies = RHILockIndexBuffer(
				PrivateData->SkeletalMeshIndexBufferCopy.IndexBufferRHI,
				0, NumSourceIndices * sizeof(uint32),
				RLM_WriteOnly);

			// Lock post-animation index buffer (for rendering).
			void *RawPostAnimIndicies = RHILockIndexBuffer(
				PrivateData->PostAnimationIndexBuffer.IndexBufferRHI,
				0, NumSourceIndices * sizeof(uint32) * PrivateData->ShellCount,
				RLM_WriteOnly);
				
			for (int32 i = 0; i < NumSourceIndices; i++) {

				uint32 OriginalIndex = 0;
				
				OriginalIndex = Command->AssetIndices[i];

				// Write our skeletal mesh copy vert.
				uint32 *WriteIndexOriginal = &(((uint32*)RawIndicies)[i]);
				*WriteIndexOriginal = OriginalIndex;

				// Write our shell verts.
				for (int32 k = 0; k < PrivateData->ShellCount; k++) {
					uint32 *WriteIndexShells = &(((uint32*)RawPostAnimIndicies)[i + k * NumSourceIndices]);
					*WriteIndexShells = OriginalIndex + (k * NumSourceVerts);
				}
			}

			RHIUnlockIndexBuffer(PrivateData->SkeletalMeshIndexBufferCopy.IndexBufferRHI);
			RHIUnlockIndexBuffer(PrivateData->PostAnimationIndexBuffer.IndexBufferRHI);
		}
	}
	
	double endTime = FPlatformTime::Seconds();
	UE_LOG(NeoFur, Log, TEXT("Generating buffers complete: %f ms"), float(endTime - startTime) * 1000.0f);
}

void FNeoFurComponentSceneProxy::CreateRenderThreadResources()
{
	check(IsInRenderingThread());

    PrivateData->VertexFactories[0].SetFurVertexBuffer(
		&PrivateData->StaticVertexData_GPU,
		&PrivateData->ControlPointVertexBuffers_GPU[0],
		&PrivateData->ControlPointVertexBuffers_GPU[1]);
		
    PrivateData->VertexFactories[0].InitResource();

	PrivateData->VertexFactories[1].SetFurVertexBuffer(
		&PrivateData->StaticVertexData_GPU,
		&PrivateData->ControlPointVertexBuffers_GPU[1],
		&PrivateData->ControlPointVertexBuffers_GPU[0]);
		
    PrivateData->VertexFactories[1].InitResource();
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

struct FurComponentRunSimParams
{
	FNeoFurComponentSceneProxy * Proxy;
	FTransform RelativeTransformSinceLastFrame;
	float DeltaTime;
	FVector LocalSpaceGravity;
	FMatrix *BoneMats;
	int32 NumBoneMats;
	FNeoFurComponentSceneProxy::MorphDataVertexType *IncomingMorphData;
};

void FNeoFurComponentSceneProxy::RunSimulation(
	const FTransform &RelativeTransformSinceLastFrame,
	float DeltaTime,
	const FVector &LocalSpaceGravity,
	FMatrix *BoneMats, int32 NumBoneMats,
	MorphDataVertexType *IncomingMorphData)
{
	check(IsInGameThread());

	if(NumBoneMats > NEOFUR_MAX_BONE_COUNT) {
		// FIXME: Maybe find a better way to deliver this error message. This
		// will probably just result in horrifying levels of log spam.
		UE_LOG(NeoFur, Error, TEXT(
			"Cannot simulate fur with more than %d bones. Clamping bone count. This will probably look horribly wrong."), NEOFUR_MAX_BONE_COUNT);
		NumBoneMats = NEOFUR_MAX_BONE_COUNT;
	}
	
	// Frame time spikes make the simulation "twitch" erratically, so
	// we need to smooth out the incoming delta times by averaging it
	// with the last several frames.
	PreviousFrameTimes[FrameTimeIndex] = DeltaTime;
	float DeltaTimeToUse = 0.0f;
	int32 FramesCounted = 0;
	for(int32 i = 0; i < NeoFurFrameTimeSmoothingFrames; i++) {
		if(PreviousFrameTimes[i] != -1.0f) {
			FramesCounted++;
			DeltaTimeToUse += PreviousFrameTimes[i];
		}
	}
	if(FramesCounted) {
		DeltaTimeToUse /= float(FramesCounted);
	} else {
		DeltaTimeToUse = DeltaTime;
	}
	FrameTimeIndex++;
	FrameTimeIndex %= NeoFurFrameTimeSmoothingFrames;


	FurComponentRunSimParams *params = new FurComponentRunSimParams;
	params->Proxy = this;
	params->RelativeTransformSinceLastFrame = RelativeTransformSinceLastFrame;
	params->DeltaTime = DeltaTimeToUse;
	params->LocalSpaceGravity = LocalSpaceGravity;
	params->BoneMats = BoneMats;
	params->NumBoneMats = NumBoneMats;
	params->IncomingMorphData = IncomingMorphData;

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		FurComponentRunSimulation,
		FurComponentRunSimParams *, params, params,
		{
			params->Proxy->RunSimulation_Renderthread(
				RHICmdList,
				params->RelativeTransformSinceLastFrame,
				params->DeltaTime,
				params->LocalSpaceGravity,
				params->BoneMats,
				params->NumBoneMats,
				params->IncomingMorphData);
			delete params;
		}
	);
}

bool FNeoFurComponentSceneProxy::ShouldUseGPUShader() const
{
	#if !NEOFUR_NO_COMPUTE_SHADERS
	return !GetForceSimulateOnCPU() && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
	#else
		return false;
	#endif
}

void FNeoFurComponentSceneProxy::RunSimulation_Renderthread(
	FRHICommandListImmediate &RHICmdList,
	const FTransform &RelativeTransformSinceLastFrame,
	float DeltaTime,
	const FVector &LocalSpaceGravity,
	FMatrix *BoneMats, int32 NumBoneMats,
	MorphDataVertexType *IncomingMorphData)
{
	// Bail out immediately if paused. Otherwise our sliding delta will make it
	// lerp into paused even though the rest of the world stops.
	if (DeltaTime == 0.0f) {
		return;
	}

	// FIXME: This is an ugly hack to work around some issues with
	// mismatched bone counts. We're just going to use the maximum
	// bone count for now, which is inefficient.
	FMatrix *BoneMatsTmp = new FMatrix[NEOFUR_MAX_BONE_COUNT];
	for(int32 i = 0; i < NEOFUR_MAX_BONE_COUNT; i++) {
		if(i < NumBoneMats && BoneMats) {
			BoneMatsTmp[i] = BoneMats[i];
		} else {
			BoneMatsTmp[i].SetIdentity();
		}
	}

	// Slide the time delta slowly towards what the actual framerate is.
	const float MaxTimeDeltaDelta = 0.005f;
	float DeltaTimeDelta = PrivateData->LastDeltaTime - DeltaTime;
	if(fabs(DeltaTimeDelta) > MaxTimeDeltaDelta) {
		DeltaTime = DeltaTime + MaxTimeDeltaDelta * (DeltaTimeDelta < 0.0f ? -1.0f : 1.0f);
	}
	
	// Clamp our update time. If we're under 30 fps, we're already in bad
	// shape. At least we can keep the simulation stable.
	const float DeltaTimeMax = (16.667f * 2.0f) / 1000.0f;
	if (DeltaTime > DeltaTimeMax) {
		DeltaTime = DeltaTimeMax;
	}
	PrivateData->LastDeltaTime = DeltaTime;

#if !NEOFUR_NO_COMPUTE_SHADERS
	if(!ShouldUseGPUShader()) {
		RunSimulation_Renderthread_CPU(
			RelativeTransformSinceLastFrame,
			DeltaTime,
			LocalSpaceGravity,
			BoneMatsTmp,
			IncomingMorphData);
	} else {
		RunSimulation_Renderthread_GPU(
			RelativeTransformSinceLastFrame,
			DeltaTime,
			LocalSpaceGravity,
			BoneMatsTmp, NEOFUR_MAX_BONE_COUNT, // NumBoneMats,
			IncomingMorphData,
			RHICmdList);
	}
#else
	RunSimulation_Renderthread_CPU(
		RelativeTransformSinceLastFrame,
		DeltaTime,
		LocalSpaceGravity,
		BoneMatsTmp,
		IncomingMorphData);
#endif

	if (BoneMats) {
		delete[] BoneMats;
	}
	delete[] BoneMatsTmp;
	
	if(IncomingMorphData) {
		delete[] IncomingMorphData;
	}

	ReadyToRender = true;
}

uint32 FNeoFurComponentSceneProxy::GetControlPointCount() const
{
	return (PrivateData->ControlPointVertexBuffers_GPU->VertexBufferRHI->GetSize() / sizeof(ControlPointVertexType)) / PrivateData->ShellCount;
}

#if !NEOFUR_NO_COMPUTE_SHADERS
void FNeoFurComponentSceneProxy::RunSimulation_Renderthread_GPU(
	const FTransform &RelativeTransformSinceLastFrame,
	float DeltaTime,
	const FVector &LocalSpaceGravity,
	FMatrix *BoneMats, int32 NumBoneMats,
	MorphDataVertexType *IncomingMorphData,
	FRHICommandListImmediate &RHICmdList)
{
	if(PrivateData->bSkipSimulation) return;

	// FIXME: Any assets created in here should probably be created at init
	// time and NOT every frame. This includes "views" of resources.

	uint32 ControlPointCount = GetControlPointCount();

	RHICmdList.SetComputeShader(PrivateData->ComputeShader->GetComputeShader());

	// Read control point buffer
	if(PrivateData->ComputeShader->In_ControlPoints.IsBound()) {
	    
		// FIXME: Remove lazy init.
		if(!PrivateData->ControlPointsSRV[!PrivateData->ControlPointVertexBufferFrame]) {
			PrivateData->ControlPointsSRV[!PrivateData->ControlPointVertexBufferFrame] =
				RHICmdList.CreateShaderResourceView(PrivateData->ControlPointVertexBuffers_GPU[!PrivateData->ControlPointVertexBufferFrame].VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
		}

		RHICmdList.SetShaderResourceViewParameter(
			FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
			PrivateData->ComputeShader->In_ControlPoints.GetBaseIndex(),
			PrivateData->ControlPointsSRV[!PrivateData->ControlPointVertexBufferFrame]);
	}

	if(PrivateData->ComputeShader->In_OriginalMesh.IsBound()) {
	    
		// FIXME: Create this earlier instead of lazy init.
		if(!PrivateData->SkeletalMeshSRV) {
			PrivateData->SkeletalMeshSRV = RHICmdList.CreateShaderResourceView(
				PrivateData->SkeletalMeshVertexBufferCopy_GPU.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
		}
		RHICmdList.SetShaderResourceViewParameter(
			FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
			PrivateData->ComputeShader->In_OriginalMesh.GetBaseIndex(),
			PrivateData->SkeletalMeshSRV);
	}

	if(PrivateData->ComputeShader->In_BoneMatrices.IsBound()) {

		FMatrix *BoneMatBuf = nullptr;
		BoneMatBuf = (FMatrix *)RHILockVertexBuffer(
			PrivateData->BoneMatsVertexBuffer.VertexBufferRHI, 0,
			PrivateData->BoneMatsVertexBuffer.VertexBufferRHI->GetSize(), RLM_WriteOnly);
			
		if(BoneMats) {
			memcpy(BoneMatBuf, BoneMats, sizeof(FMatrix) * NumBoneMats);
		} else {
			for(int32 i = 0; i < NumBoneMats; i++) {
				BoneMatBuf[i] = FMatrix::Identity;
			}
		}

		RHIUnlockVertexBuffer(PrivateData->BoneMatsVertexBuffer.VertexBufferRHI);
		
		// FIXME: Remove lazy init.
		if(!PrivateData->BoneMatsSRV) {
			PrivateData->BoneMatsSRV = RHICmdList.CreateShaderResourceView(
				PrivateData->BoneMatsVertexBuffer.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
		}

		RHICmdList.SetShaderResourceViewParameter(
			FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
			PrivateData->ComputeShader->In_BoneMatrices.GetBaseIndex(),
			PrivateData->BoneMatsSRV);
	}

	if(PrivateData->ComputeShader->In_PhysicsProperties.IsBound()) {

		FNeoFurPhysicsParameters *PhysicsPropertiesBuf = nullptr;
		
		// FIXME: Create this once with BUF_Dynamic and rip out the lazy init.
		if(!PrivateData->PhysicsPropertiesVertBuffer) {
			FRHIResourceCreateInfo CreateInfo;
			PrivateData->PhysicsPropertiesVertBuffer = RHICreateVertexBuffer(sizeof(FNeoFurPhysicsParameters) + sizeof(FNeoFurFramePhysicsInputs), BUF_Static | BUF_ShaderResource, CreateInfo);
		}

		PhysicsPropertiesBuf = (FNeoFurPhysicsParameters *)RHILockVertexBuffer(
			PrivateData->PhysicsPropertiesVertBuffer, 0,
			sizeof(FNeoFurPhysicsParameters) + sizeof(FNeoFurFramePhysicsInputs),
			RLM_WriteOnly);
		memcpy(PhysicsPropertiesBuf, &PrivateData->PhysicsParameters, sizeof(FNeoFurPhysicsParameters));
		
		FNeoFurFramePhysicsInputs FrameInputs;
		memset(&FrameInputs, 0, sizeof(FrameInputs));
		memcpy(&FrameInputs, &PrivateData->PhysicsFrameInputs, sizeof(FrameInputs));

		memcpy((char*)PhysicsPropertiesBuf + sizeof(FNeoFurPhysicsParameters), &FrameInputs, sizeof(FNeoFurFramePhysicsInputs));
		RHIUnlockVertexBuffer(PrivateData->PhysicsPropertiesVertBuffer);
		
		// FIXME: Remove lazy init.
		if(!PrivateData->PhysicsPropertiesSRV) {
			PrivateData->PhysicsPropertiesSRV = RHICmdList.CreateShaderResourceView(
				PrivateData->PhysicsPropertiesVertBuffer, sizeof(float), PF_R32_FLOAT);
		}

		RHICmdList.SetShaderResourceViewParameter(
			FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
			PrivateData->ComputeShader->In_PhysicsProperties.GetBaseIndex(),
			PrivateData->PhysicsPropertiesSRV);
	}
	
    if(PrivateData->ComputeShader->In_PerFrameData.IsBound()) {

        if(!PrivateData->PerFrameDataVertBuffer) {
            FRHIResourceCreateInfo CreateInfo;
            PrivateData->PerFrameDataVertBuffer = RHICreateVertexBuffer(
                sizeof(FNeoFurComputeShaderPerFrameData), BUF_Static | BUF_ShaderResource, CreateInfo);
        }

        if(!PrivateData->PerFrameDataVertBufferSRV) {
            PrivateData->PerFrameDataVertBufferSRV = RHICmdList.CreateShaderResourceView(
                PrivateData->PerFrameDataVertBuffer, sizeof(float), PF_R32_FLOAT);
        }

        FNeoFurComputeShaderPerFrameData *perFrameData = (FNeoFurComputeShaderPerFrameData*)RHILockVertexBuffer(
            PrivateData->PerFrameDataVertBuffer, 0, sizeof(FNeoFurComputeShaderPerFrameData), RLM_WriteOnly);
        perFrameData->ControlPointCount      = ControlPointCount;
        perFrameData->ShellCount             = PrivateData->ActiveShellCount;
        perFrameData->TotalShellCount        = PrivateData->ShellCount;
        perFrameData->ShellDistance          = PrivateData->ShellDistance;
        perFrameData->DeltaTime              = DeltaTime;
        perFrameData->TransformFromLastFrame = RelativeTransformSinceLastFrame.ToMatrixWithScale();
        perFrameData->LocalSpaceGravity      = LocalSpaceGravity;
        perFrameData->ShellFade              = PrivateData->ShellFade;
        perFrameData->VisibleLengthScale     = PrivateData->VisibleLengthScale;
        RHIUnlockVertexBuffer(PrivateData->PerFrameDataVertBuffer);

        RHICmdList.SetShaderResourceViewParameter(
            FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
            PrivateData->ComputeShader->In_PerFrameData.GetBaseIndex(),
            PrivateData->PerFrameDataVertBufferSRV);
    }

	// Write control point buffer
	if(PrivateData->ComputeShader->Out_ControlPoints.IsBound()) {
	    
		// FIXME: Create this earlier instead of on-the-fly.
		if(!PrivateData->ControlPointsUAV[PrivateData->ControlPointVertexBufferFrame]) {
			PrivateData->ControlPointsUAV[PrivateData->ControlPointVertexBufferFrame] =
				RHICmdList.CreateUnorderedAccessView(PrivateData->ControlPointVertexBuffers_GPU[PrivateData->ControlPointVertexBufferFrame].VertexBufferRHI, PF_R32_FLOAT);
		}

      #if ENGINE_MINOR_VERSION >= 13
		RHICmdList.TransitionResource(
			EResourceTransitionAccess::ERWBarrier,
			EResourceTransitionPipeline::EGfxToCompute,
			PrivateData->ControlPointsUAV[PrivateData->ControlPointVertexBufferFrame]);
      #endif
      
		RHICmdList.SetUAVParameter(
			PrivateData->ComputeShader->GetComputeShader(),
			PrivateData->ComputeShader->Out_ControlPoints.GetBaseIndex(),
			PrivateData->ControlPointsUAV[PrivateData->ControlPointVertexBufferFrame]);
	}

	if(PrivateData->ComputeShader->In_MorphData.IsBound()) {
		// FIXME: Remove lazy init.
		if(!PrivateData->MorphDataSRV) {
			PrivateData->MorphDataSRV = RHICmdList.CreateShaderResourceView(
				PrivateData->MorphDataVertexBuffer.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
		}

		// FIXME: Only do this if morph data has actually changed.
		{
			MorphDataVertexType *MorphData = (MorphDataVertexType *)RHILockVertexBuffer(
				PrivateData->MorphDataVertexBuffer.VertexBufferRHI, 0,
				sizeof(MorphDataVertexType) * ControlPointCount,
				RLM_WriteOnly);

			if(IncomingMorphData) {
				memcpy(MorphData, IncomingMorphData, sizeof(MorphDataVertexType) * ControlPointCount);
			} else {
				memset(MorphData, 0, sizeof(MorphDataVertexType) * ControlPointCount);
			}

			RHIUnlockVertexBuffer(PrivateData->MorphDataVertexBuffer.VertexBufferRHI);
		}

		RHICmdList.SetShaderResourceViewParameter(
			FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
			PrivateData->ComputeShader->In_MorphData.GetBaseIndex(),
			PrivateData->MorphDataSRV);
	}
	
	FComputeShaderRHIParamRef ComputeShaderRHI = PrivateData->ComputeShader->GetComputeShader();

	DispatchComputeShader(RHICmdList, *PrivateData->ComputeShader, ControlPointCount, 1, 1);

	// Apparently it actually IS necessary to unbind your buffers or weird stuff starts happening.
	RHICmdList.SetUAVParameter(
		PrivateData->ComputeShader->GetComputeShader(),
		PrivateData->ComputeShader->Out_PostAnimVertexBuffer.GetBaseIndex(),
		nullptr);
	RHICmdList.SetUAVParameter(
		PrivateData->ComputeShader->GetComputeShader(),
		PrivateData->ComputeShader->Out_ControlPoints.GetBaseIndex(),
		nullptr);
	RHICmdList.SetShaderResourceViewParameter(
		FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
		PrivateData->ComputeShader->In_ControlPoints.GetBaseIndex(),
		nullptr);
	RHICmdList.SetShaderResourceViewParameter(
		FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
		PrivateData->ComputeShader->In_OriginalMesh.GetBaseIndex(),
		nullptr);
	RHICmdList.SetShaderResourceViewParameter(
		FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
		PrivateData->ComputeShader->In_BoneMatrices.GetBaseIndex(),
		nullptr);
	RHICmdList.SetShaderResourceViewParameter(
		FComputeShaderRHIParamRef(PrivateData->ComputeShader->GetComputeShader()),
		PrivateData->ComputeShader->In_PerFrameData.GetBaseIndex(),
		nullptr);
		
	PrivateData->LastSimulatedActiveShellCount[PrivateData->ControlPointVertexBufferFrame] = PrivateData->ActiveShellCount;

  #if ENGINE_MINOR_VERSION >= 13
	RHICmdList.TransitionResource(
		EResourceTransitionAccess::ERWBarrier,
		EResourceTransitionPipeline::EComputeToGfx,
		PrivateData->ControlPointsUAV[PrivateData->ControlPointVertexBufferFrame]);
  #endif

	// Swap in/out control points.
	PrivateData->ControlPointVertexBufferFrame = !PrivateData->ControlPointVertexBufferFrame;
}
#endif

// ---------------------------------------------------------------------------
// Actual rendering stuff starts here
// ---------------------------------------------------------------------------

FPrimitiveViewRelevance FNeoFurComponentSceneProxy::GetViewRelevance(const FSceneView* View)
{
	return ((const FNeoFurComponentSceneProxy*)this)->GetViewRelevance(View);
}

FPrimitiveViewRelevance FNeoFurComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDrawRelevance     = IsShown(View);
	Relevance.bShadowRelevance   = IsShadowCast(View);
	Relevance.bDynamicRelevance  = true;
	Relevance.bRenderCustomDepth = ShouldRenderCustomDepth();
	Relevance.bRenderInMainPass  = ShouldRenderInMainPass();
	return Relevance;
}

uint32 FNeoFurComponentSceneProxy::GetMemoryFootprint(void) const
{
	return GetAllocatedSize() + PrivateData->MemoryUsage;
}

void FNeoFurComponentSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	class FMeshElementCollector& Collector) const
{
	check(IsInRenderingThread());
	
	if(PrivateData->bSkipRendering) return;
	if(!PrivateData->MaterialRenderProxy) return;
	if(PrivateData->ActiveShellCount <= 0) return;

	if(!ReadyToRender) return;
	
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++) {
		if ((1 << ViewIndex) & VisibilityMap) {

			const FSceneView &View = *Views[ViewIndex];

            FMeshBatch &Batch = Collector.AllocateMesh();
            Batch.VertexFactory = &PrivateData->VertexFactories[PrivateData->ControlPointVertexBufferFrame];
            Batch.MaterialRenderProxy = PrivateData->MaterialRenderProxy;
            Batch.Type = PT_TriangleList;

            FMeshBatchElement &Element = Batch.Elements[0];
            
            Element.IndexBuffer = &PrivateData->PostAnimationIndexBuffer;
            int32 NumPrimitivesTotal = (PrivateData->PostAnimationIndexBuffer.IndexBufferRHI->GetSize() / PrivateData->PostAnimationIndexBuffer.IndexBufferRHI->GetStride()) / 3;
            int32 TrianglesPerShell = NumPrimitivesTotal / PrivateData->ShellCount;
            Element.NumPrimitives = TrianglesPerShell * FMath::Min3(
				PrivateData->ActiveShellCount,
				PrivateData->LastSimulatedActiveShellCount[0],
				PrivateData->LastSimulatedActiveShellCount[1]);

            Element.MinVertexIndex = 0;
            Element.MaxVertexIndex = PrivateData->PostAnimationVertexBuffer.VertexBufferRHI->GetSize() / sizeof(VertexType);
            Element.FirstIndex = 0;
            Element.PrimitiveUniformBuffer = CreatePrimitiveUniformBufferImmediate(GetLocalToWorld(), GetBounds(), GetLocalBounds(), true, UseEditorDepthTest());
            
            Element.UserData = PrivateData;

            Collector.AddMesh(ViewIndex, Batch);
		}
	}
}

void FNeoFurComponentSceneProxy::SetPhysicsParameters(
	const FNeoFurPhysicsParameters &Params,
	const FNeoFurFramePhysicsInputs &AccumulatedForces)
{
	check(IsInGameThread());
	
	ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
		NeoFurSceneProxySetPhysicsParams,
		FNeoFurComponentSceneProxy *, Proxy, this,
		FNeoFurPhysicsParameters, Params, Params,
		FNeoFurFramePhysicsInputs, AccumulatedForces, AccumulatedForces,
		{
			Proxy->PrivateData->PhysicsParameters = Params;
			Proxy->PrivateData->PhysicsFrameInputs = AccumulatedForces;
		}
	);
}

void FNeoFurComponentSceneProxy::SetActiveShellCount(int32 NewActiveShellCount, float InShellFade)
{
	check(IsInGameThread());

	ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
		NeoFurSceneProxySetActiveShellCount,
		FNeoFurComponentSceneProxy *, Proxy, this,
		int32, NewActiveShellCount, NewActiveShellCount,
		float, InShellFade, InShellFade,
		{
			if(NewActiveShellCount < 1) NewActiveShellCount = 1;
			if(NewActiveShellCount > Proxy->PrivateData->ShellCount) NewActiveShellCount = Proxy->PrivateData->ShellCount;
			Proxy->PrivateData->ActiveShellCount = NewActiveShellCount;
			Proxy->PrivateData->ShellFade = InShellFade;
		}
	);
}

void FNeoFurComponentSceneProxy::SetShellDistance(float InDistance)
{
	check(IsInGameThread());
	
	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		NeoFurSceneProxySetShellDistance,
		FNeoFurComponentSceneProxy *, Proxy, this,
		float, InDistance, InDistance,
		{
			Proxy->PrivateData->ShellDistance = InDistance;
		}
	);
}

void FNeoFurComponentSceneProxy::SetVisibleLengthScale(float InVisibleLengthScale)
{
	check(IsInGameThread());

	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		NeoFurSceneProxySetVisibleLengthScale,
		FNeoFurComponentSceneProxy *, Proxy, this,
		float, InVisibleLengthScale, InVisibleLengthScale,
		{
			Proxy->PrivateData->VisibleLengthScale = InVisibleLengthScale;
		}
	);
}

bool FNeoFurComponentSceneProxy::GetForceSimulateOnCPU() const
{
    return PrivateData->bForceSimulateOnCPU;
}

void FNeoFurComponentSceneProxy::SetMaterialProxy(FMaterialRenderProxy *MaterialProxy)
{
	check(IsInGameThread());

	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		NeoFurSceneProxySetMaterialProxy,
		FNeoFurComponentSceneProxy *, Proxy, this,
        FMaterialRenderProxy *, MaterialProxy, MaterialProxy,
		{
			Proxy->PrivateData->MaterialRenderProxy = MaterialProxy;
		}
	);
}






