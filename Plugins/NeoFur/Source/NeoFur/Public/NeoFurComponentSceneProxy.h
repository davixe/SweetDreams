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

#include "NeoFurComputeShader.h"
#include "NeoFurComponentSceneProxy.generated.h"

class UNeoFurAsset;

const float NeoFurPhysicsParameters_Default_VelocityInfluence            = 1.0f;
const float NeoFurPhysicsParameters_Default_SpringLengthStiffness        = 250.0f;
const float NeoFurPhysicsParameters_Default_SpringAngleStiffness         = 1200.0f;
const float NeoFurPhysicsParameters_Default_SpringDampeningMultiplier    = 0.8f;
const float NeoFurPhysicsParameters_Default_GravityInfluence             = 0.8f;
const float NeoFurPhysicsParameters_Default_AirResistanceMultiplier      = 0.75f;
const float NeoFurPhysicsParameters_Default_MaxStretchDistanceMultiplier = 2.0f;
const float NeoFurPhysicsParameters_Default_MaxRotationFromNormal        = 60.0f;
const float NeoFurPhysicsParameters_Default_RadialForceInfluence         = 1.0f;
const float NeoFurPhysicsParameters_Default_WindInfluence                = 1.0f;
const float NeoFurPhysicsParameters_Default_Bendiness                    = 1.0f;
const float NeoFurPhysicsParameters_Default_NormalDirectionBlend         = 0.0f;

struct FNeoFurProxyPrivateData;

USTRUCT()
struct FNeoFurPhysicsParameters
{
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// !!    IF ANY OF THIS STRUCTURE CHANGES, YOU MUST UPDATE THE OFFSETS IN NeoFurComputeShader.usf!        !!
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta=(ClampMin = "0.0", ClampMax = "1.0"))
	float VelocityInfluence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0"))
	float SpringLengthStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0"))
	float SpringAngleStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpringDampeningMultiplier;

	// FIXME: Make this a vector, so we can have gravity point in whatever direction we want.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics")
	float GravityInfluence;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirResistanceMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "1.0"))
	float MaxStretchDistanceMultiplier;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxRotationFromNormal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0"))
	float RadialForceInfluence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0"))
	float WindInfluence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0", DisplayName = "Bend Exponent"))
	float Bendiness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeoFur Physics", meta = (ClampMin = "0.0"))
	float NormalDirectionBlend;

	FNeoFurPhysicsParameters() :
		VelocityInfluence            (NeoFurPhysicsParameters_Default_VelocityInfluence),
		SpringLengthStiffness        (NeoFurPhysicsParameters_Default_SpringLengthStiffness),
		SpringAngleStiffness         (NeoFurPhysicsParameters_Default_SpringAngleStiffness),
		SpringDampeningMultiplier    (NeoFurPhysicsParameters_Default_SpringDampeningMultiplier),
		GravityInfluence             (NeoFurPhysicsParameters_Default_GravityInfluence),
		AirResistanceMultiplier      (NeoFurPhysicsParameters_Default_AirResistanceMultiplier),
		MaxStretchDistanceMultiplier (NeoFurPhysicsParameters_Default_MaxStretchDistanceMultiplier),
		MaxRotationFromNormal        (NeoFurPhysicsParameters_Default_MaxRotationFromNormal),
		RadialForceInfluence         (NeoFurPhysicsParameters_Default_RadialForceInfluence),
		WindInfluence                (NeoFurPhysicsParameters_Default_WindInfluence),
		Bendiness                    (NeoFurPhysicsParameters_Default_Bendiness),
		NormalDirectionBlend         (NeoFurPhysicsParameters_Default_NormalDirectionBlend)
	{
	}
	
	const FNeoFurPhysicsParameters &operator=(const FNeoFurPhysicsParameters &Other)
	{
		VelocityInfluence            = Other.VelocityInfluence;
		SpringLengthStiffness        = Other.SpringLengthStiffness;
		SpringAngleStiffness         = Other.SpringAngleStiffness;
		SpringDampeningMultiplier    = Other.SpringDampeningMultiplier;
		GravityInfluence             = Other.GravityInfluence;
		AirResistanceMultiplier      = Other.AirResistanceMultiplier;
		MaxStretchDistanceMultiplier = Other.MaxStretchDistanceMultiplier;
		MaxRotationFromNormal        = Other.MaxRotationFromNormal;
		RadialForceInfluence         = Other.RadialForceInfluence;
		WindInfluence                = Other.WindInfluence;
		Bendiness                    = Other.Bendiness;
		NormalDirectionBlend         = Other.NormalDirectionBlend;
		return *this;
	}
};

const int32 NeoFurFramePhysicsInputsNumLocalForces = 4;
struct FNeoFurFramePhysicsInputs
{
	struct {
		FVector Origin;
		float Radius;
		float Strength;
	} LocalForces[NeoFurFramePhysicsInputsNumLocalForces];

	FVector WindVector;
	float WindGustsAmount;
	int32 NumForcesThisFrame;

	FNeoFurFramePhysicsInputs()
	{
		NumForcesThisFrame = 0;
		memset(LocalForces, 0, sizeof(LocalForces));
		WindVector = FVector(0.0f, 0.0f, 0.0f);
		WindGustsAmount = 0.0f;
	}
};

class FNeoFurComponentSceneProxy : public FPrimitiveSceneProxy
{
public:

	FNeoFurComponentSceneProxy(
		const UPrimitiveComponent* InComponent,
		FMaterialRenderProxy *InMaterialRenderProxy,
		int32 InShellCount,
		float InShellDistance,
		UNeoFurAsset *FurAsset,
		bool bForceCPU     = false,
		bool bSkipSim      = false,
		bool bSkipRender   = false,
		FName ResourceName = NAME_None,
		ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::Type::SM5);
		
	virtual ~FNeoFurComponentSceneProxy();

	virtual uint32 GetMemoryFootprint(void) const override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		class FMeshElementCollector& Collector) const override;
		
	virtual void CreateRenderThreadResources() override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const /* override */ ;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) /* override */ ;
	

	// These functions handle initial buffer creation, and copying of
	// skinning data from the parent skinned mesh's vertex buffer.
	typedef struct FNeoFurComponentSceneProxyGenerateCommand FNeoFurComponentSceneProxyGenerateCommand;
	void GenerateBuffers(UNeoFurAsset *FurAsset);

	void GenerateBuffers_Renderthread_CPU(
		FRHICommandListImmediate &RHICmdList,
		FNeoFurComponentSceneProxyGenerateCommand *Command);

	void SetActiveShellCount(int32 NewActiveShellCount, float ShellFade);
	void SetShellDistance(float InDistance);
    void SetVisibleLengthScale(float InVisibleLengthScale);
    
	struct MorphDataVertexType
	{
		FVector Offset;
		FVector Normal;
	};

	// Note: This takes ownership of BoneMats and delete[]s it in the render
	// thread.
	void RunSimulation(
		const FTransform &RelativeTransformSinceLastFrame,
		float DeltaTime,
		const FVector &LocalSpaceGravity,
		FMatrix *BoneMats, int32 NumBoneMats,
		MorphDataVertexType *IncomingMorphData);
		
	void RunSimulation_Renderthread(
		FRHICommandListImmediate &RHICmdList,
		const FTransform &RelativeTransformSinceLastFrame,
		float DeltaTime,
		const FVector &LocalSpaceGravity,
		FMatrix *BoneMats, int32 NumBoneMats,
		MorphDataVertexType *IncomingMorphData);

	void RunSimulation_Renderthread_CPU(
		const FTransform &RelativeTransformSinceLastFrame,
		float DeltaTime,
		const FVector &LocalSpaceGravity,
		FMatrix *BoneMats,
		MorphDataVertexType *IncomingMorphData);
		
#if !NEOFUR_NO_COMPUTE_SHADERS
	void RunSimulation_Renderthread_GPU(
		const FTransform &RelativeTransformSinceLastFrame,
		float DeltaTime,
		const FVector &LocalSpaceGravity,
		FMatrix *BoneMats, int32 NumBoneMats,
		MorphDataVertexType *IncomingMorphData,
		FRHICommandListImmediate &RHICmdList);
#endif

	void SetPhysicsParameters(
		const FNeoFurPhysicsParameters &Params,
		const FNeoFurFramePhysicsInputs &AccumulatedForces);

    void SetMaterialProxy(FMaterialRenderProxy *MaterialProxy);
    
	bool GetForceSimulateOnCPU() const;

	// If this structure ever changes in any way ever, you must update the
	// ANIMVERTEXTYPE_OFFSET_* macros in NeoFurComputeShader.usf.
	// FIXME: Maybe move this type to UNeoFurAsset.
	struct VertexType
	{
		FVector Position;                 // CHANGES - Generated from
                                          //   Shell ID, Control point
                                          //   position, Skinned
                                          //   position, and Bendiness
                                          //   Might be able to do
                                          //   this in vertex shader.

		FVector TanX;                     // Does not change between frames

		FVector TanZ;                     // CHANGES - based entirely
                                          //   on skinned anim (same
                                          //   for all shells). Doing
                                          //   this in VS would be
                                          //   redundant.

		FVector2D UVs[MAX_TEXCOORDS];     // Does not change between frames - unless thickenAmount does.

		uint16 InfluenceBones[4];         // Does not change between frames.
		uint8 InfluenceWeights[4];        // Does not change between frames.
		
		FVector CombedDirectionAndLength; // Does not change between frames.
		FVector Velocity;                 // CHANGES

        // ---
        
		friend FArchive& operator<<(FArchive& Ar, VertexType& V)
		{
			Ar << V.Position;
			Ar << V.TanX;
			Ar << V.TanZ;

			for(int32 i = 0; i < MAX_TEXCOORDS; i++) {
				Ar << V.UVs[i];
			}

			for(int32 i = 0; i < 4; i++) {

				uint8 tmpInfluenceBone = 0;

				if(Ar.IsLoading()) {
					Ar << tmpInfluenceBone;
					V.InfluenceBones[i] = tmpInfluenceBone;
				} else {
					tmpInfluenceBone = (uint8)V.InfluenceBones[i];
					Ar << tmpInfluenceBone;
				}

				Ar << V.InfluenceWeights[i];
			}
			
			Ar << V.CombedDirectionAndLength;

			// We do not save or load velocity, but we must make sure
			// it's initialized.
			if(Ar.IsLoading()) {
				V.Velocity = FVector(0.0f, 0.0f, 0.0f);
			}

			return Ar;
		}
	};
	
	// This is for vertex data that doesn't change constantly. Some of
	// it is data from the original mesh, and some of it is generated
	// per-object. Exists in a vertex buffer for GPU simulation, and a
	// TArray<VertexType_Static> for CPU simulation.
	struct VertexType_Static
	{
		// Vertex data from the growth mesh.
		FVector Position; // ORIGINAL position
		FVector TanZ;     // ORIGINAL normal
		FVector TanX;
		uint16 InfluenceBones[4];
		uint8 InfluenceWeights[4];
		FVector CombedDirectionAndLength;

		// Some channels of UV data are calculated when we create the
		// scene proxy, because we use some of the UV channels to give
		// information about which shell layer we're on.
		FVector2D UVs[MAX_TEXCOORDS];

		// HTML5 doesn't support SV_VertexID and mobile supports it
		// only sometimes.
		float VertexID;
	};
	
	// This structure has decoding and encoding code in
	// NeoFurComputeShader.usf. If it ever changes, update those. This
	// will exist in a a pair of vertex buffers for the compute shader
	// or a pair of TArray<ControlPointVertexType> objects for the CPU
	// simulation.
	struct ControlPointVertexType
	{
		FVector Position;     // Sim+Render
		FVector Velocity;     // Sim
		FVector RootPosition; // Sim+Render

		FVector SkinnedSplineDirection; // Render
		FVector SkinnedNormal;          // Render
	};
	
private:

	bool ShouldUseGPUShader() const;

	// Needs vertex buffers to be initialized before calling.
	uint32 GetControlPointCount() const;

	// Fur will twitch if frame times are erratic, so we need to have
	// some tolerance for the occasional frame time spike.
	#define NeoFurFrameTimeSmoothingFrames 30
	float PreviousFrameTimes[NeoFurFrameTimeSmoothingFrames];
	int32 FrameTimeIndex;

    FNeoFurProxyPrivateData *PrivateData;
    
	bool ReadyToRender;
};





