// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshVertexFactory.h"

#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"
#include "RHIStaticStates.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualHeightfieldMeshVertexFactoryParameters, "VirtualHeightfieldMeshVF");

namespace
{
	template< typename T >
	FIndexBufferRHIRef CreateIndexBuffer(int32 NumQuadsPerSide)
	{
		TResourceArray<T, INDEXBUFFER_ALIGNMENT> Indices;

		// Allocate room for indices
		Indices.Reserve(NumQuadsPerSide * NumQuadsPerSide * 6);

		// Build index buffer in morton order for better vertex reuse. This amounts to roughly 75% reuse rate vs 66% of naive scanline approach
		for (int32 Morton = 0; Morton < NumQuadsPerSide * NumQuadsPerSide; Morton++)
		{
			int32 SquareX = FMath::ReverseMortonCode2(Morton);
			int32 SquareY = FMath::ReverseMortonCode2(Morton >> 1);

			bool ForwardDiagonal = false;

#if 0
			// todo: Support odd/even topology for all landscape stuff
 			if (SquareX % 2)
 			{
 				ForwardDiagonal = !ForwardDiagonal;
 			}
 			if (SquareY % 2)
 			{
 				ForwardDiagonal = !ForwardDiagonal;
 			}
#endif
			int32 Index0 = SquareX + SquareY * (NumQuadsPerSide + 1);
			int32 Index1 = Index0 + 1;
			int32 Index2 = Index0 + (NumQuadsPerSide + 1);
			int32 Index3 = Index2 + 1;

			if (!ForwardDiagonal)
			{
				Indices.Add(Index3);
				Indices.Add(Index1);
				Indices.Add(Index0);
				Indices.Add(Index0);
				Indices.Add(Index2);
				Indices.Add(Index3);
			}
			else
			{
				Indices.Add(Index3);
				Indices.Add(Index1);
				Indices.Add(Index2);
				Indices.Add(Index0);
				Indices.Add(Index2);
				Indices.Add(Index1);
			}
		}

		const uint32 Size = Indices.GetResourceDataSize();
		const uint32 Stride = sizeof(T);

		// Create index buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Indices);
		return RHICreateIndexBuffer(Stride, Size, BUF_Static, CreateInfo);
	}
}

void FVirtualHeightfieldMeshIndexBuffer::InitRHI()
{
	NumIndices = NumQuadsPerSide * NumQuadsPerSide * 6;
	if (NumQuadsPerSide < 256)
	{
		IndexBufferRHI = CreateIndexBuffer<uint16>(NumQuadsPerSide);
	}
	else
	{
		IndexBufferRHI = CreateIndexBuffer<uint32>(NumQuadsPerSide);
	}
}


/**
 * Shader parameters for vertex factory.
 */
class FVirtualHeightfieldMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FVirtualHeightfieldMeshVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		InstanceBufferParameter.Bind(ParameterMap, TEXT("InstanceBuffer"));
		HeightTextureParameter.Bind(ParameterMap, TEXT("HeightTexture"));
		HeightSamplerParameter.Bind(ParameterMap, TEXT("HeightSampler"));
		PageTableSizeParameter.Bind(ParameterMap, TEXT("PageTableSize"));
		MaxLodParameter.Bind(ParameterMap, TEXT("MaxLod"));
		VirtualHeightfieldToLocalParameter.Bind(ParameterMap, TEXT("VirtualHeightfieldToLocal"));
		VirtualHeightfieldToWorldParameter.Bind(ParameterMap, TEXT("VirtualHeightfieldToWorld"));
		LodViewOriginParameter.Bind(ParameterMap, TEXT("LodViewOrigin"));
		LodDistancesParameter.Bind(ParameterMap, TEXT("LodDistances"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* InVertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FVirtualHeightfieldMeshVertexFactory* VertexFactory = (FVirtualHeightfieldMeshVertexFactory*)InVertexFactory;

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FVirtualHeightfieldMeshVertexFactoryParameters>(), VertexFactory->GetVirtualHeightfieldMeshVertexFactoryUniformBuffer());

		FVirtualHeightfieldMeshUserData* UserData = (FVirtualHeightfieldMeshUserData*)BatchElement.UserData;
		if (UserData != nullptr)
		{
			if (InstanceBufferParameter.IsBound())
			{
				ShaderBindings.Add(InstanceBufferParameter, UserData->InstanceBufferSRV);
			}
			if (HeightTextureParameter.IsBound() && HeightSamplerParameter.IsBound())
			{
				ShaderBindings.AddTexture(HeightTextureParameter, HeightSamplerParameter, TStaticSamplerState<SF_Bilinear>::GetRHI(), UserData->HeightPhysicalTexture);
			}
			if (PageTableSizeParameter.IsBound())
			{
				ShaderBindings.Add(PageTableSizeParameter, UserData->PageTableSize);
			}
			if (MaxLodParameter.IsBound())
			{
				ShaderBindings.Add(MaxLodParameter, UserData->MaxLod);
			}
			if (VirtualHeightfieldToLocalParameter.IsBound())
			{
				ShaderBindings.Add(VirtualHeightfieldToLocalParameter, UserData->VirtualHeightfieldToLocal);
			}
			if (VirtualHeightfieldToWorldParameter.IsBound())
			{
				ShaderBindings.Add(VirtualHeightfieldToWorldParameter, UserData->VirtualHeightfieldToWorld);
			}
			if (LodViewOriginParameter.IsBound())
			{
				ShaderBindings.Add(LodViewOriginParameter, UserData->LodViewOrigin);
			}
			if (LodDistancesParameter.IsBound())
			{
				ShaderBindings.Add(LodDistancesParameter, UserData->LodDistances);
			}
		}
	}

protected:
	LAYOUT_FIELD(FShaderResourceParameter, InstanceBufferParameter);
	LAYOUT_FIELD(FShaderResourceParameter, HeightTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, HeightSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, PageTableSizeParameter);
	LAYOUT_FIELD(FShaderParameter, MaxLodParameter);
	LAYOUT_FIELD(FShaderParameter, VirtualHeightfieldToLocalParameter);
	LAYOUT_FIELD(FShaderParameter, VirtualHeightfieldToWorldParameter);
	LAYOUT_FIELD(FShaderParameter, LodViewOriginParameter);
	LAYOUT_FIELD(FShaderParameter, LodDistancesParameter);
};

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FVirtualHeightfieldMeshVertexFactory, SF_Vertex, FVirtualHeightfieldMeshVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FVirtualHeightfieldMeshVertexFactory, SF_Pixel, FVirtualHeightfieldMeshVertexFactoryShaderParameters);


FVirtualHeightfieldMeshVertexFactory::FVirtualHeightfieldMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumQuadsPerSide)
	: FVertexFactory(InFeatureLevel)
	, NumQuadsPerSide(InNumQuadsPerSide)
{
	IndexBuffer = new FVirtualHeightfieldMeshIndexBuffer(NumQuadsPerSide);
}

FVirtualHeightfieldMeshVertexFactory::~FVirtualHeightfieldMeshVertexFactory()
{
	delete IndexBuffer;
}

void FVirtualHeightfieldMeshVertexFactory::InitRHI()
{
	// Setup the uniform data
	FVirtualHeightfieldMeshVertexFactoryParameters UniformParams;
	UniformParams.NumQuadsPerTileSide = NumQuadsPerSide;
	UniformBuffer = FVirtualHeightfieldMeshVertexFactoryBufferRef::CreateUniformBufferImmediate(UniformParams, UniformBuffer_MultiFrame);

	IndexBuffer->InitResource();

	FVertexStream NullVertexStream;
	NullVertexStream.VertexBuffer = nullptr;
	NullVertexStream.Stride = 0;
	NullVertexStream.Offset = 0;
	NullVertexStream.VertexStreamUsage = EVertexStreamUsage::ManualFetch;

	check(Streams.Num() == 0);
	Streams.Add(NullVertexStream);

	FVertexDeclarationElementList Elements;
	//Elements.Add(FVertexElement(0, 0, VET_None, 0, 0, false));

#if 0
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel);

	// Add support for primitiveID
	const uint8 Index = static_cast<uint8>(EVertexInputStreamType::Default);
	PrimitiveIdStreamIndex[Index] = -1;
	if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
	{
		// When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
		Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, sizeof(uint32), VET_UInt, EVertexStreamUsage::Instancing), 13));
		PrimitiveIdStreamIndex[Index] = Elements.Last().StreamIndex;
	}
#endif

	InitDeclaration(Elements);
}

void FVirtualHeightfieldMeshVertexFactory::ReleaseRHI()
{
	UniformBuffer.SafeRelease();

	if (IndexBuffer)
	{
		IndexBuffer->ReleaseResource();
	}

	FVertexFactory::ReleaseRHI();
}

bool FVirtualHeightfieldMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	//todo[vhm]: Fallback path for mobile.
	if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
	{
		return false;
	}
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithVirtualHeightfieldMesh) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FVirtualHeightfieldMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("VF_VIRTUAL_HEIGHFIELD_MESH"), 1);
#if 0
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform)));
#endif
}

void FVirtualHeightfieldMeshVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
#if 0
	if (Type->SupportsPrimitiveIdStream()
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform))
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(Parameters.PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
	}
#endif
}

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FVirtualHeightfieldMeshVertexFactory, "/Plugin/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshVertexFactory.ush", true, false, true, false, false, false, true);
