// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IO/IoContainerId.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Templates/RefCounting.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/TypeCompatibleBytes.h"
#include "HAL/PlatformAtomics.h"
#include "Misc/SecureHash.h"
#include "Misc/AES.h"
#include "Misc/IEngineCrypto.h"

class FIoRequest;
class FIoDispatcher;
class FIoStoreWriter;
class FIoStoreEnvironment;

class FIoRequestImpl;
class FIoBatchImpl;
class FIoDispatcherImpl;
class FIoStoreWriterContextImpl;
class FIoStoreWriterImpl;
class FIoStoreReaderImpl;
class IMappedFileHandle;
class IMappedFileRegion;

CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIoDispatcher, Log, All);

/*
 * I/O error code.
 */
enum class EIoErrorCode
{
	Ok,
	Unknown,
	InvalidCode,
	Cancelled,
	FileOpenFailed,
	FileNotOpen,
	WriteError,
	NotFound,
	CorruptToc,
	UnknownChunkID,
	InvalidParameter,
	SignatureError
};

/**
 * I/O status with error code and message.
 */
class FIoStatus
{
public:
	CORE_API			FIoStatus();
	CORE_API			~FIoStatus();

	CORE_API			FIoStatus(EIoErrorCode Code, const FStringView& InErrorMessage);
	CORE_API			FIoStatus(EIoErrorCode Code);
	CORE_API FIoStatus&	operator=(const FIoStatus& Other);
	CORE_API FIoStatus&	operator=(const EIoErrorCode InErrorCode);

	CORE_API bool		operator==(const FIoStatus& Other) const;
			 bool		operator!=(const FIoStatus& Other) const { return !operator==(Other); }

	inline bool			IsOk() const { return ErrorCode == EIoErrorCode::Ok; }
	inline bool			IsCompleted() const { return ErrorCode != EIoErrorCode::Unknown; }
	inline EIoErrorCode	GetErrorCode() const { return ErrorCode; }
	CORE_API FString	ToString() const;

	CORE_API static const FIoStatus Ok;
	CORE_API static const FIoStatus Unknown;
	CORE_API static const FIoStatus Invalid;

private:
	static constexpr int32 MaxErrorMessageLength = 128;
	using FErrorMessage = TCHAR[MaxErrorMessageLength];

	EIoErrorCode	ErrorCode = EIoErrorCode::Ok;
	FErrorMessage	ErrorMessage;

	friend class FIoStatusBuilder;
};

/**
 * Helper to make it easier to generate meaningful error messages.
 */
class FIoStatusBuilder
{
	EIoErrorCode		StatusCode;
	FString				Message;
public:
	CORE_API explicit	FIoStatusBuilder(EIoErrorCode StatusCode);
	CORE_API			FIoStatusBuilder(const FIoStatus& InStatus, FStringView String);
	CORE_API			~FIoStatusBuilder();

	CORE_API			operator FIoStatus();

	CORE_API FIoStatusBuilder& operator<<(FStringView String);
};

CORE_API FIoStatusBuilder operator<<(const FIoStatus& Status, FStringView String);

/**
 * Optional I/O result or error status.
 */
template<typename T>
class TIoStatusOr
{
	template<typename U> friend class TIoStatusOr;

public:
	TIoStatusOr() : StatusValue(FIoStatus::Unknown) { }
	TIoStatusOr(const TIoStatusOr& Other);
	TIoStatusOr(TIoStatusOr&& Other);

	TIoStatusOr(FIoStatus InStatus);
	TIoStatusOr(const T& InValue);
	TIoStatusOr(T&& InValue);

	~TIoStatusOr();

	template <typename... ArgTypes>
	explicit TIoStatusOr(ArgTypes&&... Args);

	template<typename U>
	TIoStatusOr(const TIoStatusOr<U>& Other);

	TIoStatusOr<T>& operator=(const TIoStatusOr<T>& Other);
	TIoStatusOr<T>& operator=(TIoStatusOr<T>&& Other);
	TIoStatusOr<T>& operator=(const FIoStatus& OtherStatus);
	TIoStatusOr<T>& operator=(const T& OtherValue);
	TIoStatusOr<T>& operator=(T&& OtherValue);

	template<typename U>
	TIoStatusOr<T>& operator=(const TIoStatusOr<U>& Other);

	const FIoStatus&	Status() const;
	bool				IsOk() const;

	const T&			ValueOrDie();
	T					ConsumeValueOrDie();

	void				Reset();

private:
	FIoStatus				StatusValue;
	TTypeCompatibleBytes<T>	Value;
};

CORE_API void StatusOrCrash(const FIoStatus& Status);

template<typename T>
void TIoStatusOr<T>::Reset()
{
	EIoErrorCode ErrorCode = StatusValue.GetErrorCode();
	StatusValue = EIoErrorCode::Unknown;

	if (ErrorCode == EIoErrorCode::Ok)
	{
		((T*)&Value)->~T();
	}
}

template<typename T>
const T& TIoStatusOr<T>::ValueOrDie()
{
	if (!StatusValue.IsOk())
	{
		StatusOrCrash(StatusValue);
	}

	return *Value.GetTypedPtr();
}

template<typename T>
T TIoStatusOr<T>::ConsumeValueOrDie()
{
	if (!StatusValue.IsOk())
	{
		StatusOrCrash(StatusValue);
	}

	StatusValue = FIoStatus::Unknown;

	return MoveTemp(*Value.GetTypedPtr());
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(const TIoStatusOr& Other)
{
	StatusValue = Other.StatusValue;
	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const T*)&Other.Value);
	}
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(TIoStatusOr&& Other)
{
	StatusValue = Other.StatusValue;
	if (StatusValue.IsOk())
	{
		new(&Value) T(MoveTempIfPossible(*(T*)&Other.Value));
		Other.StatusValue = EIoErrorCode::Unknown;
	}
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(FIoStatus InStatus)
{
	check(!InStatus.IsOk());
	StatusValue = InStatus;
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(const T& InValue)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(InValue);
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(T&& InValue)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(MoveTempIfPossible(InValue));
}

template <typename T>
template <typename... ArgTypes>
TIoStatusOr<T>::TIoStatusOr(ArgTypes&&... Args)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(Forward<ArgTypes>(Args)...);
}

template<typename T>
TIoStatusOr<T>::~TIoStatusOr()
{
	Reset();
}

template<typename T>
bool TIoStatusOr<T>::IsOk() const
{
	return StatusValue.IsOk();
}

template<typename T>
const FIoStatus& TIoStatusOr<T>::Status() const
{
	return StatusValue;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(const TIoStatusOr<T>& Other)
{
	if (&Other != this)
	{
		Reset();

		if (Other.StatusValue.IsOk())
		{
			new(&Value) T(*(const T*)&Other.Value);
			StatusValue = EIoErrorCode::Ok;
		}
		else
		{
			StatusValue = Other.StatusValue;
		}
	}

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(TIoStatusOr<T>&& Other)
{
	if (&Other != this)
	{
		Reset();
 
		if (Other.StatusValue.IsOk())
		{
			new(&Value) T(MoveTempIfPossible(*(T*)&Other.Value));
			Other.StatusValue = EIoErrorCode::Unknown;
			StatusValue = EIoErrorCode::Ok;
		}
		else
		{
			StatusValue = Other.StatusValue;
		}
	}

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(const FIoStatus& OtherStatus)
{
	check(!OtherStatus.IsOk());

	Reset();
	StatusValue = OtherStatus;

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(const T& OtherValue)
{
	if (&OtherValue != (T*)&Value)
	{
		Reset();
		
		new(&Value) T(OtherValue);
		StatusValue = EIoErrorCode::Ok;
	}

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(T&& OtherValue)
{
	if (&OtherValue != (T*)&Value)
	{
		Reset();
		
		new(&Value) T(MoveTempIfPossible(OtherValue));
		StatusValue = EIoErrorCode::Ok;
	}

	return *this;
}

template<typename T>
template<typename U>
TIoStatusOr<T>::TIoStatusOr(const TIoStatusOr<U>& Other)
:	StatusValue(Other.StatusValue)
{
	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const U*)&Other.Value);
	}
}

template<typename T>
template<typename U>
TIoStatusOr<T>& TIoStatusOr<T>::operator=(const TIoStatusOr<U>& Other)
{
	Reset();

	if (Other.StatusValue.IsOk())
	{
		new(&Value) T(*(const U*)&Other.Value);
		StatusValue = EIoErrorCode::Ok;
	}
	else
	{
		StatusValue = Other.StatusValue;
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////

/** Helper used to manage creation of I/O store file handles etc
  */
class FIoStoreEnvironment
{
public:
	CORE_API FIoStoreEnvironment();
	CORE_API ~FIoStoreEnvironment();

	CORE_API void InitializeFileEnvironment(FStringView InPath, int32 InOrder = 0);

	CORE_API const FString& GetPath() const { return Path; }
	CORE_API int32 GetOrder() const { return Order; }

private:
	FString			Path;
	int32			Order = 0;
};

/** Reference to buffer data used by I/O dispatcher APIs
  */
class FIoBuffer
{
public:
	enum EAssumeOwnershipTag	{ AssumeOwnership };
	enum ECloneTag				{ Clone };
	enum EWrapTag				{ Wrap };

	CORE_API			FIoBuffer();
	CORE_API explicit	FIoBuffer(uint64 InSize);
	CORE_API			FIoBuffer(const void* Data, uint64 InSize, const FIoBuffer& OuterBuffer);

	CORE_API			FIoBuffer(EAssumeOwnershipTag,	const void* Data, uint64 InSize);
	CORE_API			FIoBuffer(ECloneTag,			const void* Data, uint64 InSize);
	CORE_API			FIoBuffer(EWrapTag,				const void* Data, uint64 InSize);

	// Note: we currently rely on implicit move constructor, thus we do not declare any
	//		 destructor or copy/assignment operators or copy constructors

	inline const uint8*	Data() const			{ return CorePtr->Data(); }
	inline uint8*		Data()					{ return CorePtr->Data(); }
	inline uint64		DataSize() const		{ return CorePtr->DataSize(); }

	inline void			SetSize(uint64 InSize)	{ return CorePtr->SetSize(InSize); }

	inline bool			IsMemoryOwned() const	{ return CorePtr->IsMemoryOwned(); }

	inline void			EnsureOwned() const		{ if (!CorePtr->IsMemoryOwned()) { MakeOwned(); } }

	CORE_API void		MakeOwned() const;
	
	/**
	 * Relinquishes control of the internal buffer to the caller and removes it from the FIoBuffer.
	 * This allows the caller to assume ownership of the internal data and prevent it from being deleted along with 
	 * the FIoBuffer.
	 *
	 * NOTE: It is only valid to call this if the FIoBuffer currently owns the internal memory allocation, as the 
	 * point of the call is to take ownership of it. If the FIoBuffer is only wrapping the allocation then it will
	 * return a failed FIoStatus instead.
	 *
	 * @return A status wrapper around the memory pointer. Even if the status is valid the pointer might still be null.
	 */
	UE_NODISCARD CORE_API TIoStatusOr<uint8*> Release();

private:
	/** Core buffer object. For internal use only, used by FIoBuffer

		Contains all state pertaining to a buffer.
	  */
	struct BufCore
	{
					BufCore();
		CORE_API	~BufCore();

		explicit	BufCore(uint64 InSize);
					BufCore(const uint8* InData, uint64 InSize, bool InOwnsMemory);
					BufCore(const uint8* InData, uint64 InSize, const BufCore* InOuter);
					BufCore(ECloneTag, uint8* InData, uint64 InSize);

					BufCore(const BufCore& Rhs) = delete;
		
		BufCore& operator=(const BufCore& Rhs) = delete;

		inline uint8* Data()			{ return DataPtr; }
		inline uint64 DataSize() const	{ return DataSizeLow | (uint64(DataSizeHigh) << 32); }

		//

		void	SetDataAndSize(const uint8* InData, uint64 InSize);
		void	SetSize(uint64 InSize);

		void	MakeOwned();

		TIoStatusOr<uint8*> ReleaseMemory();

		inline void SetIsOwned(bool InOwnsMemory)
		{
			if (InOwnsMemory)
			{
				Flags |= OwnsMemory;
			}
			else
			{
				Flags &= ~OwnsMemory;
			}
		}

		inline uint32 AddRef() const
		{
			return uint32(FPlatformAtomics::InterlockedIncrement(&NumRefs));
		}

		inline uint32 Release() const
		{
#if DO_CHECK
			CheckRefCount();
#endif

			const int32 Refs = FPlatformAtomics::InterlockedDecrement(&NumRefs);
			if (Refs == 0)
			{
				delete this;
			}

			return uint32(Refs);
		}

		uint32 GetRefCount() const
		{
			return uint32(NumRefs);
		}

		bool IsMemoryOwned() const	{ return Flags & OwnsMemory; }

	private:
		CORE_API void				CheckRefCount() const;

		uint8*						DataPtr = nullptr;

		uint32						DataSizeLow = 0;
		mutable int32				NumRefs = 0;

		// Reference-counted outer "core", used for views into other buffer
		//
		// Ultimately this should probably just be an index into a pool
		TRefCountPtr<const BufCore>	OuterCore;

		// TODO: These two could be packed in the MSB of DataPtr on x64
		uint8		DataSizeHigh = 0;	// High 8 bits of size (40 bits total)
		uint8		Flags = 0;

		enum
		{
			OwnsMemory		= 1 << 0,	// Buffer memory is owned by this instance
			ReadOnlyBuffer	= 1 << 1,	// Buffer memory is immutable
			
			FlagsMask		= (1 << 2) - 1
		};

		void EnsureDataIsResident() {}

		void ClearFlags()
		{
			Flags = 0;
		}
	};

	// Reference-counted "core"
	//
	// Ultimately this should probably just be an index into a pool
	TRefCountPtr<BufCore>	CorePtr;
	
	friend class FIoBufferManager;
};

class FIoChunkHash
{
public:
	friend uint32 GetTypeHash(const FIoChunkHash& InChunkHash)
	{
		uint32 Result = 5381;
		for (int i = 0; i < sizeof Hash; ++i)
		{
			Result = Result * 33 + InChunkHash.Hash[i];
		}
		return Result;
	}

	friend FArchive& operator<<(FArchive& Ar, FIoChunkHash& ChunkHash)
	{
		Ar.Serialize(&ChunkHash.Hash, sizeof Hash);
		return Ar;
	}

	inline bool operator ==(const FIoChunkHash& Rhs) const
	{
		return 0 == FMemory::Memcmp(Hash, Rhs.Hash, sizeof Hash);
	}

	inline bool operator !=(const FIoChunkHash& Rhs) const
	{
		return !(*this == Rhs);
	}

	static FIoChunkHash HashBuffer(const void* Data, uint64 DataSize)
	{
		FIoChunkHash Result;
		FSHA1::HashBuffer(Data, DataSize, Result.Hash);
		FMemory::Memset(Result.Hash + 20, 0, 12);
		return Result;
	}

private:
	uint8	Hash[32];
};

/**
 * Identifier to a chunk of data.
 */
class FIoChunkId
{
public:
	CORE_API static const FIoChunkId InvalidChunkId;

	friend uint32 GetTypeHash(FIoChunkId InId)
	{
		uint32 Hash = 5381;
		for (int i = 0; i < sizeof Id; ++i)
		{
			Hash = Hash * 33 + InId.Id[i];
		}
		return Hash;
	}

	friend FArchive& operator<<(FArchive& Ar, FIoChunkId& ChunkId)
	{
		Ar.Serialize(&ChunkId.Id, sizeof Id);
		return Ar;
	}

	inline bool operator ==(const FIoChunkId& Rhs) const
	{
		return 0 == FMemory::Memcmp(Id, Rhs.Id, sizeof Id);
	}

	inline bool operator !=(const FIoChunkId& Rhs) const
	{
		return !(*this == Rhs);
	}

	void Set(const void* InIdPtr, SIZE_T InSize)
	{
		check(InSize == sizeof Id);
		FMemory::Memcpy(Id, InIdPtr, sizeof Id);
	}

	inline bool IsValid() const
	{
		return *this != InvalidChunkId;
	}

private:
	static inline FIoChunkId CreateEmptyId()
	{
		FIoChunkId ChunkId;
		uint8 Data[12] = { 0 };
		ChunkId.Set(Data, sizeof Data);

		return ChunkId;
	}

	uint8	Id[12];
};

/**
 * Addressable chunk types.
 */
enum class EIoChunkType : uint8
{
	Invalid,
	InstallManifest,
	ExportBundleData,
	BulkData,
	OptionalBulkData,
	MemoryMappedBulkData,
	LoaderGlobalMeta,
	LoaderInitialLoadMeta,
	LoaderGlobalNames,
	LoaderGlobalNameHashes,
	ContainerHeader
};

/**
 * Creates a chunk identifier,
 */
static FIoChunkId CreateIoChunkId(uint64 ChunkId, uint16 ChunkIndex, EIoChunkType IoChunkType)
{
	uint8 Data[12] = {0};

	*reinterpret_cast<uint64*>(&Data[0]) = ChunkId;
	*reinterpret_cast<uint16*>(&Data[8]) = ChunkIndex;
	*reinterpret_cast<uint8*>(&Data[11]) = static_cast<uint8>(IoChunkType);

	FIoChunkId IoChunkId;
	IoChunkId.Set(Data, 12);

	return IoChunkId;
}

//////////////////////////////////////////////////////////////////////////

class FIoReadOptions
{
public:
	FIoReadOptions() = default;

	FIoReadOptions(uint64 InOffset, uint64 InSize)
		: RequestedOffset(InOffset)
		, RequestedSize(InSize)
	{ }

	~FIoReadOptions() = default;

	void SetRange(uint64 Offset, uint64 Size)
	{
		RequestedOffset = Offset;
		RequestedSize	= Size;
	}

	void SetTargetVa(void* InTargetVa)
	{
		TargetVa = InTargetVa;
	}

	uint64 GetOffset() const
	{
		return RequestedOffset;
	}

	uint64 GetSize() const
	{
		return RequestedSize;
	}

	void* GetTargetVa() const
	{
		return TargetVa;
	}

private:
	uint64	RequestedOffset = 0;
	uint64	RequestedSize = ~uint64(0);
	void* TargetVa = nullptr;
	uint32	Flags = 0;
};

//////////////////////////////////////////////////////////////////////////

class FIoBatchReadOptions
{
public:
	FIoBatchReadOptions() = default;

	void SetTargetVa(void* InTargetVa)
	{
		TargetVa = InTargetVa;
	}

	void* GetTargetVa() const
	{
		return TargetVa;
	}

private:
	void* TargetVa = nullptr;
};

//////////////////////////////////////////////////////////////////////////

/**
  */
class FIoRequest
{
public:
	FIoRequest() = default;

	FIoRequest(const FIoRequest&) = default;
	FIoRequest& operator=(const FIoRequest&) = default;

	CORE_API bool							IsOk() const;
	CORE_API FIoStatus						Status() const;
	CORE_API const FIoChunkId&				GetChunkId() const;
	CORE_API TIoStatusOr<FIoBuffer>			GetResult() const;

private:
	FIoRequestImpl* Impl = nullptr;

	explicit FIoRequest(FIoRequestImpl* InImpl)
	: Impl(InImpl)
	{
	}

	friend class FIoDispatcher;
	friend class FIoDispatcherImpl;
	friend class FIoBatch;
};

using FIoReadCallback = TFunction<void(TIoStatusOr<FIoBuffer>)>;

enum EIoDispatcherPriority : uint8
{
	IoDispatcherPriority_Low,
	IoDispatcherPriority_Medium,
	IoDispatcherPriority_High,

	IoDispatcherPriority_Count,
};

/** I/O batch

	This is a primitive used to group I/O requests for synchronization
	purposes
  */
class FIoBatch
{
	friend class FIoDispatcher;

	FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl);

public:
	FIoBatch() = default;

	CORE_API bool IsValid() const;

	CORE_API FIoRequest Read(const FIoChunkId& Chunk, FIoReadOptions Options);

	CORE_API void ForEachRequest(TFunction<bool(FIoRequest&)>&& Callback);

	/**
	 * Initiates the loading of the batch as individual requests.
	 */
	CORE_API void Issue(EIoDispatcherPriority Priority);

	/**
	 * Initiates the loading of the batch to a single contiguous output buffer. The requests will be in the
	 * same order that they were added to the FIoBatch.
	 * NOTE: It is not valid to call this on a batch containing requests that have been given a TargetVa to 
	 * read into as the requests are supposed to read into the batch's output buffer, doing so will cause the
	 * method to return an error 'InvalidParameter'.
	 *
	 * @param Options A set of options allowing customization on how the load will work.
	 * @param Callback An optional callback that will be triggered once the batch has finished loading. 
	 * The batch's output buffer will be provided as the parameter of the callback.
	 *
	 * @return This methods had the capacity to fail so the return value should be checked.
	 */
	UE_NODISCARD CORE_API FIoStatus IssueWithCallback(FIoBatchReadOptions Options, EIoDispatcherPriority Priority, FIoReadCallback&& Callback);
	
	CORE_API void Wait();
	CORE_API void Cancel();

private:
	FIoDispatcherImpl*	Dispatcher		= nullptr;
	FIoBatchImpl*		Impl			= nullptr;
};

/**
 * Mapped region.
 */
struct FIoMappedRegion
{
	IMappedFileHandle* MappedFileHandle = nullptr;
	IMappedFileRegion* MappedFileRegion = nullptr;
};

struct FIoDispatcherMountedContainer
{
	FIoStoreEnvironment Environment;
	FIoContainerId ContainerId;
};

struct FIoSignatureError
{
	FString ContainerName;
	int32 BlockIndex = INDEX_NONE;
	FSHAHash ExpectedHash;
	FSHAHash ActualHash;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FIoSignatureErrorDelegate, const FIoSignatureError&);

struct FIoSignatureErrorEvent
{
	FCriticalSection CriticalSection;
	FIoSignatureErrorDelegate SignatureErrorDelegate;
};

/** I/O dispatcher
  */
class FIoDispatcher
{
public:
	DECLARE_EVENT_OneParam(FIoDispatcher, FIoContainerMountedEvent, const FIoDispatcherMountedContainer&);

	CORE_API						FIoDispatcher();
	CORE_API virtual				~FIoDispatcher();

	CORE_API FIoStatus				Mount(const FIoStoreEnvironment& Environment);

	CORE_API FIoBatch				NewBatch();
	CORE_API void					FreeBatch(FIoBatch& Batch);


	CORE_API void					ReadWithCallback(const FIoChunkId& ChunkId, const FIoReadOptions& Options, EIoDispatcherPriority Priority, FIoReadCallback&& Callback);
	CORE_API TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options);

	// Polling methods
	CORE_API bool					DoesChunkExist(const FIoChunkId& ChunkId) const;
	CORE_API TIoStatusOr<uint64>	GetSizeForChunk(const FIoChunkId& ChunkId) const;
	CORE_API TArray<FIoDispatcherMountedContainer> GetMountedContainers() const;

	// Events
	CORE_API FIoContainerMountedEvent& OnContainerMounted();
	CORE_API FIoSignatureErrorEvent& GetSignatureErrorEvent();

	FIoDispatcher(const FIoDispatcher&) = default;
	FIoDispatcher& operator=(const FIoDispatcher&) = delete;

	static CORE_API bool IsValidEnvironment(const FIoStoreEnvironment& Environment);
	static CORE_API bool IsInitialized();
	static CORE_API FIoStatus Initialize();
	static CORE_API void InitializePostSettings();
	static CORE_API void Shutdown();
	static CORE_API FIoDispatcher& Get();

private:
	FIoDispatcherImpl* Impl = nullptr;

	friend class FIoRequest;
	friend class FIoBatch;
	friend class FIoQueue;
};

//////////////////////////////////////////////////////////////////////////

struct FIoStoreWriterSettings
{
	FName CompressionMethod = NAME_None;
	uint64 CompressionBlockSize = 0;
	uint64 CompressionBlockAlignment = 0;
	uint64 MemoryMappingAlignment = 0;
	uint64 WriterMemoryLimit = 0;
	bool bEnableCsvOutput = false;
};

enum class EIoContainerFlags : uint8
{
	None,
	Compressed	= (1 << 0),
	Encrypted	= (1 << 1),
	Signed		= (1 << 2),
};
ENUM_CLASS_FLAGS(EIoContainerFlags);

struct FIoContainerSettings
{
	FIoContainerId ContainerId;
	EIoContainerFlags ContainerFlags = EIoContainerFlags::None;
	FGuid EncryptionKeyGuid;
	FAES::FAESKey EncryptionKey;
	FRSAKeyHandle SigningKey;

	bool IsCompressed() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Compressed);
	}

	bool IsEncrypted() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Encrypted);
	}

	bool IsSigned() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Signed);
	}
};

struct FIoStoreWriterResult
{
	FIoContainerId ContainerId;
	FString ContainerName;
	int64 TocSize = 0;
	int64 TocEntryCount = 0;
	int64 PaddingSize = 0;
	int64 UncompressedContainerSize = 0;
	int64 CompressedContainerSize = 0;
	FName CompressionMethod = NAME_None;
	EIoContainerFlags ContainerFlags;
};

struct FIoWriteOptions
{
	const TCHAR* DebugName = nullptr;
	bool bForceUncompressed = false;
	bool bIsMemoryMapped = false;
};

class FIoStoreWriterContext
{
public:
	CORE_API FIoStoreWriterContext();
	CORE_API ~FIoStoreWriterContext();

	UE_NODISCARD CORE_API FIoStatus Initialize(const FIoStoreWriterSettings& InWriterSettings);

private:
	friend class FIoStoreWriter;
	
	FIoStoreWriterContextImpl* Impl;
};

class FIoStoreWriter
{
public:
	CORE_API 			FIoStoreWriter(FIoStoreEnvironment& InEnvironment);
	CORE_API virtual	~FIoStoreWriter();

	FIoStoreWriter(const FIoStoreWriter&) = delete;
	FIoStoreWriter& operator=(const FIoStoreWriter&) = delete;

	UE_NODISCARD CORE_API FIoStatus	Initialize(const FIoStoreWriterContext& Context, const FIoContainerSettings& ContainerSettings);
	UE_NODISCARD CORE_API FIoStatus	Append(const FIoChunkId& ChunkId, const FIoChunkHash& ChunkHash, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions);
	UE_NODISCARD CORE_API FIoStatus	Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions);
	UE_NODISCARD CORE_API TIoStatusOr<FIoStoreWriterResult> Flush();

private:
	FIoStoreWriterImpl*		Impl;
};

struct FIoStoreTocChunkInfo
{
	FIoChunkId Id;
	FIoChunkHash Hash;
	uint64 Offset;
	uint64 Size;
	bool bForceUncompressed;
	bool bIsMemoryMapped;
};

class FIoStoreReader
{
public:
	CORE_API FIoStoreReader();
	CORE_API ~FIoStoreReader();

	UE_NODISCARD CORE_API FIoStatus Initialize(const FIoStoreEnvironment& InEnvironment, const TMap<FGuid, FAES::FAESKey>& InDecryptionKeys);
	CORE_API FIoContainerId GetContainerId() const;
	CORE_API EIoContainerFlags GetContainerFlags() const;
	CORE_API FGuid GetEncryptionKeyGuid() const;
	CORE_API void EnumerateChunks(TFunction<bool(const FIoStoreTocChunkInfo&)>&& Callback) const;
	CORE_API TIoStatusOr<FIoBuffer> Read(const FIoChunkId& Chunk, const FIoReadOptions& Options) const;

private:
	FIoStoreReaderImpl* Impl;
};

//////////////////////////////////////////////////////////////////////////

//////////////////////////////////

