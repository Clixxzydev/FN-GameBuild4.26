// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Engine/Texture.h"
#include "TextureResource.h"
#include "RenderAssetUpdate.h"
#include "Serialization/BulkData2.h"
#include "Texture2D.generated.h"

class FTexture2DResourceMem;

UCLASS(hidecategories=Object, MinimalAPI, BlueprintType)
class UTexture2D : public UTexture
{
	GENERATED_UCLASS_BODY()

public:

	/*
	 * Level scope index of this texture. It is used to reduce the amount of lookup to map a texture to its level index.
	 * Useful when building texture streaming data, as well as when filling the texture streamer with precomputed data.
     * It relates to FStreamingTextureBuildInfo::TextureLevelIndex and also the index in ULevel::StreamingTextureGuids. 
	 * Default value of -1, indicates that the texture has an unknown index (not yet processed). At level load time, 
	 * -2 is also used to indicate that the texture has been processed but no entry were found in the level table.
	 * After any of these processes, the LevelIndex is reset to INDEX_NONE. Making it ready for the next level task.
	 */
	UPROPERTY(transient, duplicatetransient, NonTransactional)
	int32 LevelIndex;

	/** keep track of first mip level used for ResourceMem creation */
	UPROPERTY()
	int32 FirstResourceMemMip;

public:
	/**
	 * Retrieves the size of the source image from which the texture was created.
	 */
	FORCEINLINE FIntPoint GetImportedSize() const
	{
#if WITH_EDITOR
		return Source.GetLogicalSize();
#else // #if WITH_EDITOR
		return ImportedSize;
#endif // #if WITH_EDITOR
	}

private:
	/** True if streaming is temporarily disabled so we can update subregions of this texture's resource 
	without streaming clobbering it. Automatically cleared before saving. */
	UPROPERTY(transient)
	uint8 bTemporarilyDisableStreaming:1;

public:
#if WITH_EDITORONLY_DATA
	/** Whether the texture has been painted in the editor.						*/
	UPROPERTY()
	uint8 bHasBeenPaintedInEditor:1;
#endif // WITH_EDITORONLY_DATA

	/** The addressing mode to use for the X axis.								*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, meta=(DisplayName="X-axis Tiling Method"), AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureAddress> AddressX;

	/** The addressing mode to use for the Y axis.								*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Texture, meta=(DisplayName="Y-axis Tiling Method"), AssetRegistrySearchable, AdvancedDisplay)
	TEnumAsByte<enum TextureAddress> AddressY;

private:
	/**
	 * The imported size of the texture. Only valid on cooked builds when texture source is not
	 * available. Access ONLY via the GetImportedSize() accessor!
	 */
	UPROPERTY()
	FIntPoint ImportedSize;

public:
	/** The derived data for this texture on this platform. */
	FTexturePlatformData *PlatformData;
#if WITH_EDITOR
	/* cooked platform data for this texture */
	TMap<FString, FTexturePlatformData*> CookedPlatformData;
#endif

	/** memory used for directly loading bulk mip data */
	FTexture2DResourceMem*		ResourceMem;

protected:

	/** Helper to manage the current pending update following a call to StreamIn() or StreamOut(). */
	TRefCountPtr<FRenderAssetUpdate> PendingUpdate;
	friend class FTexture2DUpdate;

public:

	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;	
#if WITH_EDITOR
	virtual void PostLinkerChange() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void BeginDestroy() override;
	virtual bool IsReadyForAsyncPostLoad() const override;
	virtual void PostLoad() override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual FString GetDesc() override;
	//~ End UObject Interface.

	//~ Begin UTexture Interface.
	virtual float GetSurfaceWidth() const override { return GetSizeX(); }
	virtual float GetSurfaceHeight() const override { return GetSizeY(); }
	virtual FTextureResource* CreateResource() override;
	virtual EMaterialValueType GetMaterialType() const override;
	virtual void UpdateResource() override;
	virtual float GetAverageBrightness(bool bIgnoreTrueBlack, bool bUseGrayscale) override;
	virtual FTexturePlatformData** GetRunningPlatformData() final override { return &PlatformData; }
#if WITH_EDITOR
	virtual TMap<FString,FTexturePlatformData*>* GetCookedPlatformData() override { return &CookedPlatformData; }
#endif
	//~ End UTexture Interface.

	//~ Begin UStreamableRenderAsset Interface
	virtual int32 GetNumMipsForStreaming() const final override { return GetNumMips(); }
	virtual int32 GetNumNonStreamingMips() const final override;
	virtual int32 CalcNumOptionalMips() const final override;
	virtual int32 CalcCumulativeLODSize(int32 NumLODs) const final override { return CalcTextureMemorySize(NumLODs); }
	virtual FIoFilenameHash GetMipIoFilenameHash(const int32 MipIndex) const final override;
	virtual bool DoesMipDataExist(const int32 MipIndex) const final override;
	
	/**
	* Returns whether the texture is ready for streaming aka whether it has had InitRHI called on it.
	*
	* @return true if initialized and ready for streaming, false otherwise
	*/
	virtual bool IsReadyForStreaming() const final override
	{
		FTexture2DResource* Texture2DResource = (FTexture2DResource*)Resource;
		return Texture2DResource && Texture2DResource->bReadyForStreaming;
	}

	ENGINE_API virtual int32 GetNumResidentMips() const final override;
	virtual int32 GetNumRequestedMips() const final override;
	virtual bool CancelPendingMipChangeRequest() final override;

	/** true if the texture is currently being updated through StreamIn() or StreamOut(). */
	virtual bool HasPendingUpdate() const final override { return PendingUpdate != nullptr; }
	virtual bool IsPendingUpdateLocked() const final override;

	virtual bool StreamOut(int32 NewMipCount) final override;
	virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;
	virtual bool UpdateStreamingStatus(bool bWaitForMipFading = false, TArray<UStreamableRenderAsset*>* DeferredTickCBAssets = nullptr) final override;
	virtual void InvalidateLastRenderTimeForStreaming() final override;
	virtual float GetLastRenderTimeForStreaming() const final override;
	virtual bool ShouldMipLevelsBeForcedResident() const final override;
	//~ End UStreamableRenderAsset Interface

	/** Trivial accessors. */
	FORCEINLINE int32 GetSizeX() const
	{
		if (PlatformData)
		{
			return PlatformData->SizeX;
		}
		return 0;
	}
	FORCEINLINE int32 GetSizeY() const
	{
		if (PlatformData)
		{
			return PlatformData->SizeY;
		}
		return 0;
	}
	FORCEINLINE int32 GetNumMips() const
	{
		if (PlatformData)
		{
			if (IsCurrentlyVirtualTextured())
			{
				return PlatformData->GetNumVTMips();
			}
			return PlatformData->Mips.Num();
		}
		return 0;
	}

	FORCEINLINE EPixelFormat GetPixelFormat(uint32 LayerIndex = 0u) const
	{
		if (PlatformData)
		{
			return PlatformData->GetLayerPixelFormat(LayerIndex);
		}
		return PF_Unknown;
	}
	FORCEINLINE int32 GetMipTailBaseIndex() const
	{
		if (PlatformData)
		{
			const int32 NumMipsInTail = PlatformData->GetNumMipsInTail();
			return FMath::Max(0, NumMipsInTail > 0 ? (PlatformData->Mips.Num() - NumMipsInTail) : (PlatformData->Mips.Num() - 1));
		}
		return 0;
	}
	FORCEINLINE const TIndirectArray<FTexture2DMipMap>& GetPlatformMips() const
	{
		check(PlatformData);
		return PlatformData->Mips;
	}
	FORCEINLINE int32 GetExtData() const
	{
		if (PlatformData)
		{
			return PlatformData->GetExtData();
		}
		return 0;
	}

	FORCEINLINE int32 GetStreamingIndex() const { return StreamingIndex; }

	/**
	 * Calculates the maximum number of mips the engine allows to be loaded for this texture. 
	 * The cinematic mips will be considered as loadable, streaming enabled or not.
	 * Note that in the cooking process, mips smaller than the min residency count
	 * can be stripped out by the cooker.
	 *
	 * @param bIgnoreMinResidency - Whether to ignore min residency limitations.
	 * @return The maximum allowed number mips for this texture.
	 */
	ENGINE_API int32 GetNumMipsAllowed(bool bIgnoreMinResidency) const;

private:
	/** The minimum number of mips that must be resident in memory (cannot be streamed). */
	static ENGINE_API int32 GMinTextureResidentMipCount;

public:
	/** Returns the minimum number of mips that must be resident in memory (cannot be streamed). */
	FORCEINLINE int32 GetMinTextureResidentMipCount() const
	{
		return FMath::Max(GMinTextureResidentMipCount, PlatformData ? (int32)PlatformData->GetNumMipsInTail() : 0);
	}

	/** Returns the minimum number of mips that must be resident in memory (cannot be streamed). */
	static FORCEINLINE int32 GetStaticMinTextureResidentMipCount()
	{
		return GMinTextureResidentMipCount;
	}

	/** Sets the minimum number of mips that must be resident in memory (cannot be streamed). */
	static void SetMinTextureResidentMipCount(int32 InMinTextureResidentMipCount);

	/**
	 * Get mip data starting with the specified mip index.
	 * @param FirstMipToLoad - The first mip index to cache.
	 * @param OutMipData -	Must point to an array of pointers with at least
	 *						Mips.Num() - FirstMipToLoad + 1 entries. Upon
	 *						return those pointers will contain mip data.
	 */
	ENGINE_API void GetMipData(int32 FirstMipToLoad, void** OutMipData);

	/**
	 * Computes the minimum and maximum allowed mips for a texture.
	 * @param MipCount - The number of mip levels in the texture.
	 * @param NumNonStreamingMips - The number of mip levels that are not allowed to stream.
	 * @param LODBias - Bias applied to the number of mip levels.
	 * @param OutMinAllowedMips - Returns the minimum number of mip levels that must be loaded.
	 * @param OutMaxAllowedMips - Returns the maximum number of mip levels that must be loaded.
	 */
	static void CalcAllowedMips( int32 MipCount, int32 NumNonStreamingMips, int32 LODBias, int32& OuMinAllowedMips, int32& OutMaxAllowedMips );

	/**
	 * Calculates the size of this texture in bytes if it had MipCount miplevels streamed in.
	 *
	 * @param	MipCount	Number of mips to calculate size for, counting from the smallest 1x1 mip-level and up.
	 * @return	Size of MipCount mips in bytes
	 */
	int32 CalcTextureMemorySize( int32 MipCount ) const;

	/**
	 * Calculates the size of this texture if it had MipCount miplevels streamed in.
	 *
	 * @param	Enum	Which mips to calculate size for.
	 * @return	Total size of all specified mips, in bytes
	 */
	virtual uint32 CalcTextureMemorySizeEnum( ETextureMipCount Enum ) const override;

	/**
	 *	Get the CRC of the source art pixels.
	 *
	 *	@param	[out]	OutSourceCRC		The CRC value of the source art pixels.
	 *
	 *	@return			bool				true if successful, false if failed (or no source art)
	 */
	ENGINE_API bool GetSourceArtCRC(uint32& OutSourceCRC);

	/**
	 *	See if the source art of the two textures matches...
	 *
	 *	@param		InTexture		The texture to compare it to
	 *
	 *	@return		bool			true if they matche, false if not
	 */
	ENGINE_API bool HasSameSourceArt(UTexture2D* InTexture);
	
	/**
	 * Returns true if the runtime texture has an alpha channel that is not completely white.
	 */
	ENGINE_API bool HasAlphaChannel() const;

	/**
	 * Waits until all streaming requests for this texture has been fully processed.
	 */
	virtual void WaitForStreaming() override;

	/**
	 * Returns the size of the object/ resource for display to artists/ LDs in the Editor.
	 *
	 * @return size of resource as to be displayed to artists/ LDs in the Editor.
	 */
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;

	/**
	 * Whether all miplevels of this texture have been fully streamed in, LOD settings permitting.
	 */
	ENGINE_API bool IsFullyStreamedIn();

	/**
	 * Links texture to the texture streaming manager.
	 */
	void LinkStreaming();

	/**
	 * Unlinks texture from the texture streaming manager.
	 */
	void UnlinkStreaming();
	
	/**
	 * Cancels any pending texture streaming actions if possible.
	 * Returns when no more async loading requests are in flight.
	 */
	ENGINE_API static void CancelPendingTextureStreaming();


	/**
	 * Returns the global mip map bias applied as an offset for 2d textures.
	 */
	ENGINE_API static float GetGlobalMipMapLODBias();

	/**
	 * Calculates and returns the corresponding ResourceMem parameters for this texture.
	 *
	 * @param FirstMipIdx		Index of the largest mip-level stored within a seekfree (level) package
	 * @param OutSizeX			[out] Width of the stored largest mip-level
	 * @param OutSizeY			[out] Height of the stored largest mip-level
	 * @param OutNumMips		[out] Number of stored mips
	 * @param OutTexCreateFlags	[out] ETextureCreateFlags bit flags
	 * @return					true if the texture should use a ResourceMem. If false, none of the out parameters will be filled in.
	 */
	bool GetResourceMemSettings(int32 FirstMipIdx, int32& OutSizeX, int32& OutSizeY, int32& OutNumMips, uint32& OutTexCreateFlags);

	/**
	*	Asynchronously update a set of regions of a texture with new data.
	*	@param MipIndex - the mip number to update
	*	@param NumRegions - number of regions to update
	*	@param Regions - regions to update
	*	@param SrcPitch - the pitch of the source data in bytes
	*	@param SrcBpp - the size one pixel data in bytes
	*	@param SrcData - the source data
	*  @param bFreeData - if true, the SrcData and Regions pointers will be freed after the update.
	*/
	ENGINE_API void UpdateTextureRegions(int32 MipIndex, uint32 NumRegions, const FUpdateTextureRegion2D* Regions, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData, TFunction<void(uint8* SrcData, const FUpdateTextureRegion2D* Regions)> DataCleanupFunc = [](uint8*, const FUpdateTextureRegion2D*){});

#if WITH_EDITOR
	
	/**
	 * Temporarily disable streaming so we update subregions of this texture without streaming clobbering it. 
	 */
	ENGINE_API void TemporarilyDisableStreaming();

	/** Called after an editor or undo operation is formed on texture
	*/
	virtual void PostEditUndo() override;

#endif // WITH_EDITOR

	friend struct FRenderAssetStreamingManager;
	friend struct FStreamingRenderAsset;
	
	/** creates and initializes a new Texture2D with the requested settings */
	ENGINE_API static class UTexture2D* CreateTransient(int32 InSizeX, int32 InSizeY, EPixelFormat InFormat = PF_B8G8R8A8, const FName InName = NAME_None);

	/**
	 * Gets the X size of the texture, in pixels
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "GetSizeX"), Category="Rendering|Texture")
	int32 Blueprint_GetSizeX() const;

	/**
	 * Gets the Y size of the texture, in pixels
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "GetSizeY"), Category="Rendering|Texture")
	int32 Blueprint_GetSizeY() const;

	/**
	 * Update the offset for mip map lod bias.
	 * This is added to any existing mip bias values.
	 */
	virtual void RefreshSamplerStates();

	/**
	 * Returns if the texture is actually being rendered using virtual texturing right now.
	 * Unlike the 'VirtualTextureStreaming' property which reflects the user's desired state
	 * this reflects the actual current state on the renderer depending on the platform, VT
	 * data being built, project settings, ....
	 */
	virtual bool IsCurrentlyVirtualTextured() const override
	{
		if (VirtualTextureStreaming && PlatformData && PlatformData->VTData)
		{
			return true;
		}
		return false;
	}

	/**
	 * Returns true if this virtual texture uses a single physical space all of its texture layers.
	 * This can reduce page table overhead but potentially increase the number of physical pools allocated.
	 */
	virtual bool IsVirtualTexturedWithSinglePhysicalSpace() const { return false;  }
};
