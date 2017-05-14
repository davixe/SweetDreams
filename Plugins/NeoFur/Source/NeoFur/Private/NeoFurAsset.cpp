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
#include <StaticMeshResources.h>

#if WITH_EDITOR
#if NEOFUR_FBX

// The FBX headers have some code that causes warnings and Clang on Linux to
// freak out.
#ifdef THIRD_PARTY_INCLUDES_START
THIRD_PARTY_INCLUDES_START
#include "fbxsdk.h"
THIRD_PARTY_INCLUDES_END
#else
#include "fbxsdk.h"
#endif

#endif
#include <fstream>
#include <sstream>
#endif

#include "EngineModule.h"
#include "Modules/ModuleVersion.h"

#include "NeoFurComponent.h"
#include "NeoFurVertexFactory.h"
#include "NeoFurAsset.h"

#if WITH_EDITOR && NEOFUR_FBX
static void LoadFurFBXData(FString &filename, UNeoFurAsset *ret);
#endif

// Version history...
//   1 - Initial version.
//   2 - Added OriginalIndexToNewIndexMapping.
#define FAKE_VERSION_NUMBER -1
#define MOST_RECENT_VERSION 3

UNeoFurAsset::UNeoFurAsset(const FObjectInitializer &ObjectInitializer) :
	Super(ObjectInitializer)
{
	Version = FAKE_VERSION_NUMBER;
	SkeletalMesh = nullptr;
	StaticMesh = nullptr;
	MorphTargetName = "NeoFur_OuterLayer";
}

void UNeoFurAsset::Serialize(FArchive &Ar)
{
	Super::Serialize(Ar);
	
	if(Ar.IsLoading()) {

		UE_LOG(NeoFur, Log, TEXT("Fur asset loading start."));

		Ar << Vertices;

		// In an attempt to not break every existing fur asset, we're
		// using a vertex count of zero to indicate that you should
		// jump to the version of the loading code that actually
		// tracks stuff by version number.
		if(Vertices.Num() != 0) {

			Ar << Indices;
			Ar << SplineLines;

			if(GIsEditor) {
				// In the editor? Clobber whatever we loaded with the actual
				// skeleton data. Replace stale data in the asset file.
				RegenerateFromSkeletalMesh();
			}
			
			UE_LOG(NeoFur, Log, TEXT("OLD Fur asset load complete."));
			return;
		}

	} else {
	    
		// Leave our stupid marker indicating that we're terrible at
		// dealing with versioning our binary format.
		TArray<FNeoFurComponentSceneProxy::VertexType> junkPlaceholder;
		Ar << junkPlaceholder;

	}

	// Rebuild everything before save.
	if(GIsEditor && !Ar.IsLoading()) {
		RegenerateFromSkeletalMesh();
	}

	// Newer loading.
	UE_LOG(NeoFur, Log, TEXT("NEW Fur asset load/save starting."));

	if(!Ar.IsLoading()) {
		Version = MOST_RECENT_VERSION;
		UE_LOG(NeoFur, Log, TEXT("Saving with most recent version: %d"), Version);
	} else {
		UE_LOG(NeoFur, Log, TEXT("Loading"));
	}

	Ar << Version;
	
	if(!Ar.IsLoading()) {
		UE_LOG(NeoFur, Log, TEXT("File version: %d"), Version);
	}

	UE_LOG(NeoFur, Log, TEXT("Serializing verts, indices, and splines from version 1+."));
	Ar << Vertices;
	Ar << Indices;
	Ar << SplineLines;

	if(Version > 1) {
		UE_LOG(NeoFur, Log, TEXT("Serializing index mappings from version 2+."));
		Ar << OriginalIndexToNewIndexMapping;
	}
	
	if(Version > 2) {
		UE_LOG(NeoFur, Log, TEXT("Loading 16-bint bone IDs from version 3+."));

		TArray<uint16> BoneIDs;

		if(Ar.IsLoading()) {
			Ar << BoneIDs;
			if(BoneIDs.Num() == Vertices.Num() * 4) {
				for(int32 i = 0; i < Vertices.Num(); i++) {
					for(int32 j = 0; j < 4; j++) {
						Vertices[i].InfluenceBones[j] = BoneIDs[i * 4 + j];
					}
				}
			}
		} else {
			BoneIDs.SetNum(Vertices.Num() * 4);
			for(int32 i = 0; i < Vertices.Num(); i++) {
				for(int32 j = 0; j < 4; j++) {
					BoneIDs[i * 4 + j] = Vertices[i].InfluenceBones[j];
				}
			}
			Ar << BoneIDs;
		}
	}
	
	// Rebuild everything after load, if we're in the editor.
	if(GIsEditor && Ar.IsLoading()) {
		UE_LOG(NeoFur, Log, TEXT("Rebuilding fur from mesh data for editor load."));
		RegenerateFromSkeletalMesh();
	}

	UE_LOG(NeoFur, Log, TEXT("Fur asset load complete."));
}

static void RemoveVertex(
	int32 Index,
	TArray<uint32> &Indices,
	TArray<FNeoFurComponentSceneProxy::VertexType> &SoftSkinVertices,
	TArray<int32> &OriginalIndexToNewIndexMapping)
{
	// Take the vertex from the end and stick it in this one's slot.
	int32 ReplacementIndex = SoftSkinVertices.Num() - 1;
	SoftSkinVertices[Index] = SoftSkinVertices[ReplacementIndex];
	
	// Fix up any indices that were pointing to the one we just moved.
	for(int32 k = 0; k < Indices.Num(); k++) {
		check(Indices[k] != Index); // If this fires off, it means we're trying to delete something that's in use.
		if(Indices[k] == ReplacementIndex) {
			Indices[k] = Index;
		}
	}

	for(int32 k = 0; k < OriginalIndexToNewIndexMapping.Num(); k++) {
		if(OriginalIndexToNewIndexMapping[k] == ReplacementIndex) {
			OriginalIndexToNewIndexMapping[k] = Index;
		} else if(OriginalIndexToNewIndexMapping[k] == Index) {
			OriginalIndexToNewIndexMapping[k] = -1;
		}
	}

	// Erase the end.
	SoftSkinVertices.SetNum(SoftSkinVertices.Num() - 1);
}

static void RemoveUnusedVertices(
	TArray<uint32> &Indices,
	TArray<FNeoFurComponentSceneProxy::VertexType> &SoftSkinVertices,
	TArray<int32> &OriginalIndexToNewIndexMapping)
{
	for(int32 i = 0; i < SoftSkinVertices.Num(); i++) {

		bool FoundThisVert = false;
		
		for(int32 k = 0; k < Indices.Num(); k++) {
			if(Indices[k] == i) {
				FoundThisVert = true;
				break;
			}
		}

		if(!FoundThisVert) {
			RemoveVertex(i, Indices, SoftSkinVertices, OriginalIndexToNewIndexMapping);
			i--;
		}
	}
}

static void SetupSplines(
	TArray<FNeoFurComponentSceneProxy::VertexType> &Vertices,
	const TArray< TArray<FVector> > &SplineLines)
{
	for(int32 i = 0; i < Vertices.Num(); i++) {
	    
		FNeoFurComponentSceneProxy::VertexType &Vert = Vertices[i];

		// FIXME: Exceptionally dumb spline setup. This just finds the
		// closest splines to the point and uses an interpolated
		// version of those splines' end points to replace this
		// vertex's normal (and the spring direction that goes with
		// it). We should change this in the future to use at least
		// three points.
		
		// This code is duplicated for skeletal mesh and static meshes
		// separately. Might want to condense it into a common piece
		// of code some day.

		float ClosestSplineDist = FLT_MAX;
		FVector SplineDirection = Vert.CombedDirectionAndLength;
		FVector SplineOrigin = Vert.Position;
		
		float ClosestSplineDist2 = FLT_MAX;
		FVector SplineDirection2 = Vert.CombedDirectionAndLength;
		FVector SplineOrigin2 = Vert.Position;

		// Splines.
		for(int32 k = 0; k < SplineLines.Num(); k++) {
			float s = (SplineLines[k][0] - Vert.Position).SizeSquared();
			if(s < ClosestSplineDist) {

				// Bump current down to second place.
				ClosestSplineDist2 = ClosestSplineDist;
				SplineOrigin2 = SplineOrigin;
				SplineDirection2 = SplineDirection;

				ClosestSplineDist = s;
				SplineOrigin = SplineLines[k][0];
				SplineDirection = SplineLines[k][SplineLines[k].Num() - 1] - SplineOrigin;

			} else if(s < ClosestSplineDist2) {

				// Between first and second.
				ClosestSplineDist2 = s;
				SplineOrigin2 = SplineLines[k][0];
				SplineDirection2 = SplineLines[k][SplineLines[k].Num() - 1] - SplineOrigin2;
			}
		}
		
		ClosestSplineDist  = sqrt(ClosestSplineDist);
		ClosestSplineDist2 = sqrt(ClosestSplineDist2);

		float TotalDist = ClosestSplineDist + ClosestSplineDist2;
		ClosestSplineDist /= TotalDist;
		ClosestSplineDist2 /= TotalDist;
		
		SplineDirection = SplineDirection * ClosestSplineDist2 + SplineDirection2 * ClosestSplineDist;

		Vert.CombedDirectionAndLength = SplineDirection;
	}
}

void UNeoFurAsset::RegenerateFromSkeletalMesh()
{
	check(GIsEditor);


	if(SkeletalMesh && SkeletalMesh->GetLinker())
		SkeletalMesh->GetLinker()->Preload(SkeletalMesh);
	if(StaticMesh && StaticMesh->GetLinker()) {
		StaticMesh->GetLinker()->Preload(StaticMesh);
	}

	if(StaticMesh && !StaticMesh->RenderData) {
		return;
	}

	if(SkeletalMesh && SkeletalMesh->GetImportedResource()->LODModels.Num() == 0) {
		// We're pointing to something, but it hasn't loaded yet? Avoid nuking
		// the data we have.
		return;
	}
	
	Vertices.Empty();
	Indices.Empty();
	OriginalIndexToNewIndexMapping.Empty();

	if(SkeletalMesh) {

		const FSkeletalMeshResource *Resource = SkeletalMesh->GetImportedResource();
		const FStaticLODModel &Model = Resource->LODModels[0];
		
		TArray<uint32> IndicesFromModel;
		TArray<uint32> IndicesAfterFilter;

		SkeletalMesh->GetImportedResource()->LODModels[0].MultiSizeIndexContainer.GetIndexBuffer(IndicesFromModel);

		TArray<FSoftSkinVertex> SoftSkinVertices;
		SkeletalMesh->GetImportedResource()->LODModels[0].GetVertices(SoftSkinVertices);
		
		OriginalIndexToNewIndexMapping.SetNumUninitialized(SoftSkinVertices.Num());
		for(int32 k = 0; k < OriginalIndexToNewIndexMapping.Num(); k++) {
			OriginalIndexToNewIndexMapping[k] = -1;
		}

		for(int32 k = 0; k < IndicesFromModel.Num(); k++) {
			// We need to map bone indices from the chunk's bone map to the actual index now.
			int32 VertIndex = 0;
			int32 ChunkIndex = 0;
			bool bSoftVert = true;
			bool bExtraBoneInfluences = false;
			Model.GetChunkAndSkinType(IndicesFromModel[k], ChunkIndex, VertIndex, bSoftVert, bExtraBoneInfluences);
			
			if(ChunkIndex == MaterialId || MaterialId == -1) {
				IndicesAfterFilter.Add(IndicesFromModel[k]);
				OriginalIndexToNewIndexMapping[IndicesFromModel[k]] = IndicesFromModel[k];
			}
		}

		Indices = IndicesAfterFilter;

		Vertices.SetNumZeroed(SoftSkinVertices.Num());

		for(int32 i = 0; i < SoftSkinVertices.Num(); i++) {
		    
			// We need to map bone indices from the chunk's bone map to the actual index now.
			//const FSkeletalMeshResource *Resource = SkeletalMesh->GetImportedResource();
			//const FStaticLODModel &Model = Resource->LODModels[0];
			int32 VertIndex = 0;
			int32 ChunkIndex = 0;
			bool bSoftVert = true;
			bool bExtraBoneInfluences = false;
			Model.GetChunkAndSkinType(i, ChunkIndex, VertIndex, bSoftVert, bExtraBoneInfluences);

			// DONOTCHECKIN
			#if ENGINE_MINOR_VERSION < 13
			const FSkelMeshChunk &Chunk = Model.Chunks[ChunkIndex];
			#else
			const FSkelMeshSection &Chunk = Model.Sections[ChunkIndex];
			#endif
			
			FNeoFurComponentSceneProxy::VertexType &DstVert = Vertices[i];
			FSoftSkinVertex &SrcVert = SoftSkinVertices[i];
			DstVert.Position = SrcVert.Position;
			DstVert.TanX = SrcVert.TangentX;
			DstVert.TanZ = SrcVert.TangentZ;

			// FIXME: Exceptionally dumb spline setup. This just finds
			// the closest splines to the point and uses an
			// interpolated version of those splines' end points to
			// replace this vertex's normal (and the spring direction
			// that goes with it). We should use three points in th
			// future.
			
			// This code is duplicated for skeletal mesh and static
			// meshes separately. Might want to condense it into a
			// common piece of code some day.

			float ClosestSplineDist = FLT_MAX;
			FVector SplineDirection = SrcVert.TangentZ;
			FVector SplineOrigin = SrcVert.Position;
			
			float ClosestSplineDist2 = FLT_MAX;
			FVector SplineDirection2 = SrcVert.TangentZ;
			FVector SplineOrigin2 = SrcVert.Position;

			// Splines.
			for(int32 k = 0; k < SplineLines.Num(); k++) {
				float s = (SplineLines[k][0] - DstVert.Position).SizeSquared();
				if(s < ClosestSplineDist) {

					// Bump current down to second place.
					ClosestSplineDist2 = ClosestSplineDist;
					SplineOrigin2 = SplineOrigin;
					SplineDirection2 = SplineDirection;

					ClosestSplineDist = s;
					SplineOrigin = SplineLines[k][0];
					SplineDirection = SplineLines[k][SplineLines[k].Num() - 1] - SplineOrigin;

				} else if(s < ClosestSplineDist2) {

					// Between first and second.
					ClosestSplineDist2 = s;
					SplineOrigin2 = SplineLines[k][0];
					SplineDirection2 = SplineLines[k][SplineLines[k].Num() - 1] - SplineOrigin2;
				}
			}
			
			ClosestSplineDist  = sqrt(ClosestSplineDist);
			ClosestSplineDist2 = sqrt(ClosestSplineDist2);

			float TotalDist = ClosestSplineDist + ClosestSplineDist2;
			ClosestSplineDist /= TotalDist;
			ClosestSplineDist2 /= TotalDist;
			
			SplineDirection = SplineDirection * ClosestSplineDist2 + SplineDirection2 * ClosestSplineDist;

			DstVert.CombedDirectionAndLength = SplineDirection;

			for(int32 k = 0; k < MAX_TEXCOORDS; k++) {
				DstVert.UVs[k] = SrcVert.UVs[k];
			}

			int32 totalWeight = 0;

			for(int32 k = 0; k < 4; k++) {
				if(k < Chunk.MaxBoneInfluences) {
					DstVert.InfluenceBones[k]   = Chunk.BoneMap[SrcVert.InfluenceBones[k]];
					DstVert.InfluenceWeights[k] = SrcVert.InfluenceWeights[k];
					totalWeight += DstVert.InfluenceWeights[k];
				} else {
					DstVert.InfluenceBones[k]   = 0;
					DstVert.InfluenceWeights[k] = 0;
				}
			}
		}

		
		UMorphTarget *MorphTarget = nullptr;
		for(int32 i = 0; i < SkeletalMesh->MorphTargets.Num(); i++) {
			if(FName(*MorphTargetName) == SkeletalMesh->MorphTargets[i]->GetFName()) {
				MorphTarget = SkeletalMesh->MorphTargets[i];
				break;
			}
		}

		if(MorphTarget) {

			UE_LOG(NeoFur, Log, TEXT("Using morph target for outer layer: %s"), *MorphTargetName);
			
			int32 NumDeltas = 0;
		  #if ENGINE_MINOR_VERSION < 13
			FVertexAnimDelta *AnimDelta = MorphTarget->GetDeltasAtTime(0.0f, 0, nullptr, NumDeltas);
		  #else
			FMorphTargetDelta *AnimDelta = GetDeltaFromMorphTarget(MorphTarget, 0, NumDeltas);
		  #endif

			if(AnimDelta) {

				for(int32 i = 0; i < NumDeltas; i++) {
					if(int32(AnimDelta[i].SourceIdx) < Vertices.Num()) {
						FNeoFurComponentSceneProxy::VertexType &DstVert = Vertices[AnimDelta[i].SourceIdx];
						DstVert.CombedDirectionAndLength = AnimDelta[i].PositionDelta;
					} else {
						UE_LOG(NeoFur, Log, TEXT("Morph target vertex index is an invalid index!"));
					}
				}
				
			}

		} else {
			UE_LOG(NeoFur, Log, TEXT("No morph targets for this mesh."));
		}
		
		RemoveUnusedVertices(Indices, Vertices, OriginalIndexToNewIndexMapping);
	}

	if(StaticMesh) {

		const FStaticMeshLODResources &res = StaticMesh->GetLODForExport(0);
		const FStaticMeshVertexBuffer &buf = res.VertexBuffer;
		const FPositionVertexBuffer &posbuf = res.PositionVertexBuffer;

		const FStaticMeshSection *SelectedSection = nullptr;
		for(int32 i = 0; i < res.Sections.Num(); i++) {
			const FStaticMeshSection &Section = res.Sections[i];
			if(Section.MaterialIndex == MaterialId) {
				SelectedSection = &Section;
				break;
			}
		}
		
		for(int32 i = 0; i < (int32)buf.GetNumVertices(); i++) {

			FNeoFurComponentSceneProxy::VertexType newVert;
			newVert.Position = posbuf.VertexPosition(i);
			
			for(int32 k = 0; k < (int32)buf.GetNumTexCoords() && k < MAX_TEXCOORDS; k++) {
				newVert.UVs[k] = buf.GetVertexUV(i, k);
			}
			for(int32 k = buf.GetNumTexCoords(); k < MAX_TEXCOORDS; k++) {
				newVert.UVs[k] = FVector2D(0.0f, 0.0f);
			}

			newVert.TanX = buf.VertexTangentX(i);
			newVert.TanX.Normalize();
			newVert.TanZ = buf.VertexTangentZ(i);
			
			newVert.CombedDirectionAndLength = newVert.TanZ;

			newVert.InfluenceBones[0] = 0;
			newVert.InfluenceWeights[0] = 255;
			for(int32 k = 1; k < 4; k++) {
				newVert.InfluenceBones[k] = 0;
				newVert.InfluenceWeights[k] = 0;
			}
			
			Vertices.Add(newVert);
		}

		TArray<uint32> TempIndexBuffer;

		res.IndexBuffer.GetCopy(TempIndexBuffer);

		UE_LOG(NeoFur, Log, TEXT("Making original index mapping"));

		OriginalIndexToNewIndexMapping.SetNumUninitialized(buf.GetNumVertices());
		for(int32 k = 0; k < OriginalIndexToNewIndexMapping.Num(); k++) {
			OriginalIndexToNewIndexMapping[k] = -1;
		}

		UE_LOG(NeoFur, Log, TEXT("Adding vertices for section"));
		
		if(SelectedSection) {
			// Filter us down to just the selected material ID.
			for(int32 i = SelectedSection->FirstIndex; i < (int32)SelectedSection->FirstIndex + (int32)SelectedSection->NumTriangles * 3; i++) {
				Indices.Add(TempIndexBuffer[i]);
				OriginalIndexToNewIndexMapping[TempIndexBuffer[i]] = TempIndexBuffer[i];
			}
		} else {
			// Get the entire model.
			Indices = TempIndexBuffer;
		}

		UE_LOG(NeoFur, Log, TEXT("Clearing unused verts"));
		
		RemoveUnusedVertices(Indices, Vertices, OriginalIndexToNewIndexMapping);

		SetupSplines(Vertices, SplineLines);
	}

}

#if WITH_EDITOR
void UNeoFurAsset::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	check(GIsEditor);

	// Update all components that could be running with data based off of this
	// asset.
	for(TObjectIterator<UNeoFurComponent> Itr; Itr; ++Itr) {
		if(Itr->FurAsset == this) {
			Itr->MarkRenderStateDirty();
		}
	}

	if(SplineDataFbxFilePath.FilePath.Len() == 0) {

		FbxSourceFilePathRelative = TEXT("");
		SplineLines.Empty();
		
	} else {

		// Convert the path entered into a relative path... by converting it to a full path first.
		// FIXME: This is dumb.
		FbxSourceFilePathRelative = SplineDataFbxFilePath.FilePath;
		if(FPaths::IsRelative(FbxSourceFilePathRelative)) {
			FbxSourceFilePathRelative = FPaths::ConvertRelativePathToFull(
				FPlatformProcess::BaseDir(), FbxSourceFilePathRelative);
		}

		SplineLines.Empty();
		#if NEOFUR_FBX
		LoadFurFBXData(FbxSourceFilePathRelative, this);
		#endif
		
		FPaths::MakePathRelativeTo(FbxSourceFilePathRelative, *FPaths::GameDir());
	}

	RegenerateFromSkeletalMesh();
}
#endif


#if WITH_EDITOR

// ----------------------------------------------------------------------
// FBX handling code
// ----------------------------------------------------------------------

#if NEOFUR_FBX

struct FMemReaderFileInfo
{
	void *Data;
	size_t Length;
};

class FMemReaderFbx : public FbxStream {
public:

	const void *Buffer;
	size_t Length;
	mutable size_t Offset;
	FbxManager *Manager;

	FMemReaderFbx(FbxManager *NewManager) :
		FbxStream()
	{
		this->Buffer = nullptr;
		this->Length = 0;
		this->Manager = NewManager;
		Offset = 0;
	}
	
	virtual bool Open(void *RawInfo) override
	{
		FMemReaderFileInfo *Info = (FMemReaderFileInfo*)RawInfo;
		this->Buffer = Info->Data;
		Offset = 0;
		Length = Info->Length;
		return true;
	}
	virtual bool Close() override
	{
		if(Buffer) {
			Buffer = nullptr;
			Offset = 0;
			Length = 0;
			return true;
		}
		return false;
	}

	virtual int Read(void *Data, int Size) const override
	{
		if(Offset + Size >= Length) {
			Size = Length - Offset;
		}
		
		memcpy(Data, (char*)Buffer + Offset, Size);

		Offset += Size;

		return Size;
	}

	virtual long GetPosition() const override
	{
		return Offset;
	}
	
	virtual void SetPosition(long Position) override
	{
		Offset = Position;
		if(Offset > Length) Offset = Length;
	}

	virtual EState GetState()
	{
		if(Buffer) return eOpen;
		return eClosed;
	}
	
	virtual bool Flush()
	{
		return true;
	}

	virtual int Write(const void *Data, int Size) override
	{
		return 0;
	}
	
	virtual int GetReaderID() const override
	{
		int id = Manager->GetIOPluginRegistry()->FindReaderIDByExtension("fbx");
		return id;
	}

	virtual int GetWriterID() const override
	{
		return -1;
	}
	
	virtual void Seek(const FbxInt64& pOffset, const FbxFile::ESeekPos& pSeekPos) override
	{
		if(pSeekPos == FbxFile::ESeekPos::eBegin) {
			Offset = pOffset;
		} else if(pSeekPos == FbxFile::ESeekPos::eCurrent) {
			Offset += pOffset;
		} else {
			Offset = Length - pSeekPos;
		}

		if(Offset > Length) Offset = Length;
	}

	virtual int GetError() const override
	{
		return 0;
	}

	virtual void ClearError() override
	{
	}

private:
};


static void ExtractSplines(FbxNode *Node, int RecursionCount, UNeoFurAsset *asset)
{
	// FIXME: A bunch of this assumes that the FBX file coming in is valid.

	FbxNurbsCurve *Nurbs = Node->GetNurbsCurve();
	FbxLine *Line = Node->GetLine();
	
	bool OwnedLine = false;

	if(!Line && Nurbs) {
		Line = Nurbs->TessellateCurve();
		OwnedLine = true;
	}
	
	if(Line) {
		FbxArray<int> *PointArray = Line->GetEndPointArray();
		FbxVector4 *ControlPoints = Line->GetControlPoints();

		// FIXME: Is the transform information even needed?
		/*
		FbxAMatrix TransformFbx = Node->EvaluateGlobalTransform();
		FMatrix Transform;
		for(int row = 0; row < 4; row++) {
			FbxVector4 VecFbx = TransformFbx.GetRow(row);
			for(int col = 0; col < 4; col++) {
				Transform.M[row][col] = ((double*)VecFbx)[col];
			}
		}
		*/

		int endPointCounter = 0;
		int pointCounter = 0;
		while(endPointCounter < PointArray->GetCount()) {

			TArray<FVector> NewLine;

			while(pointCounter <= PointArray->GetAt(endPointCounter)) {

				FVector InputVec(
					ControlPoints[pointCounter].mData[0],
					-ControlPoints[pointCounter].mData[1], // FIXME: Why is this inverted?
					ControlPoints[pointCounter].mData[2]);
					
				FTransform &Fixup = asset->SplineTransform;
				InputVec = Fixup.TransformPosition(InputVec);

				NewLine.Add(InputVec);

				pointCounter++;
			}
			
			asset->SplineLines.Add(NewLine);

			pointCounter++;
			endPointCounter++;
		}
	}

	if(Line && OwnedLine) {
		Line->Destroy();
		Line = nullptr;
	}
	
	for(int i = 0; i < Node->GetChildCount(); i++) {
		// FIXME: Pass parent transform data down?
		ExtractSplines(Node->GetChild(i), RecursionCount + 1, asset);
	}
}





static void LoadFurFBXData(FString &FileName , UNeoFurAsset *Asset)
{
	// FIXME: I don't know if we should be using C++ standard IO here. Maybe
	// UE4 has something built in. Maybe it doesn't matter.

	std::ifstream inFile(TCHAR_TO_ANSI(*FileName), std::ios::binary);
	if(!inFile.good()) {
		// TODO: Loud error message.
		return;
	}
	
	// Open file. Find length. Make buffer. Fill buffer. Close file.
	inFile.seekg(0, inFile.end);
	size_t fileSize = inFile.tellg();

	if(fileSize == 0) {
		// TODO: REALLY loud error message. Zero length file is bad.
		return;
	}

	inFile.seekg(0, inFile.beg);
	char *Buffer = new char[fileSize + 1];
	Buffer[fileSize] = 0; // I'm just going to terminate this as a string in case we're loading some ASCII FBX and want easy debugging.
	inFile.read(Buffer, fileSize);
	inFile.close();

	FbxManager *Manager = FbxManager::Create();
	FbxScene *Scene = FbxScene::Create(Manager, "Scene");
	FbxImporter *Importer = FbxImporter::Create(Manager, "Importer");
	
	FMemReaderFileInfo Info;
	Info.Data = (void*)Buffer;
	Info.Length = fileSize;

	FMemReaderFbx Stream(Manager);
	Stream.Open((void*)&Info);
	
	Importer->Initialize(TCHAR_TO_ANSI(*FileName));
	Importer->Initialize(&Stream, (void*)&Info);

	int MajorVersion = 0;
	int MinorVersion = 0;
	int Revision = 0;
	Importer->GetFileVersion(MajorVersion, MinorVersion, Revision);
	bool isFbx = Importer->IsFBX();
	
	if(isFbx) {
		if(Importer->Import(Scene)) {
			ExtractSplines(Scene->GetRootNode(), 0, Asset);
		} else {
			// Complain loudly.
			UE_LOG(NeoFur, Error, TEXT("FBX error at scene import: %s"), ANSI_TO_TCHAR(Importer->GetStatus().GetErrorString()));
		}
	} else {
		// Complain loudly.
		UE_LOG(NeoFur, Error, TEXT("FBX error: %s"), ANSI_TO_TCHAR(Importer->GetStatus().GetErrorString()));
	}

	Scene->Destroy();
	
	Manager->Destroy();

	delete[] Buffer;

}

#endif // NEOFUR_FBX

#endif // WITH_EDITOR






