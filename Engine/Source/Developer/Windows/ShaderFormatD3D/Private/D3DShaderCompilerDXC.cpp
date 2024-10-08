// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderFormatD3D.h"
#include "ShaderPreprocessor.h"
#include "ShaderCompilerCommon.h"
#include "D3D11ShaderResources.h"
#include "D3D12RHI.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryWriter.h"
#include "RayTracingDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogD3D12ShaderCompiler, Log, All);

// D3D doesn't define a mask for this, so we do so here
#define SHADER_OPTIMIZATION_LEVEL_MASK (D3D10_SHADER_OPTIMIZATION_LEVEL0 | D3D10_SHADER_OPTIMIZATION_LEVEL1 | D3D10_SHADER_OPTIMIZATION_LEVEL2 | D3D10_SHADER_OPTIMIZATION_LEVEL3)

// Disable macro redefinition warning for compatibility with Windows SDK 8+
#pragma warning(push)
#pragma warning(disable : 4005)	// macro redefinition

#include "Windows/AllowWindowsPlatformTypes.h"
	#include <D3D11.h>
	#include <D3Dcompiler.h>
	#include <d3d11Shader.h>
#include "Windows/HideWindowsPlatformTypes.h"
#undef DrawText

#pragma warning(pop)

MSVC_PRAGMA(warning(push))
MSVC_PRAGMA(warning(disable : 4191)) // warning C4191: 'type cast': unsafe conversion from 'FARPROC' to 'DxcCreateInstanceProc'
#include <dxc/dxcapi.h>
#include <dxc/Support/dxcapi.use.h>
#include <d3d12shader.h>
MSVC_PRAGMA(warning(pop))

THIRD_PARTY_INCLUDES_START
	#include <string>
	#include "ShaderConductor/ShaderConductor.hpp"
THIRD_PARTY_INCLUDES_END

#include "D3DShaderCompiler.inl"

FORCENOINLINE static void DXCFilterShaderCompileWarnings(const FString& CompileWarnings, TArray<FString>& FilteredWarnings)
{
	CompileWarnings.ParseIntoArray(FilteredWarnings, TEXT("\n"), true);
}

static bool IsGlobalConstantBufferSupported(const FShaderTarget& Target)
{
	switch (Target.Frequency)
	{
	case SF_RayGen:
	case SF_RayMiss:
	case SF_RayCallable:
		// Global CB is not currently implemented for RayGen, Miss and Callable ray tracing shaders.
		return false;
	default:
		return true;
	}
}

static uint32 GetAutoBindingSpace(const FShaderTarget& Target)
{
	switch (Target.Frequency)
	{
	case SF_RayGen:
		return RAY_TRACING_REGISTER_SPACE_GLOBAL;
	case SF_RayMiss:
	case SF_RayHitGroup:
	case SF_RayCallable:
		return RAY_TRACING_REGISTER_SPACE_LOCAL;
	default:
		return 0;
	}
}

// Utility variable so we can place a breakpoint while debugging
static int32 GBreakpointDXC = 0;

#define VERIFYHRESULT(expr) { HRESULT HR##__LINE__ = expr; if (FAILED(HR##__LINE__)) { UE_LOG(LogD3D12ShaderCompiler, Fatal, TEXT(#expr " failed: Result=%08x"), HR##__LINE__); } }

static dxc::DxcDllSupport& GetDxcDllHelper()
{
	static dxc::DxcDllSupport DxcDllSupport;
	static bool DxcDllInitialized = false;
	if (!DxcDllInitialized)
	{
		VERIFYHRESULT(DxcDllSupport.Initialize());
		DxcDllInitialized = true;
	}
	return DxcDllSupport;
}

static FString DxcBlobEncodingToFString(TRefCountPtr<IDxcBlobEncoding> DxcBlob)
{
	FString OutString;
	if (DxcBlob && DxcBlob->GetBufferSize())
	{
		ANSICHAR* Chars = new ANSICHAR[DxcBlob->GetBufferSize() + 1];
		FMemory::Memcpy(Chars, DxcBlob->GetBufferPointer(), DxcBlob->GetBufferSize());
		Chars[DxcBlob->GetBufferSize()] = 0;
		OutString = Chars;
		delete[] Chars;
	}

	return OutString;
}

#if !PLATFORM_SEH_EXCEPTIONS_DISABLED && PLATFORM_WINDOWS
#include "Windows/WindowsPlatformCrashContext.h"
#include "HAL/PlatformStackWalk.h"
static char GDxcStackTrace[65536] = "";
static int32 HandleException(LPEXCEPTION_POINTERS ExceptionInfo)
{
	constexpr int32 NumStackFramesToIgnore = 1;
	GDxcStackTrace[0] = 0;
	FPlatformStackWalk::StackWalkAndDump(GDxcStackTrace, UE_ARRAY_COUNT(GDxcStackTrace), NumStackFramesToIgnore, nullptr);
	return EXCEPTION_EXECUTE_HANDLER;
}
#else
static const char* GDxcStackTrace = "";
#endif

static HRESULT InnerDXCCompileWrapper(
	TRefCountPtr<IDxcCompiler3>& Compiler,
	TRefCountPtr<IDxcBlobEncoding>& TextBlob,
	LPCWSTR* Arguments,
	uint32 NumArguments,
	bool& bOutExceptionError,
	TRefCountPtr<IDxcResult>& OutCompileResult)
{
	bOutExceptionError = false;
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED && PLATFORM_WINDOWS
	__try
#endif
	{
		DxcBuffer SourceBuffer = { 0 };
		SourceBuffer.Ptr = TextBlob->GetBufferPointer();
		SourceBuffer.Size = TextBlob->GetBufferSize();
		BOOL bKnown = 0;
		uint32 Encoding = 0;
		if (SUCCEEDED(TextBlob->GetEncoding(&bKnown, (uint32*)&Encoding)))
		{
			if (bKnown)
			{
				SourceBuffer.Encoding = Encoding;
			}
		}
		return Compiler->Compile(
			&SourceBuffer,						// source text to compile
			Arguments,							// array of pointers to arguments
			NumArguments,						// number of arguments
			nullptr,							// user-provided interface to handle #include directives (optional)
			IID_PPV_ARGS(OutCompileResult.GetInitReference())	// compiler output status, buffer, and errors
		);
	}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED && PLATFORM_WINDOWS
	__except (HandleException(GetExceptionInformation()))
	{
		bOutExceptionError = true;
		return E_FAIL;
	}
#endif
}

static HRESULT DXCCompileWrapper(
	TRefCountPtr<IDxcCompiler3>& Compiler,
	TRefCountPtr<IDxcBlobEncoding>& TextBlob,
	FDxcArguments& Arguments,
	TRefCountPtr<IDxcResult>& OutCompileResult)
{
	bool bExceptionError = false;

	TArray<const WCHAR*> CompilerArgs;
	Arguments.GetCompilerArgs(CompilerArgs);

	HRESULT Result = InnerDXCCompileWrapper(Compiler, TextBlob,
		CompilerArgs.GetData(), CompilerArgs.Num(), bExceptionError, OutCompileResult);

	if (bExceptionError)
	{
		GSCWErrorCode = ESCWErrorCode::CrashInsidePlatformCompiler;

		FString ErrorMsg = TEXT("Internal error or exception inside dxcompiler.dll\n");
		ErrorMsg += GDxcStackTrace;

		FCString::Strcpy(GErrorExceptionDescription, *ErrorMsg);

#if !PLATFORM_SEH_EXCEPTIONS_DISABLED && PLATFORM_WINDOWS
		// Throw an exception so SCW can send it back in the output file
		FPlatformMisc::RaiseException(EXCEPTION_EXECUTE_HANDLER);
#endif
	}

	return Result;
}

static void SaveDxcBlobToFile(IDxcBlob* Blob, const FString& Filename)
{
	const uint8* DxilData = (const uint8*)Blob->GetBufferPointer();
	uint32 DxilSize = Blob->GetBufferSize();
	TArrayView<const uint8> Contents(DxilData, DxilSize);
	FFileHelper::SaveArrayToFile(Contents, *Filename);
}

static void DisassembleAndSave(TRefCountPtr<IDxcCompiler3>& Compiler, IDxcBlob* Dxil, const FString& DisasmFilename)
{
	TRefCountPtr<IDxcResult> DisasmResult;
	DxcBuffer DisasmBuffer = { 0 };
	DisasmBuffer.Size = Dxil->GetBufferSize();
	DisasmBuffer.Ptr = Dxil->GetBufferPointer();
	if (SUCCEEDED(Compiler->Disassemble(&DisasmBuffer, IID_PPV_ARGS(DisasmResult.GetInitReference()))))
	{
		HRESULT DisasmCodeResult;
		DisasmResult->GetStatus(&DisasmCodeResult);
		if (SUCCEEDED(DisasmCodeResult))
		{
			checkf(DisasmResult->HasOutput(DXC_OUT_DISASSEMBLY), TEXT("Disasm part missing but container said it has one!"));
			TRefCountPtr<IDxcBlobEncoding> DisasmBlob;
			TRefCountPtr<IDxcBlobUtf16> Dummy;
			VERIFYHRESULT(DisasmResult->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(DisasmBlob.GetInitReference()), Dummy.GetInitReference()));
			FString String = DxcBlobEncodingToFString(DisasmBlob);
			FFileHelper::SaveStringToFile(String, *DisasmFilename);
		}
	}
}

static void DumpFourCCParts(dxc::DxcDllSupport& DxcDllHelper, TRefCountPtr<IDxcBlob>& Blob)
{
#if UE_BUILD_DEBUG && IS_PROGRAM
	TRefCountPtr<IDxcContainerReflection> Refl;
	VERIFYHRESULT(DxcDllHelper.CreateInstance(CLSID_DxcContainerReflection, Refl.GetInitReference()));

	VERIFYHRESULT(Refl->Load(Blob));

	uint32 Count = 0;
	VERIFYHRESULT(Refl->GetPartCount(&Count));

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("*** Blob Size: %d, %d Parts\n"), Blob->GetBufferSize(), Count);

	for (uint32 Index = 0; Index < Count; ++Index)
	{
		char FourCC[5] = "\0\0\0\0";
		VERIFYHRESULT(Refl->GetPartKind(Index, (uint32*)FourCC));
		TRefCountPtr<IDxcBlob> Part;
		Refl->GetPartContent(Index, Part.GetInitReference());
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("* %d %s, Size %d\n"), Index, ANSI_TO_TCHAR(FourCC), (uint32)Part->GetBufferSize());
	}
#endif
}

static bool RemoveContainerReflection(dxc::DxcDllSupport& DxcDllHelper, TRefCountPtr<IDxcBlob>& Dxil)
{
	TRefCountPtr<IDxcOperationResult> Result;
	TRefCountPtr<IDxcContainerBuilder> Builder;
	TRefCountPtr<IDxcBlob> StrippedDxil;

	VERIFYHRESULT(DxcDllHelper.CreateInstance(CLSID_DxcContainerBuilder, Builder.GetInitReference()));
	VERIFYHRESULT(Builder->Load(Dxil));
	
	// Try and remove both the PDB & Reflection Data
	bool bPDBRemoved = SUCCEEDED(Builder->RemovePart(DXC_PART_PDB));
	bool bReflectionDataRemoved = SUCCEEDED(Builder->RemovePart(DXC_PART_REFLECTION_DATA));
	if (bPDBRemoved || bReflectionDataRemoved)
	{
		VERIFYHRESULT(Builder->SerializeContainer(Result.GetInitReference()));
		if (SUCCEEDED(Result->GetResult(StrippedDxil.GetInitReference())))
		{
			Dxil.SafeRelease();
			Dxil = StrippedDxil;
			return true;
		}
	}

	return false;
};

static HRESULT D3DCompileToDxil(const char* SourceText, FDxcArguments& Arguments,
	TRefCountPtr<IDxcBlob>& OutDxilBlob, TRefCountPtr<IDxcBlob>& OutReflectionBlob, TRefCountPtr<IDxcBlobEncoding>& OutErrorBlob)
{
	dxc::DxcDllSupport& DxcDllHelper = GetDxcDllHelper();

	TRefCountPtr<IDxcCompiler3> Compiler;
	VERIFYHRESULT(DxcDllHelper.CreateInstance(CLSID_DxcCompiler, Compiler.GetInitReference()));

	TRefCountPtr<IDxcLibrary> Library;
	VERIFYHRESULT(DxcDllHelper.CreateInstance(CLSID_DxcLibrary, Library.GetInitReference()));

	TRefCountPtr<IDxcBlobEncoding> TextBlob;
	VERIFYHRESULT(Library->CreateBlobWithEncodingFromPinned((LPBYTE)SourceText, FCStringAnsi::Strlen(SourceText), CP_UTF8, TextBlob.GetInitReference()));

	TRefCountPtr<IDxcResult> CompileResult;
	VERIFYHRESULT(DXCCompileWrapper(Compiler, TextBlob, Arguments, CompileResult));

	HRESULT CompileResultCode;
	CompileResult->GetStatus(&CompileResultCode);
	if (SUCCEEDED(CompileResultCode))
	{
		TRefCountPtr<IDxcBlobUtf16> Dummy;
		checkf(CompileResult->HasOutput(DXC_OUT_OBJECT), TEXT("No object code found!"));
		VERIFYHRESULT(CompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(OutDxilBlob.GetInitReference()), Dummy.GetInitReference()));

		checkf(CompileResult->HasOutput(DXC_OUT_REFLECTION), TEXT("No reflection found!"));
		VERIFYHRESULT(CompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(OutReflectionBlob.GetInitReference()), Dummy.GetInitReference()));

		if (Arguments.ShouldDump())
		{
			// Dump dissasembly before we strip reflection out
			const FString& DisasmFilename = Arguments.GetDumpDisassemblyFilename();
			check(DisasmFilename.Len() > 0);
			DisassembleAndSave(Compiler, OutDxilBlob, DisasmFilename);

			// Dump dxil (.d3dasm -> .dxil)
			FString DxilFile = Arguments.GetDumpDisassemblyFilename().LeftChop(7) + TEXT("_refl.dxil");
			SaveDxcBlobToFile(OutDxilBlob, DxilFile);

			if (CompileResult->HasOutput(DXC_OUT_PDB) && CompileResult->HasOutput(DXC_OUT_SHADER_HASH))
			{
				TRefCountPtr<IDxcBlob> PdbBlob;
				VERIFYHRESULT(CompileResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(PdbBlob.GetInitReference()), Dummy.GetInitReference()));

				TRefCountPtr<IDxcBlob> HashBlob;
				VERIFYHRESULT(CompileResult->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(HashBlob.GetInitReference()), Dummy.GetInitReference()));

				check(sizeof(DxcShaderHash) == HashBlob->GetBufferSize());
				const DxcShaderHash* ShaderHash = (DxcShaderHash*)HashBlob->GetBufferPointer();

				FString HashName;
				static_assert(sizeof(DxcShaderHash::HashDigest) == 16, "Hash changed");
				for (int32 Index = 0; Index < 16; ++Index)
				{
					HashName += FString::Printf(TEXT("%02x"), ShaderHash->HashDigest[Index]);
				}

				// Dump pdb (.d3dasm -> .pdb)
				//#todo-rco: Need to put this in a central location
				FString PdbFile = Arguments.GetDumpDebugInfoPath() / (HashName + TEXT(".lld"));
				SaveDxcBlobToFile(PdbBlob, PdbFile);
			}
		}

		DumpFourCCParts(DxcDllHelper, OutDxilBlob);
		if (RemoveContainerReflection(DxcDllHelper, OutDxilBlob))
		{
			DumpFourCCParts(DxcDllHelper, OutDxilBlob);
		}

		if (Arguments.ShouldDump())
		{
			// Dump dxil (.d3dasm -> .dxil)
			FString DxilFile = Arguments.GetDumpDisassemblyFilename().LeftChop(7) + TEXT("_norefl.dxil");
			SaveDxcBlobToFile(OutDxilBlob, DxilFile);
		}

		GBreakpointDXC++;
	}
	else
	{
		GBreakpointDXC++;
	}

	CompileResult->GetErrorBuffer(OutErrorBlob.GetInitReference());

	return CompileResultCode;
}

static FString D3DCreateDXCCompileBatchFile(const FDxcArguments& Args, const FString& ShaderPath)
{
	FString BatchFileHeader = TEXT("@ECHO OFF\nSET DXC=\"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.18362.0\\x64\\dxc.exe\"\n"\
		"IF EXIST %DXC% (\nREM\n) ELSE (\nECHO Couldn't find Windows 10.0.17763 SDK, falling back to dxc.exe in PATH...\n"\
		"SET DXC=dxc.exe)\n");

	FString DXCCommandline = FString(TEXT("%DXC%"));

	DXCCommandline += Args.GetBatchCommandLineString(ShaderPath);

	DXCCommandline += TEXT(" ");
	DXCCommandline += ShaderPath;

	return BatchFileHeader + DXCCommandline + TEXT("\npause\n");
}

inline bool IsCompatibleBinding(const D3D12_SHADER_INPUT_BIND_DESC& BindDesc, uint32 BindingSpace)
{
	return BindDesc.Space == BindingSpace;
}

// Parses ray tracing shader entry point specification string in one of the following formats:
// 1) Verbatim single entry point name, e.g. "MainRGS"
// 2) Complex entry point for ray tracing hit group shaders:
//      a) "closesthit=MainCHS"
//      b) "closesthit=MainCHS anyhit=MainAHS"
//      c) "closesthit=MainCHS anyhit=MainAHS intersection=MainIS"
//      d) "closesthit=MainCHS intersection=MainIS"
//    NOTE: closesthit attribute must always be provided for complex hit group entry points
static void ParseRayTracingEntryPoint(const FString& Input, FString& OutMain, FString& OutAnyHit, FString& OutIntersection)
{
	auto ParseEntry = [&Input](const TCHAR* Marker)
	{
		FString Result;
		int32 BeginIndex = Input.Find(Marker, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (BeginIndex != INDEX_NONE)
		{
			int32 EndIndex = Input.Find(TEXT(" "), ESearchCase::IgnoreCase, ESearchDir::FromStart, BeginIndex);
			if (EndIndex == INDEX_NONE) EndIndex = Input.Len() + 1;
			int32 MarkerLen = FCString::Strlen(Marker);
			int32 Count = EndIndex - BeginIndex;
			Result = Input.Mid(BeginIndex + MarkerLen, Count - MarkerLen);
		}
		return Result;
	};

	OutMain = ParseEntry(TEXT("closesthit="));
	OutAnyHit = ParseEntry(TEXT("anyhit="));
	OutIntersection = ParseEntry(TEXT("intersection="));

	// If complex hit group entry is not specified, assume a single verbatim entry point
	if (OutMain.IsEmpty() && OutAnyHit.IsEmpty() && OutIntersection.IsEmpty())
	{
		OutMain = Input;
	}
}

static bool IsUsingTessellation(const FShaderCompilerInput& Input)
{
	switch (Input.Target.GetFrequency())
	{
	case SF_Vertex:
	{
		const FString* UsingTessellationDefine = Input.Environment.GetDefinitions().Find(TEXT("USING_TESSELLATION"));
		return (UsingTessellationDefine != nullptr && *UsingTessellationDefine == TEXT("1"));
	}
	case SF_Hull:
	case SF_Domain:
		return true;
	default:
		return false;
	}
}

static ShaderConductor::Compiler::ShaderModel ToDXCShaderModel(ELanguage Language)
{
	switch (Language)
	{
	case ELanguage::ES3_1:
	case ELanguage::SM5:
		return { 5, 0 };
	default:
		UE_LOG(LogD3D12ShaderCompiler, Error, TEXT("Invalid input shader target for enum ELanguage (%d)."), (int32)Language);
	}
	return { 6,0 };
}

static ShaderConductor::ShaderStage ToDXCShaderStage(EShaderFrequency Frequency)
{
	check(Frequency >= SF_Vertex && Frequency <= SF_Compute);
	switch (Frequency)
	{
	case SF_Vertex:		return ShaderConductor::ShaderStage::VertexShader;
	case SF_Pixel:		return ShaderConductor::ShaderStage::PixelShader;
	case SF_Geometry:	return ShaderConductor::ShaderStage::GeometryShader;
	case SF_Hull:		return ShaderConductor::ShaderStage::HullShader;
	case SF_Domain:		return ShaderConductor::ShaderStage::DomainShader;
	case SF_Compute:	return ShaderConductor::ShaderStage::ComputeShader;
	default:			return ShaderConductor::ShaderStage::NumShaderStages;
	}
}

static void InnerDXCRewriteWrapper(const ShaderConductor::Compiler::SourceDesc& InDesc,
	const ShaderConductor::Compiler::Options& InOptions, ShaderConductor::Compiler::ResultDesc& ResultDesc)
{
	ResultDesc = ShaderConductor::Compiler::Rewrite(InDesc, InOptions);
}

static ShaderConductor::Compiler::ResultDesc DXCRewriteWrapper(const ShaderConductor::Compiler::SourceDesc& InDesc,
	const ShaderConductor::Compiler::Options& InOptions,
	bool& bOutException)
{
	bOutException = false;
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__try
#endif
	{
		ShaderConductor::Compiler::ResultDesc ResultDesc;
		InnerDXCRewriteWrapper(InDesc, InOptions, ResultDesc);
		return ResultDesc;
	}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		GSCWErrorCode = ESCWErrorCode::CrashInsidePlatformCompiler;
		ShaderConductor::Compiler::ResultDesc ResultDesc;
		FMemory::Memzero(ResultDesc);
		bOutException = true;
		return ResultDesc;
	}
#endif
}

static const TCHAR* GRewrittenBaseFilename = TEXT("Output.dxc.hlsl");
static bool RewriteUsingSC(FString& PreprocessedShaderSource, const FShaderCompilerInput& Input, bool bIsRayTracingShader,
	bool bDumpDebugInfo, ELanguage Language, FShaderCompilerOutput& Output)
{
	bool bResult = true;
	const bool bUsingTessellation = IsUsingTessellation(Input);
	if (bUsingTessellation || bIsRayTracingShader)
	{
		bResult = false;
	}
	else
	{
		// Set up compile options for ShaderConductor (shader model, optimization settings etc.)
		ShaderConductor::Compiler::Options Options;
		Options.removeUnusedGlobals = false;
		Options.packMatricesInRowMajor = false;
		Options.enableDebugInfo = false;
		Options.enable16bitTypes = false;
		Options.disableOptimizations = false;
		Options.shaderModel = ToDXCShaderModel(Language);

		// Convert input source code from TCHAR to ANSI
		std::string CStrSourceData(TCHAR_TO_ANSI(*PreprocessedShaderSource));
		std::string CStrFileName(TCHAR_TO_ANSI(*Input.VirtualSourceFilePath));
		std::string CStrEntryPointName(TCHAR_TO_ANSI(*Input.EntryPointName));

		const ShaderConductor::MacroDefine BuiltinDefines[] =
		{
//			{ "COMPILER_HLSL", "1" },
			{ "TextureExternal", "Texture2D" },
		};

		// Set up source description for ShaderConductor
		ShaderConductor::Compiler::SourceDesc SourceDesc;
		FMemory::Memzero(SourceDesc);
		SourceDesc.source = CStrSourceData.c_str();
		SourceDesc.fileName = CStrFileName.c_str();
		SourceDesc.entryPoint = CStrEntryPointName.c_str();
		SourceDesc.numDefines = sizeof(BuiltinDefines) / sizeof(BuiltinDefines[0]);
		SourceDesc.defines = BuiltinDefines;
		SourceDesc.stage = ToDXCShaderStage(Input.Target.GetFrequency());

		ShaderConductor::Compiler::TargetDesc TargetDesc;
		FMemory::Memzero(TargetDesc);
		TargetDesc.language = ShaderConductor::ShadingLanguage::Dxil;

		// Rewrite HLSL source to remove unused global variables (DXC retains them when compiling)
		ShaderConductor::Blob* RewriteBlob = nullptr;

		// Rewrite HLSL
		Options.removeUnusedGlobals = true;
		bool bException = false;
		ShaderConductor::Compiler::ResultDesc RewriteResultDesc = DXCRewriteWrapper(SourceDesc, Options, bException);
		Options.removeUnusedGlobals = false;
		if (RewriteResultDesc.hasError || bException)
		{
			if (bException)
			{
				Output.Errors.Add(TEXT("ShaderConductor exception during rewrite"));
			}
			// Append compile error to output reports
			if (ShaderConductor::Blob* ErrorBlob = RewriteResultDesc.errorWarningMsg)
			{
				FUTF8ToTCHAR UTF8Converter(reinterpret_cast<const ANSICHAR*>(ErrorBlob->Data()), ErrorBlob->Size());
				const FString ErrorString(ErrorBlob->Size(), UTF8Converter.Get());
				Output.Errors.Add(*ErrorString);

				ShaderConductor::DestroyBlob(RewriteResultDesc.errorWarningMsg);
				RewriteResultDesc.errorWarningMsg = nullptr;
				bResult = false;
			}
		}
		else
		{
			// Copy rewritten HLSL code into new source data string
			RewriteBlob = RewriteResultDesc.target;

			CStrSourceData.clear();
			CStrSourceData.resize(RewriteBlob->Size());
			FCStringAnsi::Strncpy(&CStrSourceData[0], static_cast<const char*>(RewriteBlob->Data()), RewriteBlob->Size());
			PreprocessedShaderSource = CStrSourceData.c_str();

			if (bDumpDebugInfo)
			{
				DumpDebugUSF(Input, CStrSourceData.c_str(), (int32)CStrSourceData.length(), 0, GRewrittenBaseFilename);
			}
		}

		// Release ShaderConductor resources
		if (RewriteBlob)
		{
			ShaderConductor::DestroyBlob(RewriteBlob);
		}
	}

	return bResult;
}

// Generate the dumped usf file; call the D3D compiler, gather reflection information and generate the output data
bool CompileAndProcessD3DShaderDXC(FString& PreprocessedShaderSource,
	const uint32 CompileFlags, const FShaderCompilerInput& Input, FString& EntryPointName,
	const TCHAR* ShaderProfile, ELanguage Language, bool bProcessingSecondTime,
	TArray<FString>& FilteredErrors, FShaderCompilerOutput& Output)
{
	auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShaderSource);

	const bool bIsRayTracingShader = IsRayTracingShader(Input.Target);
	const bool bUseDXC = bIsRayTracingShader
		|| Input.Environment.CompilerFlags.Contains(CFLAG_WaveOperations)
		|| Input.Environment.CompilerFlags.Contains(CFLAG_ForceDXC);

	const uint32 AutoBindingSpace = GetAutoBindingSpace(Input.Target);

	FString RayEntryPoint; // Primary entry point for all ray tracing shaders
	FString RayAnyHitEntryPoint; // Optional for hit group shaders
	FString RayIntersectionEntryPoint; // Optional for hit group shaders
	FString RayTracingExports;

	bool bEnable16BitTypes = false;

	if (bIsRayTracingShader)
	{
		ParseRayTracingEntryPoint(Input.EntryPointName, RayEntryPoint, RayAnyHitEntryPoint, RayIntersectionEntryPoint);

		RayTracingExports = RayEntryPoint;

		if (!RayAnyHitEntryPoint.IsEmpty())
		{
			RayTracingExports += TEXT(";");
			RayTracingExports += RayAnyHitEntryPoint;
		}

		if (!RayIntersectionEntryPoint.IsEmpty())
		{
			RayTracingExports += TEXT(";");
			RayTracingExports += RayIntersectionEntryPoint;
		}

		// Enable 16bit_types to reduce DXIL size (compiler bug - will be fixed)
		bEnable16BitTypes = true;
	}

	// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
	bool bDumpDebugInfo = DumpDebugShaderUSF(PreprocessedShaderSource, Input);

	FString Filename = Input.GetSourceFilename();

	if (Input.Environment.CompilerFlags.Contains(CFLAG_D3D12ForceShaderConductorRewrite))
	{
		if (RewriteUsingSC(PreprocessedShaderSource, Input, bIsRayTracingShader, bDumpDebugInfo, Language, Output))
		{
			Filename = GRewrittenBaseFilename;
		}
	}

	FString DisasmFilename;
	if (bDumpDebugInfo)
	{
		DisasmFilename = Input.DumpDebugInfoPath / Filename;
	}

	// Ignore backwards compatibility flag (/Gec) as it is deprecated.
	// #dxr_todo: this flag should not be even passed into this function from the higher level.
	uint32 DXCFlags = CompileFlags & (~D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY);
	if (Input.Environment.CompilerFlags.Contains(CFLAG_SkipOptimizationsDXC))
	{
		DXCFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
	}

	const bool bKeepDebugInfo = Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo);

	FDxcArguments Args(EntryPointName, ShaderProfile, RayTracingExports,
		Input.DumpDebugInfoPath, Filename, bEnable16BitTypes, bKeepDebugInfo, DXCFlags, AutoBindingSpace);

	if (bDumpDebugInfo)
	{
		FString BatchFileContents = D3DCreateDXCCompileBatchFile(Args, Filename);
		FFileHelper::SaveStringToFile(BatchFileContents, *(Input.DumpDebugInfoPath / TEXT("CompileDXC.bat")));

		if (Input.bGenerateDirectCompileFile)
		{
			FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
			FFileHelper::SaveStringToFile(Input.DebugDescription, *(Input.DumpDebugInfoPath / TEXT("permutation_info.txt")));
		}
	}

	TRefCountPtr<IDxcBlob> ShaderBlob;
	TRefCountPtr<IDxcBlob> ReflectionBlob;
	TRefCountPtr<IDxcBlobEncoding> DxcErrorBlob;
	HRESULT Result = D3DCompileToDxil(AnsiSourceFile.Get(), Args, ShaderBlob, ReflectionBlob, DxcErrorBlob);

	if (DxcErrorBlob && DxcErrorBlob->GetBufferSize())
	{
		FString ErrorString = DxcBlobEncodingToFString(DxcErrorBlob);
		DXCFilterShaderCompileWarnings(ErrorString, FilteredErrors);
	}

	if (SUCCEEDED(Result))
	{
		// Gather reflection information
		int32 NumInterpolants = 0;
		TIndirectArray<FString> InterpolantNames;
		TArray<FString> ShaderInputs;
		TArray<FShaderCodeVendorExtension> VendorExtensions;

		bool bGlobalUniformBufferUsed = false;
		uint32 NumInstructions = 0;
		uint32 NumSamplers = 0;
		uint32 NumSRVs = 0;
		uint32 NumCBs = 0;
		uint32 NumUAVs = 0;
		TArray<FString> UniformBufferNames;
		TArray<FString> ShaderOutputs;

		TBitArray<> UsedUniformBufferSlots;
		UsedUniformBufferSlots.Init(false, 32);

		dxc::DxcDllSupport& DxcDllHelper = GetDxcDllHelper();
		TRefCountPtr<IDxcUtils> Utils;
		VERIFYHRESULT(DxcDllHelper.CreateInstance(CLSID_DxcUtils, Utils.GetInitReference()));
		DxcBuffer ReflBuffer = { 0 };
		ReflBuffer.Ptr = ReflectionBlob->GetBufferPointer();
		ReflBuffer.Size = ReflectionBlob->GetBufferSize();

		if (bIsRayTracingShader)
		{
			TRefCountPtr<ID3D12LibraryReflection> LibraryReflection;
			Result = Utils->CreateReflection(&ReflBuffer, IID_PPV_ARGS(LibraryReflection.GetInitReference()));

			if (FAILED(Result))
			{
				UE_LOG(LogD3D12ShaderCompiler, Fatal, TEXT("D3DReflectDxil failed: Result=%08x"), Result);
			}

			D3D12_LIBRARY_DESC LibraryDesc = {};
			LibraryReflection->GetDesc(&LibraryDesc);

			ID3D12FunctionReflection* FunctionReflection = nullptr;
			D3D12_FUNCTION_DESC FunctionDesc = {};

			// MangledEntryPoints contains partial mangled entry point signatures in a the following form:
			// ?QualifiedName@ (as described here: https://en.wikipedia.org/wiki/Name_mangling)
			// Entry point parameters are currently not included in the partial mangling.
			TArray<FString, TInlineAllocator<3>> MangledEntryPoints;

			if (!RayEntryPoint.IsEmpty())
			{
				MangledEntryPoints.Add(FString::Printf(TEXT("?%s@"), *RayEntryPoint));
			}
			if (!RayAnyHitEntryPoint.IsEmpty())
			{
				MangledEntryPoints.Add(FString::Printf(TEXT("?%s@"), *RayAnyHitEntryPoint));
			}
			if (!RayIntersectionEntryPoint.IsEmpty())
			{
				MangledEntryPoints.Add(FString::Printf(TEXT("?%s@"), *RayIntersectionEntryPoint));
			}

			uint32 NumFoundEntryPoints = 0;

			for (uint32 FunctionIndex = 0; FunctionIndex < LibraryDesc.FunctionCount; ++FunctionIndex)
			{
				FunctionReflection = LibraryReflection->GetFunctionByIndex(FunctionIndex);
				FunctionReflection->GetDesc(&FunctionDesc);

				for (const FString& MangledEntryPoint : MangledEntryPoints)
				{
					// Entry point parameters are currently not included in the partial mangling, therefore partial substring match is used here.
					if (FCStringAnsi::Strstr(FunctionDesc.Name, TCHAR_TO_ANSI(*MangledEntryPoint)))
					{
						// Note: calling ExtractParameterMapFromD3DShader multiple times merges the reflection data for multiple functions
						ExtractParameterMapFromD3DShader<ID3D12FunctionReflection, D3D12_FUNCTION_DESC, D3D12_SHADER_INPUT_BIND_DESC,
							ID3D12ShaderReflectionConstantBuffer, D3D12_SHADER_BUFFER_DESC,
							ID3D12ShaderReflectionVariable, D3D12_SHADER_VARIABLE_DESC>(
								Input.Target.Platform, AutoBindingSpace, Input.VirtualSourceFilePath, FunctionReflection, FunctionDesc, bGlobalUniformBufferUsed, NumSamplers, NumSRVs, NumCBs, NumUAVs,
								Output, UniformBufferNames, UsedUniformBufferSlots, VendorExtensions);

						NumFoundEntryPoints++;
					}
				}
			}

			if (NumFoundEntryPoints == MangledEntryPoints.Num())
			{
				Output.bSucceeded = true;

				bool bGlobalUniformBufferAllowed = false;

				if (bGlobalUniformBufferUsed && !IsGlobalConstantBufferSupported(Input.Target))
				{
					const TCHAR* ShaderFrequencyString = GetShaderFrequencyString(Input.Target.GetFrequency(), false);
					FString ErrorString = FString::Printf(TEXT("Global uniform buffer cannot be used in a %s shader."), ShaderFrequencyString);

					uint32 NumLooseParameters = 0;
					for (const auto& It : Output.ParameterMap.ParameterMap)
					{
						if (It.Value.Type == EShaderParameterType::LooseData)
						{
							NumLooseParameters++;
						}
					}

					if (NumLooseParameters)
					{
						ErrorString += TEXT(" Global parameters: ");
						uint32 ParameterIndex = 0;
						for (const auto& It : Output.ParameterMap.ParameterMap)
						{
							if (It.Value.Type == EShaderParameterType::LooseData)
							{
								--NumLooseParameters;
								ErrorString += FString::Printf(TEXT("%s%s"), *It.Key, NumLooseParameters ? TEXT(", ") : TEXT("."));
							}
						}
					}

					FilteredErrors.Add(ErrorString);
					Result = E_FAIL;
					Output.bSucceeded = false;
				}
			}
			else
			{
				UE_LOG(LogD3D12ShaderCompiler, Fatal, TEXT("Failed to find required points in the shader library."));
				Output.bSucceeded = false;
			}
		}
		else
		{

			TRefCountPtr<ID3D12ShaderReflection> ShaderReflection;
			Result = Utils->CreateReflection(&ReflBuffer, IID_PPV_ARGS(ShaderReflection.GetInitReference()));
			if (FAILED(Result))
			{
				UE_LOG(LogD3D12ShaderCompiler, Fatal, TEXT("D3DReflectDxil failed: Result=%08x"), Result);
			}

			D3D12_SHADER_DESC ShaderDesc = {};
			ShaderReflection->GetDesc(&ShaderDesc);

			ExtractParameterMapFromD3DShader<ID3D12ShaderReflection, D3D12_SHADER_DESC, D3D12_SHADER_INPUT_BIND_DESC,
				ID3D12ShaderReflectionConstantBuffer, D3D12_SHADER_BUFFER_DESC,
				ID3D12ShaderReflectionVariable, D3D12_SHADER_VARIABLE_DESC>(
					Input.Target.Platform, AutoBindingSpace, Input.VirtualSourceFilePath, ShaderReflection, ShaderDesc, bGlobalUniformBufferUsed, NumSamplers, NumSRVs, NumCBs, NumUAVs,
					Output, UniformBufferNames, UsedUniformBufferSlots, VendorExtensions);


			Output.bSucceeded = true;
		}

		if (!ValidateResourceCounts(NumSRVs, NumSamplers, NumUAVs, NumCBs, FilteredErrors))
		{
			Result = E_FAIL;
			Output.bSucceeded = false;
		}

		// Save results if compilation and reflection succeeded
		if (Output.bSucceeded)
		{
			auto PostSRTWriterCallback = [&](FMemoryWriter& Ar)
			{
				if (bIsRayTracingShader)
				{
					Ar << RayEntryPoint;
					Ar << RayAnyHitEntryPoint;
					Ar << RayIntersectionEntryPoint;
				}
			};

			auto AddOptionalDataCallback = [&](FShaderCode& ShaderCode)
			{
				FShaderCodeFeatures CodeFeatures;
				//#todo-rco: Really should look inside DXIL
				CodeFeatures.bUsesWaveOps = Input.Environment.CompilerFlags.Contains(CFLAG_WaveOperations);

				// We only need this to appear when using a DXC shader
				ShaderCode.AddOptionalData<FShaderCodeFeatures>(CodeFeatures);
			};

			//#todo-rco: Should compress ShaderCode?

			FShaderCodePackedResourceCounts PackedResourceCounts = { bGlobalUniformBufferUsed, static_cast<uint8>(NumSamplers), static_cast<uint8>(NumSRVs), static_cast<uint8>(NumCBs), static_cast<uint8>(NumUAVs), 0 };
			GenerateFinalOutput(ShaderBlob,
				Input, VendorExtensions,
				UsedUniformBufferSlots, UniformBufferNames,
				bProcessingSecondTime, ShaderInputs,
				PackedResourceCounts, NumInstructions,
				Output,
				PostSRTWriterCallback,
				AddOptionalDataCallback);
		}
	}

	if (FAILED(Result))
	{
		FilteredErrors.Add(TEXT("D3DCompileToDxil failed"));
	}

	return SUCCEEDED(Result);
}

#undef VERIFYHRESULT
