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

#include "SkeletalRenderPublic.h"
#include "PhysicsEngine/BodySetup.h"

#include "NeoFurAsset.h"
#include "NeoFurComponentSceneProxy.h"
#include "NeoFurComponent.h"
#include "NeoFurVertexFactory.h"


#include "EngineModule.h"
#include "Modules/ModuleVersion.h"

// FIXME: Remove this.
#include <iostream>

UNeoFurComponent::UNeoFurComponent(const FObjectInitializer &ObjectInitializer) :
	UPrimitiveComponent(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UMaterial> DefaultMaterialFinder(TEXT("Material'/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial'"));
	Material = DefaultMaterialFinder.Object;
	
	USkinnedMeshComponent *Parent = FindSkinnedMeshParent();
	if (Parent) {
		AddTickPrerequisiteComponent(Parent);
	}

	ShellCount = 30;
	ShellDistance = 4.0f;

	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
	bTickInEditor = true;

	// We need to have a pretty late update here because of how late
	// the physical animation system runs. This is in case anyone
	// intends to attach fur to a skeleton with physical animation
	// active.
	SetTickGroup(ETickingGroup::TG_PostUpdateWork);

	FurPhysicsParameters = FNeoFurPhysicsParameters();

	FurAsset = nullptr;
	
	bForceCPUSimulation = false;
	bDrawSplines = false;

	BodySetup = nullptr;
	
	memset(&AccumulatedForces, 0, sizeof(AccumulatedForces));

	SetCollisionObjectType(ECollisionChannel::ECC_PhysicsBody);
	SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);

	LODStartDistance = 250.0f;
	LODEndDistance = 2000.0f;
	LODMinimumShellCount = ShellCount / 2;
	MaximumDistanceFromCamera = 4000.0f; // 100 meters default render distance. Too much or too little?

	ActiveShellCount = 0;

    VisibleLengthScale = 1.0f;
    ActiveShellCountScale = 1.0f;

}

USkinnedMeshComponent *UNeoFurComponent::FindSkinnedMeshParent() const
{
	USkinnedMeshComponent *ParentSkinnedComponent = nullptr;
	#if ENGINE_MINOR_VERSION < 12
	USceneComponent *CurrentParent = AttachParent;
	#else
	USceneComponent *CurrentParent = GetAttachParent();
	#endif
	while (CurrentParent) {
		ParentSkinnedComponent = Cast<USkinnedMeshComponent>(CurrentParent);
		if (ParentSkinnedComponent) break;
	  #if ENGINE_MINOR_VERSION < 12
		CurrentParent = CurrentParent->AttachParent;
	  #else
		CurrentParent = CurrentParent->GetAttachParent();
	  #endif
	}
	return ParentSkinnedComponent;
}

UStaticMeshComponent *UNeoFurComponent::FindStaticMeshParent() const
{
	UStaticMeshComponent *ParentStaticComponent = nullptr;
	#if ENGINE_MINOR_VERSION < 12
	USceneComponent *CurrentParent = AttachParent;
	#else
	USceneComponent *CurrentParent = GetAttachParent();
	#endif
	while (CurrentParent) {
		ParentStaticComponent = Cast<UStaticMeshComponent>(CurrentParent);
		if (ParentStaticComponent) break;
	  #if ENGINE_MINOR_VERSION < 12
		CurrentParent = CurrentParent->AttachParent;
	  #else
		CurrentParent = CurrentParent->GetAttachParent();
	  #endif
	}
	return ParentStaticComponent;
}

FPrimitiveSceneProxy *UNeoFurComponent::CreateSceneProxy()
{
	// Find a skinned mesh component to pull data from.
	/*
	USkinnedMeshComponent *ParentSkinnedComponent = FindSkinnedMeshParent();
	if (!ParentSkinnedComponent) {
		// TODO: Complain loudly. The user has attached us to something that is not a skeletal mesh.
		return nullptr;
	}
	*/
	
	if (!FurAsset) {
		// TODO: Complain. No fur asset.
		return nullptr;
	}

	if(!FurAsset->SkeletalMesh && !FurAsset->StaticMesh) {
		// TODO: Complain. No skeletal mesh to attach to.
		return nullptr;
	}

	if(!FurAsset->Indices.Num() || !FurAsset->Vertices.Num()) {
		return nullptr;
	}
	
	if (!Material) {
		// No material. Nothing to render (not an error).
		// FIXME: Keep render state dirty?
		return nullptr;
	}

	if(!ActiveShellCount) {
		return nullptr;
	}

	// FIXME: Find a way to ensure matching skeletons without just checking for matching meshes.
	/*
	if(FurAsset->SkeletalMesh != ParentSkinnedComponent->SkeletalMesh) {
		// TODO: Complain loudly. The user has mismatched a skeletal mesh and fur asset.
		return nullptr;
	}
	*/
	
	if(ShellCount <= 0) {
		return nullptr;
	}

	
#ifndef NEOFUR_NO_COMPUTE_SHADERS
	const TRefCountPtr<FShader>* ShaderRef = nullptr;
	auto GlobalShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::Type::SM5);
	if(GlobalShaderMap) {
	    ShaderRef = GlobalShaderMap->GetShaders().Find(&FNeoFurComputeShaderCS::StaticType);
	}
	if(!ShaderRef) {
		MarkRenderStateDirty(); // FIXME: Maybe we need a better way to say "try again next frame!"
		return nullptr;
	}
#endif

	FNeoFurComponentSceneProxy *Proxy = new FNeoFurComponentSceneProxy(
		this,
		Material->GetRenderProxy(IsSelected()),
		ShellCount,
		ShellDistance,
		FurAsset,
		bForceCPUSimulation,
		bSkipSimulation,
		bSkipRendering,
		NAME_None,
		GetScene()->GetFeatureLevel());

	Proxy->SetPhysicsParameters(FurPhysicsParameters, AccumulatedForces);

    Proxy->SetVisibleLengthScale(VisibleLengthScale);

	// Now that we're added to the scene (and it actually matters), we need to know the current transform.
	UpdateLastFrameTransform();
	
	return Proxy;
}

void UNeoFurComponent::UpdateLastFrameTransform()
{
	LastFrameTransform = this->GetComponentTransform();
}

FBoxSphereBounds UNeoFurComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds MyBounds;
	MyBounds.SphereRadius = ShellDistance;
	MyBounds.BoxExtent = FVector(
		ShellDistance * 2.0f,
		ShellDistance * 2.0f,
		ShellDistance * 2.0f);
	MyBounds.Origin = FVector(0.0f, 0.0f, 0.0f);

	USkinnedMeshComponent *Parent = FindSkinnedMeshParent();
	UStaticMeshComponent *StaticParent = FindStaticMeshParent();
	if (Parent) {
		MyBounds = Parent->CalcBounds(LocalToWorld);
		MyBounds.ExpandBy(FurPhysicsParameters.MaxStretchDistanceMultiplier * ShellDistance);
	} else if(StaticParent) {
		MyBounds = StaticParent->CalcBounds(LocalToWorld);
		MyBounds.ExpandBy(FurPhysicsParameters.MaxStretchDistanceMultiplier * ShellDistance);
	} else {
		// FIXME: Get the size of the source mesh, whatever it is.
		MyBounds = Bounds.TransformBy(LocalToWorld);
	}
	
	return MyBounds;
}

#if NEOFUR_USE_CUSTOM_RNG
static uint32 NeoFurRandomState_i = 0;
static uint32 NeoFurRandomState_j = 0;
static unsigned char NeoFurRandomState[] = {
    0x23, 0x19, 0x7b, 0x3c, 0xf4, 0x8f, 0x9e, 0xc0, 0x5e, 0x99, 0xd9, 0xa9, 0x5c, 0x9b, 0xdf, 0x0b,
    0x89, 0x95, 0xf5, 0x44, 0x88, 0x8a, 0xb7, 0x6e, 0xb6, 0x11, 0x3b, 0xa3, 0x62, 0x37, 0xfe, 0x76,
    0x48, 0x17, 0x96, 0x2c, 0xbf, 0xf6, 0x67, 0x31, 0x68, 0xe1, 0x1c, 0x56, 0xa5, 0x38, 0x4f, 0x46,
    0xdd, 0x8d, 0x08, 0xd1, 0x80, 0x8b, 0xbd, 0x9c, 0x78, 0x39, 0x8e, 0x7c, 0xd7, 0x0c, 0x22, 0xef,
    0xf1, 0x0a, 0xae, 0xd0, 0xee, 0x2f, 0x90, 0x7f, 0xab, 0x43, 0x6f, 0x7a, 0x5b, 0x01, 0xea, 0xaf,
    0xa1, 0x1d, 0xa7, 0x94, 0x20, 0x1e, 0xd6, 0x6d, 0xf7, 0xff, 0xe9, 0x71, 0xa0, 0x1b, 0xc1, 0x58,
    0x6b, 0x21, 0x42, 0x53, 0x6c, 0xe7, 0x83, 0x98, 0x4c, 0xf3, 0xca, 0x74, 0x84, 0x4d, 0xc5, 0xcc,
    0xfb, 0xa4, 0x9f, 0xf8, 0x86, 0x64, 0xd4, 0xb2, 0x55, 0x0d, 0xec, 0x36, 0xb3, 0x63, 0x05, 0x91,
    0x28, 0xce, 0xcb, 0x1f, 0x60, 0xf9, 0xe2, 0xdb, 0x49, 0x6a, 0x7e, 0x8c, 0xdc, 0x25, 0x3a, 0xfc,
    0xc4, 0xad, 0x81, 0xed, 0x3f, 0x40, 0x33, 0x12, 0xb0, 0x77, 0x4a, 0xb9, 0x16, 0xc8, 0x45, 0xd2,
    0xa6, 0xb1, 0x9a, 0x02, 0x47, 0xfd, 0xeb, 0xcf, 0xe5, 0xc3, 0xc2, 0x2a, 0x2e, 0xf2, 0xb4, 0xc6,
    0xba, 0xbb, 0xc7, 0x03, 0x97, 0x3e, 0x30, 0x2d, 0x82, 0x3d, 0x4e, 0xbc, 0x51, 0xf0, 0xaa, 0x34,
    0x26, 0x66, 0x87, 0x54, 0xb8, 0x14, 0x0e, 0x5f, 0x93, 0xe0, 0xd8, 0xc9, 0xe6, 0x50, 0x79, 0x06,
    0xa8, 0xcd, 0x5d, 0x0f, 0x24, 0x13, 0xac, 0x15, 0x41, 0xfa, 0x69, 0x9d, 0x72, 0x73, 0xe4, 0xd3,
    0x07, 0xbe, 0x61, 0xde, 0xb5, 0xd5, 0x65, 0xe8, 0x35, 0x59, 0x00, 0x09, 0x7d, 0x04, 0xe3, 0xda,
    0x29, 0x57, 0x32, 0x27, 0x70, 0x1a, 0x4b, 0xa2, 0x85, 0x75, 0x10, 0x2b, 0x52, 0x18, 0x92, 0x5a,
};
inline static unsigned char NeoFurGetRandomByte()
{
	NeoFurRandomState_i = (NeoFurRandomState_i + 1) % sizeof(NeoFurRandomState);
	NeoFurRandomState_j = (NeoFurRandomState_j + NeoFurRandomState[NeoFurRandomState_i]) % sizeof(NeoFurRandomState);
	unsigned char tmp = NeoFurRandomState[NeoFurRandomState_i];
	NeoFurRandomState[NeoFurRandomState_i] = NeoFurRandomState[NeoFurRandomState_j];
	NeoFurRandomState[NeoFurRandomState_j] = tmp;
	uint32 n = NeoFurRandomState[NeoFurRandomState_i] + NeoFurRandomState[NeoFurRandomState_j];
	return NeoFurRandomState[n % sizeof(NeoFurRandomState)];
}
inline static uint32 NeoFurGetRandomUint()
{
	return
		(((uint32)NeoFurGetRandomByte()) << 24) |
		(((uint32)NeoFurGetRandomByte()) << 16) |
		(((uint32)NeoFurGetRandomByte()) << 8 ) |
		(((uint32)NeoFurGetRandomByte()) << 0 );
}
#else
inline static unsigned char NeoFurGetRandomByte()
{
	return ((uint32)(FMath::Rand())) & 0xff;
}
#endif

void UNeoFurComponent::TickComponent(
	float DeltaTime,
	enum ELevelTick TickType,
	FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Wind
	const FSceneInterface *Scene = GetScene();
	FVector WindDirection(0.0f, 0.0f, 0.0f);
	float WindSpeed = 0.0f;
	float WindGustAmtMin = 0.0f;
	float WindGustAmtMax = 0.0f;
	if(Scene) {
		Scene->GetWindParameters(
			GetComponentLocation(),
			WindDirection, WindSpeed,
			WindGustAmtMin, WindGustAmtMax);
	}
	AccumulatedForces.WindVector = WindDirection * WindSpeed;
	AccumulatedForces.WindGustsAmount = WindGustAmtMax * ((NeoFurGetRandomByte() % 20) * 0.1f - 1.0f); // FIXME: Engine uses FMath::Rand() like this internally, so I'm doing it too.
	AccumulatedForces.WindVector *= FurPhysicsParameters.WindInfluence * 5000.0f; // FIXME: Scaling factor is totally arbitrary.
	
	AccumulatedForces.WindVector = GetComponentTransform().Inverse().TransformVector(AccumulatedForces.WindVector);

	// FIXME: This is probably a terrible way to find views for LOD stuff.
	// FIXME: Only bother with stuff in each camera's field of view.
	float MinDist = FLT_MAX;
	bool FoundCamera = false;
	FVector MyLocation = GetComponentLocation();
	UWorld *World = GetWorld();
	if(World) {
		for(int32 ViewIndex = 0; ViewIndex < World->ViewLocationsRenderedLastFrame.Num(); ViewIndex++) {
			float Dist = (World->ViewLocationsRenderedLastFrame[ViewIndex] - MyLocation).SizeSquared();
			if(Dist < MinDist) {
				MinDist = Dist;
			}
		}
	}
	if(MinDist != FLT_MAX) {
		MinDist = sqrt(MinDist);
	}
	
	float ShellFadeoutAlpha = 1.0f;
	if(MinDist != FLT_MAX) {
		int32 OldShellCount = ActiveShellCount;
		if(MinDist > MaximumDistanceFromCamera) {
			ActiveShellCount = 0;
		} else {
			float LODVal = (MinDist - LODStartDistance) / (LODEndDistance - LODStartDistance);
			LODVal = 1.0f - FMath::Clamp(LODVal, 0.0f, 1.0f);
			float LODCurve = 100.0f; // FIXME: Make adjustable!
			LODVal = pow(LODCurve, LODVal) / LODCurve;
			ActiveShellCount = LODMinimumShellCount + LODVal * (ShellCount - LODMinimumShellCount);
			ActiveShellCount = FMath::Clamp(ActiveShellCount, LODMinimumShellCount, ShellCount);
		}
		if((ActiveShellCount == 0 && OldShellCount != 0) || (OldShellCount == 0 && ActiveShellCount != 0)) {
			MarkRenderStateDirty();
		}
		if(MinDist > LODEndDistance) {
			ShellFadeoutAlpha = 1.0f - (MinDist - LODEndDistance) / (MaximumDistanceFromCamera - LODEndDistance);
		}
	}

	// Debug spline drawing.
	if(FurAsset && GetWorld() && bDrawSplines) {
		for(int32 i = 0; i < FurAsset->SplineLines.Num(); i++) {
			FTransform Transform = GetComponentTransform();
			FVector Start        = Transform.TransformPosition(FurAsset->SplineLines[i][0]);
			FVector End          = Transform.TransformPosition(FurAsset->SplineLines[i][FurAsset->SplineLines[i].Num() - 1]);
			FVector MidPoint     = (Start + End) / 2.0f;
			DrawDebugLine(
				GetWorld(),
				Start,
				MidPoint,
				FColor(255, 0, 0));
			DrawDebugLine(
				GetWorld(),
				MidPoint,
				End,
				FColor(0, 255, 0));
		}
	}

	UpdatePhysicsParametersInProxy();
	memset(&AccumulatedForces, 0, sizeof(AccumulatedForces));


	if (SceneProxy) {

		FNeoFurComponentSceneProxy::MorphDataVertexType *MorphData = nullptr;
		
		// This should convert from OLD component space to world space, and
		// then back to NEW component space.
		FTransform InvertedComponentTransform = GetComponentTransform().Inverse();
		FTransform RelativeTransform = LastFrameTransform * InvertedComponentTransform;

		FVector GravityVector(0.0f, 0.0f, GetWorld() ? GetWorld()->GetGravityZ() : 0.0f);
		GravityVector = InvertedComponentTransform.TransformVector(GravityVector);
		
		// Grab bones.
		FMatrix *BoneMats = nullptr;
		USkinnedMeshComponent *Parent = FindSkinnedMeshParent();
		int32 NumBones = 1;
		if (Parent) {

			const TArray<FTransform> *SpaceBases = &(Parent->GetSpaceBases());
			if(Parent->MasterPoseComponent.IsValid()) {
				SpaceBases = &(Parent->MasterPoseComponent->GetSpaceBases());
			}

			USkeletalMesh *Mesh = Parent->SkeletalMesh;
			NumBones = SpaceBases->Num();

			// Make sure this is the same skeleton that the asset was based on.
			if (Mesh && FurAsset) {

				// Raw pointer and manual allocation because we're going to
				// give ownership of this to the render thread and want to
				// avoid ref counting magic.

				if(NumBones != Mesh->RefBasesInvMatrix.Num()) {
					// TODO: Complain loudly! Bones were added/removed after the
					// asset was created.
					return;
				}
				BoneMats = new FMatrix[NumBones];
				for (int32 i = 0; i < NumBones; i++) {
					BoneMats[i] = Mesh->RefBasesInvMatrix[i] * (*SpaceBases)[i].ToMatrixWithScale();
				}
			}
			
			// Morph targets
			if(Mesh && FurAsset && FurAsset->SkeletalMesh) {

				// FIXME: This is a horribly inefficient loop.
				for(int32 i = 0; i <
					  #if ENGINE_MINOR_VERSION < 13
						Parent->ActiveVertexAnims.Num()
					  #else
						Parent->ActiveMorphTargets.Num()
					  #endif
						; i++) {

				  #if ENGINE_MINOR_VERSION < 13
					FActiveVertexAnim &Anim = Parent->ActiveVertexAnims[i];
				  #else
					FActiveMorphTarget &Anim = Parent->ActiveMorphTargets[i];
				  #endif
				  
				  #if ENGINE_MINOR_VERSION < 13
					float AnimWeight = Anim.Weight;
				  #else
					float AnimWeight = 0.0f;
					if(Anim.WeightIndex < Parent->MorphTargetWeights.Num() && Anim.WeightIndex >= 0) {
						AnimWeight = Parent->MorphTargetWeights[Anim.WeightIndex];
					}
				  #endif

				  #if ENGINE_MINOR_VERSION < 13
					UMorphTarget *MorphTarget = Cast<UMorphTarget>(Anim.VertAnim);
				  #else
					UMorphTarget *MorphTarget = Anim.MorphTarget;
				  #endif

					if(MorphTarget && AnimWeight) {
					    
						UMorphTarget *FurMorphTarget = FurAsset->SkeletalMesh->FindMorphTarget(FName(*MorphTarget->GetName()));

						if (FurMorphTarget) {
						    
							// Found a matching morph target on our side.

							if (!MorphData) {
								MorphData = new FNeoFurComponentSceneProxy::MorphDataVertexType[FurAsset->Vertices.Num()];
								memset(MorphData, 0, sizeof(FNeoFurComponentSceneProxy::MorphDataVertexType) * FurAsset->Vertices.Num());
							}

							int32 NumDeltas = 0;
						  #if ENGINE_MINOR_VERSION < 13
							FVertexAnimDelta *Deltas = FurMorphTarget->GetDeltasAtTime(0.0f, 0, nullptr, NumDeltas);
						  #else
							FMorphTargetDelta *Deltas = GetDeltaFromMorphTarget(FurMorphTarget, 0, NumDeltas);
						  #endif

							for (int32 k = 0; k < NumDeltas; k++) {
							    
							  #if ENGINE_MINOR_VERSION < 13
								FVertexAnimDelta &Delta = Deltas[k];
							  #else
								FMorphTargetDelta &Delta = Deltas[k];
							  #endif

								if(FurAsset->OriginalIndexToNewIndexMapping.Num()) {
								    
									if((int32)Delta.SourceIdx < FurAsset->OriginalIndexToNewIndexMapping.Num()
										&& FurAsset->OriginalIndexToNewIndexMapping[Delta.SourceIdx] < FurAsset->Vertices.Num())
									{
										if(FurAsset->OriginalIndexToNewIndexMapping[Delta.SourceIdx] != -1) {
											MorphData[FurAsset->OriginalIndexToNewIndexMapping[Delta.SourceIdx]].Offset += Delta.PositionDelta * AnimWeight;
											MorphData[FurAsset->OriginalIndexToNewIndexMapping[Delta.SourceIdx]].Normal += Delta.TangentZDelta * AnimWeight;
										}
									} else {
										UE_LOG(NeoFur, Error,
											TEXT("Bad morph data detected! Morph target %s on growth mesh %s is invalid!"),
											*MorphTarget->GetName(),
											*FurAsset->SkeletalMesh->GetName());
									}

								} else {
								    
									// FIXME: Old version. Should probably be removed.
									MorphData[Delta.SourceIdx].Offset += Delta.PositionDelta * AnimWeight;
									MorphData[Delta.SourceIdx].Normal += Delta.TangentZDelta * AnimWeight;
								}
							}
						}
					}
				}

			}
		}
		
		if(MinDist != FLT_MAX) {
			((FNeoFurComponentSceneProxy*)SceneProxy)->SetActiveShellCount(
				int32(float(ActiveShellCount) * FMath::Clamp(ActiveShellCountScale, 0.0f, 1.0f)),
                ShellFadeoutAlpha);
		}

        if(Material) {
            ((FNeoFurComponentSceneProxy*)SceneProxy)->SetMaterialProxy(
                Material->GetRenderProxy(IsSelected()));
        }

		// Queue up the simulation.
		((FNeoFurComponentSceneProxy*)SceneProxy)->RunSimulation(
			RelativeTransform,
			DeltaTime,
			GravityVector,
			BoneMats, NumBones,
			MorphData);
	}

	UpdateLastFrameTransform();

	// Reset forces inputs.
	AccumulatedForces.NumForcesThisFrame = 0;
	memset(AccumulatedForces.LocalForces, 0, sizeof(AccumulatedForces.LocalForces));
}

const FNeoFurPhysicsParameters UNeoFurComponent::GetFurPhysicsParameters() const
{
	return FurPhysicsParameters;
}

void UNeoFurComponent::SetFurPhysicsParameters(const FNeoFurPhysicsParameters &Parameters)
{
	this->FurPhysicsParameters = Parameters;
	UpdatePhysicsParametersInProxy();
}

void UNeoFurComponent::UpdatePhysicsParametersInProxy()
{
	if (SceneProxy) {
		((FNeoFurComponentSceneProxy*)SceneProxy)->SetPhysicsParameters(FurPhysicsParameters, AccumulatedForces);
	}
}

#if WITH_EDITOR
void UNeoFurComponent::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UNeoFurComponent::AddRadialForce(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bAccelChange)
{
	Super::AddRadialForce(Origin, Radius, Strength, Falloff, bAccelChange);

	// FIXME: Find a better way to accumulate forces.
	if(AccumulatedForces.NumForcesThisFrame < NeoFurFramePhysicsInputsNumLocalForces) {
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Origin = GetComponentTransform().Inverse().TransformPosition(Origin);
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Radius = Radius;
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Strength = Strength;
		AccumulatedForces.NumForcesThisFrame++;
	}
}

void UNeoFurComponent::AddRadialImpulse(FVector Origin, float Radius, float Strength, enum ERadialImpulseFalloff Falloff, bool bAccelChange)
{
	Super::AddRadialImpulse(Origin, Radius, Strength, Falloff, bAccelChange);
	
	// FIXME: Find a better way to accumulate forces.
	if(AccumulatedForces.NumForcesThisFrame < NeoFurFramePhysicsInputsNumLocalForces) {
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Origin = GetComponentTransform().Inverse().TransformPosition(Origin);
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Radius = Radius;
		AccumulatedForces.LocalForces[AccumulatedForces.NumForcesThisFrame].Strength = Strength;
		AccumulatedForces.NumForcesThisFrame++;
	}
}

class UBodySetup* UNeoFurComponent::GetBodySetup()
{
	// FIXME: We should correctly calculate the bounding sphere.
	if(!BodySetup) {
		BodySetup = NewObject<UBodySetup>(this);
		BodySetup->AggGeom.SphereElems.Add(FKSphereElem());
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		FKSphereElem* se = BodySetup->AggGeom.SphereElems.GetData();
		se->Center = FVector::ZeroVector;
		se->Radius = FurPhysicsParameters.MaxStretchDistanceMultiplier * ShellDistance + 100.0;
	}

	return BodySetup;
}

int32 UNeoFurComponent::GetTotalActiveShellCount(UObject *WorldContextObject)
{
	UWorld *World = WorldContextObject->GetWorld();
	int32 Total = 0;
	for(TObjectIterator<UNeoFurComponent> i; i; ++i) {
		if(i->GetWorld() == World) {
			Total += i->ActiveShellCount;
		}
	}
	return Total;
}

void UNeoFurComponent::SetShellDistance(float NewDistance)
{
	ShellDistance = NewDistance;
	UpdateBounds();
	
	if(SceneProxy) {
		((FNeoFurComponentSceneProxy*)SceneProxy)->SetShellDistance(NewDistance);
	}

	// FIXME: There is now stale data in the body setup. We must find a way to
	// recalculate that.
}

void UNeoFurComponent::SetVisibleLengthScale(float NewVisibleLengthScale)
{
	VisibleLengthScale = NewVisibleLengthScale;

	if(SceneProxy) {
		((FNeoFurComponentSceneProxy*)SceneProxy)->SetVisibleLengthScale(VisibleLengthScale);
	}
}

#if ENGINE_MINOR_VERSION >= 15
void UNeoFurComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	OutMaterials.Add(Material);
}
#endif





