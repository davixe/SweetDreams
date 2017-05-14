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

// This file contains the CPU-side physics simulation code. It's a big
// function that we come back to more often than the rest of
// FNeoFurComponentSceneProxy, so for organization we'll have it in
// its own file.

// FNeoFurComponentSceneProxy::RunSimulation_Renderthread_CPU() should
// operate very similarly to NeoFurComputeShader_Main() in
// NeoFurComputeShader.usf. If this is altered to have different
// behavior, NeoFurComputeShader_Main() must be updated to maintain
// parity between the two versions.

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

#include "RHICommandList.h"

// CPU shader support functions.

inline FVector GetPositionForShell(
	float ShellAlpha,
	const FVector &SkinnedFinalPosition,
	const FVector &SkinnedFinalSplineDir,
	const FVector &ControlPointPosition,
	float Bendiness,
	float In_ShellFade,
	float In_VisibleLengthScale,
	float In_ShellDistance)
{
	// FIXME: This might be much more efficient if we computed it
	// outside the loop and used a lookup table.
	float BlendedPointAlpha = 1.0f - powf(1.0 - ShellAlpha, Bendiness);
	FVector BlendedPoint =
		(1.0f - BlendedPointAlpha) * (SkinnedFinalPosition + SkinnedFinalSplineDir * In_ShellDistance) +
		BlendedPointAlpha * ControlPointPosition;
		
	float positionAlpha = ShellAlpha * In_ShellFade * In_VisibleLengthScale;
	FVector NewPoint_Position =
		(1.0f - positionAlpha) * SkinnedFinalPosition +
		positionAlpha * BlendedPoint;

	return NewPoint_Position;
}

void FNeoFurComponentSceneProxy::RunSimulation_Renderthread_CPU(
	const FTransform &RelativeTransformSinceLastFrame,
	float DeltaTime,
	const FVector &LocalSpaceGravity,
	FMatrix *BoneMats,
	MorphDataVertexType *IncomingMorphData)
{
	check(IsInRenderingThread());
	check(!ShouldUseGPUShader());

	if(PrivateData->bSkipSimulation) return;

	// We need to compute the skinned position of the simulated vertices. To do
	// this, we'll add up influences of...
	//   gravity,
	//   existing particle velocity,
	//   spring force with the original skinned vert,
	//   velocity from the relative transform,
	//   and compute a new control point position.

	TArray<ControlPointVertexType> &NewControlPoints = PrivateData->ControlPointVertexBuffers_CPU[!PrivateData->ControlPointVertexBufferFrame];
	const TArray<ControlPointVertexType> &OldControlPoints = PrivateData->ControlPointVertexBuffers_CPU[PrivateData->ControlPointVertexBufferFrame];
	int32 NumControlPoints = OldControlPoints.Num();

	// Names to match globals in the compute shader.
	int32 In_ShellCount = PrivateData->ActiveShellCount;
	int32 In_TotalShellCount = PrivateData->ShellCount;
	float In_ShellDistance = PrivateData->ShellDistance;
	
	for (int32 i = 0; i < NumControlPoints; i++) {

		NewControlPoints[i] = OldControlPoints[i];
		
		// Compute skinned vertex location and normal/tangent.
		// --------------------------------------------------------------------

		VertexType SkinnedRenderPoint;
		VertexType_Static &StaticVert = PrivateData->StaticVertexData_CPU[i];

		SkinnedRenderPoint.Position = StaticVert.Position;
		SkinnedRenderPoint.TanZ = StaticVert.TanZ;
		SkinnedRenderPoint.TanX = StaticVert.TanX;
		for(int32 k = 0; k < MAX_TEXCOORDS; k++) {
			SkinnedRenderPoint.UVs[k] = StaticVert.UVs[k];
		}
		SkinnedRenderPoint.CombedDirectionAndLength = StaticVert.CombedDirectionAndLength;

		// Apply morph target data to the position and normal.
		if(IncomingMorphData) {
			SkinnedRenderPoint.Position += IncomingMorphData[i].Offset;
			SkinnedRenderPoint.TanZ += IncomingMorphData[i].Normal;
		}

		if (BoneMats) {
			float TotalInfluence = 0.0f;
			FVector FinalPosition(0.0f, 0.0f, 0.0f);
			FVector FinalTanZ(0.0f, 0.0f, 0.0f);
			FVector FinalTanX(0.0f, 0.0f, 0.0f);
			FVector FinalSplineDir(0.0f, 0.0f, 0.0f);

			for (int32 j = 0; j < sizeof(StaticVert.InfluenceBones) / sizeof(StaticVert.InfluenceBones[0]); j++) {
				if (StaticVert.InfluenceWeights[j]) {
					float ThisWeight = float(StaticVert.InfluenceWeights[j]) / 255.0f;
					
					// SkinnedRenderPoint is used as the source
					// instead of StaticVert here because we want to
					// get the data from after morph targets.
					FinalPosition  += BoneMats[StaticVert.InfluenceBones[j]].TransformPosition(SkinnedRenderPoint.Position) * ThisWeight;
					FinalTanZ      += BoneMats[StaticVert.InfluenceBones[j]].TransformVector(SkinnedRenderPoint.TanZ) * ThisWeight;

					FinalTanX      += BoneMats[StaticVert.InfluenceBones[j]].TransformVector(StaticVert.TanX) * ThisWeight;
					FinalSplineDir += BoneMats[StaticVert.InfluenceBones[j]].TransformVector(StaticVert.CombedDirectionAndLength) * ThisWeight;
					TotalInfluence += ThisWeight;
				}
			}

			if(TotalInfluence) {
				FinalPosition /= TotalInfluence;
				FinalTanZ /= TotalInfluence;
				FinalTanX /= TotalInfluence;
				FinalSplineDir /= TotalInfluence;
			}
			
			SkinnedRenderPoint.Position = FinalPosition;
			SkinnedRenderPoint.TanZ = FinalTanZ;
			SkinnedRenderPoint.TanX = FinalTanX;
			SkinnedRenderPoint.CombedDirectionAndLength = FinalSplineDir;
		}
		FVector NormalizedSplineDir = SkinnedRenderPoint.CombedDirectionAndLength;
		float SplineLength = SkinnedRenderPoint.CombedDirectionAndLength.Size();
		NormalizedSplineDir /= SplineLength;

		// Needed for motion blur.
		FVector OldRootPosition = OldControlPoints[i].RootPosition;
		FVector ControlPointPosition_Old = OldControlPoints[i].Position;

		// Velocity as a result of the different transform.
		// --------------------------------------------------------------------
		
		// General velocity "fudge". Not realistic, but makes it look stiff by
		// faking it.
		float VelocityAlpha = PrivateData->PhysicsParameters.VelocityInfluence;
		NewControlPoints[i].Position =
			VelocityAlpha * RelativeTransformSinceLastFrame.TransformPosition(OldControlPoints[i].Position) +
			(1.0f - VelocityAlpha) * OldControlPoints[i].Position; // FIXME: Work skinning into this equation.

		// Spring simulation.
		// --------------------------------------------------------------------
		{
			FVector TotalSpringForce(0.0f, 0.0f, 0.0f);
			
			// Spring length stuff.
			FVector SpringVector = NewControlPoints[i].Position - SkinnedRenderPoint.Position;
			float SpringLength = SpringVector.Size();

			if(SpringLength > 0.01f) {

				// Linear spring.
				// TODO: Add maximum position offset clamping (max spring squish/stretch). That can help prevent fast motions from letting the control point sink into the body.
				float SpringRestLength = In_ShellDistance * SplineLength;
				float SpringOffset = SpringRestLength - SpringLength;
				float SpringStiffness = PrivateData->PhysicsParameters.SpringLengthStiffness;
				float SpringForce = SpringStiffness * SpringOffset;
				TotalSpringForce += (SpringVector / SpringLength) * SpringForce;

				// Torsion spring stuff.
				// TODO: Add maximum angle offset clamping. That can help prevent fast motions from letting the control point sink into the body.
				float dp = FVector::DotProduct((SpringVector / SpringLength), NormalizedSplineDir);
				float AngleOffset = 0.0f;
				if(dp < 1.0) {
					AngleOffset = acos(dp);
				}
				float AngleSpringStiffness = PrivateData->PhysicsParameters.SpringAngleStiffness;
				if (AngleOffset) {
				    
					FVector SkinnedPositionTarget = SkinnedRenderPoint.Position + NormalizedSplineDir * In_ShellDistance * SplineLength;
					FVector AngleSpringReturnForceDirection = SkinnedPositionTarget - NewControlPoints[i].Position;
					AngleSpringReturnForceDirection.Normalize();
					FVector FinalReturnVelocity = AngleSpringReturnForceDirection * DeltaTime * AngleSpringStiffness * AngleOffset;

					// Clamp to maximum required velocity to get to rest position this frame.
					if ((NewControlPoints[i].Position - SkinnedPositionTarget).SizeSquared() < (FinalReturnVelocity * DeltaTime).SizeSquared()) {
						FinalReturnVelocity = (SkinnedPositionTarget - NewControlPoints[i].Position) / DeltaTime;
					}
					
					TotalSpringForce += FinalReturnVelocity / DeltaTime;
				}

			}
			
			float SpringDampening = PrivateData->PhysicsParameters.SpringDampeningMultiplier;

			NewControlPoints[i].Velocity += TotalSpringForce * DeltaTime * SpringDampening;
		}
		
		// Gravity
		// --------------------------------------------------------------------
		// Note: Don't just accelerate down at 9.8m/s/s. This is to represent
		// the FORCE of gravity, when balanced with all the spring systems and
		// whatever other parts of the sim.
		NewControlPoints[i].Velocity += PrivateData->PhysicsParameters.GravityInfluence * LocalSpaceGravity * DeltaTime;

		// Radial force effects.
		// --------------------------------------------------------------------
		for(int k = 0; k < PrivateData->PhysicsFrameInputs.NumForcesThisFrame; k++) {
			FVector RadialForceVector = NewControlPoints[i].Position - PrivateData->PhysicsFrameInputs.LocalForces[k].Origin;
			float RadialForceDistance = RadialForceVector.Size();
			float RadialForceScale = 1.0f - (RadialForceDistance / PrivateData->PhysicsFrameInputs.LocalForces[k].Radius);
			if(RadialForceDistance && RadialForceScale > 0.0f) {
				RadialForceVector /= RadialForceDistance;
				NewControlPoints[i].Velocity +=
					RadialForceVector *
					PrivateData->PhysicsFrameInputs.LocalForces[k].Strength *
					RadialForceScale *
					PrivateData->PhysicsParameters.RadialForceInfluence;
			}
		}

		// "Air Resistance" Dampening - Deceleration proportional to velocity.
		// --------------------------------------------------------------------
		NewControlPoints[i].Velocity *= PrivateData->PhysicsParameters.AirResistanceMultiplier;

		// Wind
		// --------------------------------------------------------------------

		FVector WindVector = PrivateData->PhysicsFrameInputs.WindVector * DeltaTime;
		NewControlPoints[i].Velocity += WindVector;
		NewControlPoints[i].Velocity += WindVector * sin(FVector::DotProduct(
			NewControlPoints[i].Position, WindVector)) * PrivateData->PhysicsFrameInputs.WindGustsAmount;

		// Finally, apply the velocity to the particle's position.
		// --------------------------------------------------------------------
		if(!FMath::IsNaN(NewControlPoints[i].Velocity.X) && !FMath::IsNaN(NewControlPoints[i].Velocity.Y) && !FMath::IsNaN(NewControlPoints[i].Velocity.Z) &&
			FMath::IsFinite(NewControlPoints[i].Velocity.X) && FMath::IsFinite(NewControlPoints[i].Velocity.Y) && FMath::IsFinite(NewControlPoints[i].Velocity.Z)) {
			    
			NewControlPoints[i].Position += NewControlPoints[i].Velocity * DeltaTime;
		} else {
			NewControlPoints[i].Velocity.X = 0.0f;
			NewControlPoints[i].Velocity.Y = 0.0f;
			NewControlPoints[i].Velocity.Z = 0.0f;
		}

		// Max distance clamp.
		// --------------------------------------------------------------------
		
		float Distance = (NewControlPoints[i].Position - SkinnedRenderPoint.Position).Size();
		float MaxDistance = In_ShellDistance * PrivateData->PhysicsParameters.MaxStretchDistanceMultiplier * SplineLength;
		if (Distance > MaxDistance) {
			FVector NewOffset = ((NewControlPoints[i].Position - SkinnedRenderPoint.Position) / Distance) * MaxDistance;
			NewControlPoints[i].Position = SkinnedRenderPoint.Position + NewOffset;
		}

		// TODO: Min distance clamp.
		// --------------------------------------------------------------------

		// Max angle clamp.
		// --------------------------------------------------------------------

		// FIXME: Wasting time by doing degree-to-radian conversion here.
		float MaxRotation = PrivateData->PhysicsParameters.MaxRotationFromNormal * 3.14159/180.0;
		FVector ControlPointOffset = (NewControlPoints[i].Position - SkinnedRenderPoint.Position);
		FVector NormalizedTanZ = NormalizedSplineDir; //SkinnedRenderPoint.TanZ / SkinnedRenderPoint.TanZ.Size();
		FVector NormalizedOffset = ControlPointOffset / ControlPointOffset.Size();
		if(acos(FVector::DotProduct(NormalizedTanZ, NormalizedOffset)) > MaxRotation) {

			// Make a rotation matrix that can bring us into a coordinate space
			// defined by the normal, a unit length vector that is coplanar with
			// the normal and offset while also perpendicular to the normal, and
			// an axis perpendicular to both of those.
			FVector axis0 = NormalizedTanZ;
			FVector axis1 = FVector::CrossProduct(NormalizedOffset, axis0);
			axis1.Normalize(); // FIXME: Maybe unneeded.
			FVector axis2 = FVector::CrossProduct(axis0, axis1);

			// FIXME: There's a better way to set this up.
			FMatrix rotMat = FMatrix(axis0, axis1, axis2, FVector::ZeroVector);
			rotMat.M[3][3] = 1.0f;
			rotMat.M[0][3] = 0.0f;
			rotMat.M[1][3] = 0.0f;
			rotMat.M[2][3] = 0.0f;
			rotMat.M[3][0] = 0.0f;
			rotMat.M[3][1] = 0.0f;
			rotMat.M[3][2] = 0.0f;
			
			// Convert to the local coordinate space.
			FVector LocalControlPointOffset = rotMat.GetTransposed().TransformVector(ControlPointOffset);

			// Do the acual clamping in the local coordinate space, where the
			// rotation axes are a simple sin/cos (and y=0 to stay on the right
			// plane).
			float xSide = LocalControlPointOffset.Z < 0 ? -1.0f : 1.0f;
			LocalControlPointOffset.X = cos(MaxRotation);
			LocalControlPointOffset.Z = sin(MaxRotation) * xSide;
			// y=0 plane is the plane that the origin, the skinned position, and
			// the control point all exist on. We definitely want to stay there.
			LocalControlPointOffset.Y = 0;

			// Restore the length of the original offset.
			LocalControlPointOffset.Normalize(); // FIXME: Maybe unneeded.
			LocalControlPointOffset = LocalControlPointOffset * ControlPointOffset.Size();
			
			// Convert back to object space.
			ControlPointOffset = rotMat.TransformVector(LocalControlPointOffset);
			NewControlPoints[i].Position = ControlPointOffset + SkinnedRenderPoint.Position;
		}

		// Write control point for next frame.
		// --------------------------------------------------------------------

		NewControlPoints[i].RootPosition = SkinnedRenderPoint.Position;
		NewControlPoints[i].SkinnedSplineDirection = SkinnedRenderPoint.CombedDirectionAndLength;
		NewControlPoints[i].SkinnedNormal = SkinnedRenderPoint.TanZ.GetUnsafeNormal();
	}
	
	// Update GPU-side control point vertex buffer.
	ControlPointVertexType *GPUControlPoints = (ControlPointVertexType *)RHILockVertexBuffer(
        PrivateData->ControlPointVertexBuffers_GPU[PrivateData->ControlPointVertexBufferFrame].VertexBufferRHI, 0,
        PrivateData->ControlPointVertexBuffers_GPU[PrivateData->ControlPointVertexBufferFrame].VertexBufferRHI->GetSize(),
		RLM_WriteOnly);
	for(int32 i = 0; i < In_ShellCount; i++) {
		memcpy(
			GPUControlPoints + (i * NumControlPoints),
			&NewControlPoints[0],
			sizeof(ControlPointVertexType) * NumControlPoints);
	}
	RHIUnlockVertexBuffer(
		PrivateData->ControlPointVertexBuffers_GPU[PrivateData->ControlPointVertexBufferFrame].VertexBufferRHI);

	// Record active shell count so we don't render something with
	// more shells than whatever was simulated. (This buffer will be
	// used next frame for motion blur.)
	PrivateData->LastSimulatedActiveShellCount[PrivateData->ControlPointVertexBufferFrame] = In_ShellCount;
	
	// Switch buffers for next frame.
	PrivateData->ControlPointVertexBufferFrame = !PrivateData->ControlPointVertexBufferFrame;
}






