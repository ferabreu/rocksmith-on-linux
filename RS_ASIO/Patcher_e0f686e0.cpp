#include "stdafx.h"
#include "dllmain.h"
#include "Patcher.h"
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <setupapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Advapi32.lib")
#endif

// Patch code for Rocksmith (2011), CRC32 0xe0f686e0
//
// The Rocksmith 2011 executable encrypts its .text section on disk using a custom
// packer stub (PSFD00 section), so call-site byte patterns cannot be found statically.
// Instead, we patch the Import Address Table (IAT) directly.
//
// The IAT is in .rdata (unencrypted) and is filled by the loader before our DLL runs.
// We overwrite the function pointer slots for the COM functions that PortAudio/WASAPI
// uses to enumerate audio devices, replacing them with our own implementations.
//
// IAT entry RVAs (relative to image base 0x00400000, verified from the PE headers):
//
//   RegQueryValueExW                     RVA 0x0088d000  (advapi32.dll)
//   RegCloseKey                          RVA 0x0088d004  (advapi32.dll)
//
//   SetupDiGetClassDevsW                 RVA 0x0088d2bc  (setupapi.dll)
//   SetupDiOpenDeviceInterfaceRegKey     RVA 0x0088d2c0  (setupapi.dll)
//   SetupDiGetDeviceRegistryPropertyW    RVA 0x0088d2c4  (setupapi.dll)
//   SetupDiGetDeviceInterfaceDetailW     RVA 0x0088d2c8  (setupapi.dll)
//   SetupDiDestroyDeviceInfoList         RVA 0x0088d2cc  (setupapi.dll)
//   SetupDiGetDeviceInterfaceAlias       RVA 0x0088d2d0  (setupapi.dll)
//   SetupDiGetClassDevsA                 RVA 0x0088d2d4  (setupapi.dll)
//   SetupDiEnumDeviceInterfaces          RVA 0x0088d2d8  (setupapi.dll)
//
//   CoCreateInstance                      RVA 0x0088d47c  (ole32.dll)
//   CoMarshalInterThreadInterfaceInStream  RVA 0x0088d490  (ole32.dll)
//   CoGetInterfaceAndReleaseStream         RVA 0x0088d494  (ole32.dll)

// Known IAT RVAs - these are fixed offsets from the module base and do not change
// because the executable does not use ASLR (ImageBase is fixed at 0x00400000).
static const DWORD IAT_RVA_CoCreateInstance                     = 0x0088d47c;
static const DWORD IAT_RVA_CoMarshalInterThreadInterfaceInStream = 0x0088d490;
static const DWORD IAT_RVA_CoGetInterfaceAndReleaseStream        = 0x0088d494;
static const DWORD IAT_RVA_RegQueryValueExW                      = 0x0088d000;
static const DWORD IAT_RVA_RegCloseKey                           = 0x0088d004;
static const DWORD IAT_RVA_SetupDiGetClassDevsW                  = 0x0088d2bc;
static const DWORD IAT_RVA_SetupDiOpenDeviceInterfaceRegKey      = 0x0088d2c0;
static const DWORD IAT_RVA_SetupDiGetDeviceRegistryPropertyW     = 0x0088d2c4;
static const DWORD IAT_RVA_SetupDiGetDeviceInterfaceDetailW      = 0x0088d2c8;
static const DWORD IAT_RVA_SetupDiDestroyDeviceInfoList          = 0x0088d2cc;
static const DWORD IAT_RVA_SetupDiGetDeviceInterfaceAlias        = 0x0088d2d0;
static const DWORD IAT_RVA_SetupDiGetClassDevsA                  = 0x0088d2d4;
static const DWORD IAT_RVA_SetupDiEnumDeviceInterfaces           = 0x0088d2d8;

static std::mutex g_setupDiRegKeysMutex;
static std::set<HKEY> g_setupDiRegKeys;
static std::set<HKEY> g_fakeSetupDiRegKeys;

static const ULONG_PTR kFakeSetupDiInterfaceTag = 0x52534149; // "RSAI"
static const wchar_t* kFakeSetupDiDevicePath = L"\\\\?\\USB#VID_12BA&PID_00FF#RS_ASIO#{6994ad04-93ef-11d0-a3cc-00a0c9223196}";
static const wchar_t* kFakeSetupDiRegValue = L"USB\\VID_12BA&PID_00FF\\RS_ASIO";
static const wchar_t* kFakeSetupDiFriendlyName = L"Rocksmith Guitar Adapter Mono";
static const wchar_t* kFakeSetupDiDeviceDesc = L"Rocksmith USB Guitar Adapter";
static const bool kEnableSetupDiSynthesis = true;
static const bool kRequireRocksmithCaptureEndpointForSynthesis = true;

struct RealCaptureFallbackToken
{
	SP_DEVICE_INTERFACE_DATA captureInterfaceData{};
};

static std::mutex g_setupDiCaptureFallbackMutex;
static HDEVINFO g_captureInterfaceInfoSet = INVALID_HANDLE_VALUE;
static std::set<RealCaptureFallbackToken*> g_captureFallbackTokens;
static std::mutex g_synthesisProbeMutex;
static std::optional<bool> g_synthesisProbeCachedResult;

static HKEY GetInvalidHKeyValue()
{
	return reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE);
}

static bool IsAudioInterfaceGuid(const GUID* guid)
{
	return guid && IsEqualGUID(*guid, KSCATEGORY_AUDIO);
}

static bool IsFakeSetupDiInterfaceData(const SP_DEVICE_INTERFACE_DATA* interfaceData)
{
	return interfaceData && interfaceData->Reserved == kFakeSetupDiInterfaceTag;
}

static std::wstring ToLowerCopy(const wchar_t* text)
{
	if (!text)
		return std::wstring();

	std::wstring out(text);
	std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch)
	{
		return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
	});

	return out;
}

static bool HasRocksmithCaptureEndpoint()
{
	std::lock_guard<std::mutex> g(g_synthesisProbeMutex);
	if (g_synthesisProbeCachedResult.has_value())
	{
		return *g_synthesisProbeCachedResult;
	}

	IMMDeviceEnumerator* enumerator = nullptr;
	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&enumerator));

	if (FAILED(hr) || !enumerator)
	{
		rslog::info_ts() << "SetupDi synthesis probe: failed to create MMDeviceEnumerator hr=" << HResultToStr(hr) << std::endl;
		g_synthesisProbeCachedResult = false;
		return false;
	}

	IMMDeviceCollection* devices = nullptr;
	hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices);
	enumerator->Release();
	enumerator = nullptr;

	if (FAILED(hr) || !devices)
	{
		rslog::info_ts() << "SetupDi synthesis probe: EnumAudioEndpoints failed hr=" << HResultToStr(hr) << std::endl;
		g_synthesisProbeCachedResult = false;
		return false;
	}

	UINT count = 0;
	devices->GetCount(&count);

	bool found = false;
	for (UINT i = 0; i < count && !found; ++i)
	{
		IMMDevice* dev = nullptr;
		if (FAILED(devices->Item(i, &dev)) || !dev)
			continue;

		std::wstring lowerId;
		LPWSTR id = nullptr;
		if (SUCCEEDED(dev->GetId(&id)) && id)
		{
			lowerId = ToLowerCopy(id);
			CoTaskMemFree(id);
			id = nullptr;
		}

		std::wstring lowerFriendlyName;
		IPropertyStore* store = nullptr;
		if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store)
		{
			PROPVARIANT pv;
			PropVariantInit(&pv);
			if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
			{
				lowerFriendlyName = ToLowerCopy(pv.pwszVal);
			}
			PropVariantClear(&pv);
			store->Release();
		}

		dev->Release();

		if (lowerFriendlyName.find(L"rocksmith") != std::wstring::npos ||
			lowerFriendlyName.find(L"guitar adapter") != std::wstring::npos ||
			lowerId.find(L"rocksmith") != std::wstring::npos ||
			lowerId.find(L"vid_12ba") != std::wstring::npos)
		{
			found = true;
		}
	}

	devices->Release();

	g_synthesisProbeCachedResult = found;
	rslog::info_ts() << "SetupDi synthesis probe: rocksmith endpoint present=" << (found ? 1 : 0) << std::endl;
	return found;
}

static bool ShouldAllowSetupDiSynthesis()
{
	if (!kEnableSetupDiSynthesis)
		return false;

	if (!kRequireRocksmithCaptureEndpointForSynthesis)
		return true;

	return HasRocksmithCaptureEndpoint();
}

static void FillFakeSetupDiInterfaceData(PSP_DEVICE_INTERFACE_DATA interfaceData, const GUID* interfaceClassGuid)
{
	if (!interfaceData)
		return;

	interfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	interfaceData->InterfaceClassGuid = interfaceClassGuid ? *interfaceClassGuid : KSCATEGORY_AUDIO;
	interfaceData->Flags = SPINT_ACTIVE;
	interfaceData->Reserved = kFakeSetupDiInterfaceTag;
}

static BOOL CopyRegSzToBuffer(const wchar_t* value, PDWORD outType, PBYTE outBuffer, DWORD outBufferSize, PDWORD outRequiredSize)
{
	if (!value)
		value = L"";

	const DWORD bytesRequired = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));

	if (outType)
		*outType = REG_SZ;
	if (outRequiredSize)
		*outRequiredSize = bytesRequired;

	if (!outBuffer || outBufferSize < bytesRequired)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	memcpy(outBuffer, value, bytesRequired);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

static BOOL CopyRegMultiSzToBuffer(const wchar_t* value, PDWORD outType, PBYTE outBuffer, DWORD outBufferSize, PDWORD outRequiredSize)
{
	if (!value)
		value = L"";

	const size_t valueChars = wcslen(value);
	const DWORD bytesRequired = static_cast<DWORD>((valueChars + 2) * sizeof(wchar_t));

	if (outType)
		*outType = REG_MULTI_SZ;
	if (outRequiredSize)
		*outRequiredSize = bytesRequired;

	if (!outBuffer || outBufferSize < bytesRequired)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	wchar_t* out = reinterpret_cast<wchar_t*>(outBuffer);
	memcpy(out, value, valueChars * sizeof(wchar_t));
	out[valueChars] = L'\0';
	out[valueChars + 1] = L'\0';

	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

static void TrackSetupDiRegKey(HKEY key, bool track, bool isFake)
{
	if (!key || key == GetInvalidHKeyValue())
		return;

	std::lock_guard<std::mutex> g(g_setupDiRegKeysMutex);
	if (track)
	{
		g_setupDiRegKeys.insert(key);
		if (isFake)
			g_fakeSetupDiRegKeys.insert(key);
	}
	else
	{
		g_setupDiRegKeys.erase(key);
		g_fakeSetupDiRegKeys.erase(key);
	}
}

static bool IsTrackedSetupDiRegKey(HKEY key)
{
	std::lock_guard<std::mutex> g(g_setupDiRegKeysMutex);
	return g_setupDiRegKeys.find(key) != g_setupDiRegKeys.end();
}

static bool IsFakeSetupDiRegKey(HKEY key)
{
	std::lock_guard<std::mutex> g(g_setupDiRegKeysMutex);
	return g_fakeSetupDiRegKeys.find(key) != g_fakeSetupDiRegKeys.end();
}

static HDEVINFO EnsureCaptureInterfaceInfoSet()
{
	std::lock_guard<std::mutex> g(g_setupDiCaptureFallbackMutex);
	if (g_captureInterfaceInfoSet == INVALID_HANDLE_VALUE)
	{
		g_captureInterfaceInfoSet = SetupDiGetClassDevsW(&KSCATEGORY_CAPTURE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	}
	return g_captureInterfaceInfoSet;
}

static RealCaptureFallbackToken* CreateCaptureFallbackToken(const SP_DEVICE_INTERFACE_DATA& captureInterfaceData)
{
	auto* token = new RealCaptureFallbackToken();
	token->captureInterfaceData = captureInterfaceData;

	std::lock_guard<std::mutex> g(g_setupDiCaptureFallbackMutex);
	g_captureFallbackTokens.insert(token);
	return token;
}

static RealCaptureFallbackToken* GetCaptureFallbackToken(const SP_DEVICE_INTERFACE_DATA* interfaceData)
{
	if (!interfaceData || !interfaceData->Reserved)
		return nullptr;

	auto* token = reinterpret_cast<RealCaptureFallbackToken*>(interfaceData->Reserved);
	std::lock_guard<std::mutex> g(g_setupDiCaptureFallbackMutex);
	auto it = g_captureFallbackTokens.find(token);
	if (it == g_captureFallbackTokens.end())
		return nullptr;

	return *it;
}

static void ClearCaptureFallbackState()
{
	std::lock_guard<std::mutex> g(g_setupDiCaptureFallbackMutex);
	for (auto* token : g_captureFallbackTokens)
		delete token;
	g_captureFallbackTokens.clear();

	if (g_captureInterfaceInfoSet != INVALID_HANDLE_VALUE)
	{
		SetupDiDestroyDeviceInfoList(g_captureInterfaceInfoSet);
		g_captureInterfaceInfoSet = INVALID_HANDLE_VALUE;
	}
}

// Patch a single IAT slot to point to replacementFn.
// Patch_ReplaceWithBytes (from Patcher.h) handles page protection internally
// via NtProtectVirtualMemory, bypassing any hook on VirtualProtect.
static bool PatchIATEntry(DWORD rva, void* replacementFn, const char* fnName)
{
	const HMODULE hBase = GetModuleHandle(NULL);
	if (!hBase)
	{
		rslog::error_ts() << "PatchIATEntry: GetModuleHandle failed for " << fnName << std::endl;
		return false;
	}

	void** iatSlot = reinterpret_cast<void**>(reinterpret_cast<BYTE*>(hBase) + rva);

	rslog::info_ts() << "PatchIATEntry: patching " << fnName
	                 << " at " << iatSlot
	                 << "  old=" << *iatSlot
	                 << "  new=" << replacementFn << std::endl;

	Patch_ReplaceWithBytes(iatSlot, sizeof(void*), reinterpret_cast<const BYTE*>(&replacementFn));
	return true;
}

// Thin wrappers that match the calling convention of the ole32 functions we are replacing.
// CoMarshalInterThreadInterfaceInStream / CoGetInterfaceAndReleaseStream are patched to
// no-op / identity replacements so that PortAudio's cross-thread COM marshaling does not
// interfere with our fake WASAPI device objects.
//
// NOTE: These are only used if RS2011's embedded PortAudio performs cross-apartment
// marshaling the same way RS2014's does. The game may work without them; if it crashes
// after CoCreateInstance is redirected successfully, these patches are the next step.

static HRESULT STDAPICALLTYPE Patched_CoMarshalInterThreadInterfaceInStream(
    REFIID riid, IUnknown* pUnk, IStream** ppStm)
{
	rslog::info_ts() << "Patched_CoMarshalInterThreadInterfaceInStream called - riid: " << riid << std::endl;
	// Return the interface directly as the "stream" — PortAudio will read it back
	// via Patched_CoGetInterfaceAndReleaseStream below.
	if (!ppStm)
		return E_POINTER;
	*ppStm = reinterpret_cast<IStream*>(pUnk);
	if (pUnk)
		pUnk->AddRef();
	return S_OK;
}

static HRESULT STDAPICALLTYPE Patched_CoGetInterfaceAndReleaseStream(
    IStream* pStm, REFIID riid, void** ppv)
{
	rslog::info_ts() << "Patched_CoGetInterfaceAndReleaseStream called - riid: " << riid << std::endl;
	if (!ppv)
		return E_POINTER;
	// The "stream" is actually the interface pointer stored by Patched_CoMarshalInterThreadInterfaceInStream.
	// The real CoGetInterfaceAndReleaseStream deserializes and returns the pointer that was originally
	// marshaled — it does NOT call QueryInterface on the result (the stream data already encodes the
	// correct interface type). We replicate that: just hand back the stored pointer directly.
	// The AddRef performed during marshal transfers to the caller; no additional AddRef or Release needed.
	IUnknown* pUnk = reinterpret_cast<IUnknown*>(pStm);
	if (!pUnk)
	{
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	*ppv = pUnk;
	return S_OK;
}

static HDEVINFO WINAPI Patched_SetupDiGetClassDevsW(const GUID* ClassGuid, PCWSTR Enumerator, HWND hwndParent, DWORD Flags)
{
	std::ostringstream msg;
	msg << "Patched_SetupDiGetClassDevsW called - flags: 0x" << std::hex << Flags << std::dec;
	if (ClassGuid)
		msg << "  classGuid: " << *ClassGuid;
	if (Enumerator)
		msg << "  enumerator: " << Enumerator;
	rslog::info_ts() << msg.str() << std::endl;

	HDEVINFO result = SetupDiGetClassDevsW(ClassGuid, Enumerator, hwndParent, Flags);
	const DWORD gle = GetLastError();
	rslog::info_ts() << "  -> " << result << "  gle=" << std::dec << gle << std::endl;
	return result;
}

static HDEVINFO WINAPI Patched_SetupDiGetClassDevsA(const GUID* ClassGuid, PCSTR Enumerator, HWND hwndParent, DWORD Flags)
{
	std::ostringstream msg;
	msg << "Patched_SetupDiGetClassDevsA called - flags: 0x" << std::hex << Flags << std::dec;
	if (ClassGuid)
		msg << "  classGuid: " << *ClassGuid;
	if (Enumerator)
		msg << "  enumerator: " << Enumerator;
	rslog::info_ts() << msg.str() << std::endl;

	if (IsAudioInterfaceGuid(ClassGuid))
	{
		ClearCaptureFallbackState();
	}

	// Wine/Proton may diverge between ANSI and Unicode SetupAPI paths.
	// Rocksmith calls the ANSI variant, so we normalize to Unicode here while keeping
	// the real SetupAPI data source and real hardware checks intact.
	std::wstring enumWide;
	PCWSTR enumWidePtr = nullptr;
	if (Enumerator)
	{
		const int wlen = MultiByteToWideChar(CP_ACP, 0, Enumerator, -1, nullptr, 0);
		if (wlen > 0)
		{
			enumWide.resize(static_cast<size_t>(wlen));
			MultiByteToWideChar(CP_ACP, 0, Enumerator, -1, &enumWide[0], wlen);
			enumWidePtr = enumWide.c_str();
		}
	}

	HDEVINFO result = SetupDiGetClassDevsW(ClassGuid, enumWidePtr, hwndParent, Flags);
	const DWORD gle = GetLastError();
	rslog::info_ts() << "  -> " << result << "  gle=" << std::dec << gle << "  via=SetupDiGetClassDevsW" << std::endl;
	return result;
}

static BOOL WINAPI Patched_SetupDiEnumDeviceInterfaces(
	HDEVINFO DeviceInfoSet,
	PSP_DEVINFO_DATA DeviceInfoData,
	const GUID* InterfaceClassGuid,
	DWORD MemberIndex,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData)
{
	std::ostringstream msg;
	msg << "Patched_SetupDiEnumDeviceInterfaces called - MemberIndex: " << std::dec << MemberIndex;
	if (InterfaceClassGuid)
		msg << "  interfaceClassGuid: " << *InterfaceClassGuid;
	rslog::info_ts() << msg.str() << std::endl;

	BOOL ok = SetupDiEnumDeviceInterfaces(DeviceInfoSet, DeviceInfoData, InterfaceClassGuid, MemberIndex, DeviceInterfaceData);
	const DWORD gle = GetLastError();

	const bool noAudioInterfaceAtStart = (!ok && gle == ERROR_NO_MORE_ITEMS && MemberIndex == 0 && IsAudioInterfaceGuid(InterfaceClassGuid));
	const bool allowSynthesis = noAudioInterfaceAtStart ? ShouldAllowSetupDiSynthesis() : false;

	if (allowSynthesis && noAudioInterfaceAtStart)
	{
		if (DeviceInterfaceData && DeviceInterfaceData->cbSize == sizeof(SP_DEVICE_INTERFACE_DATA))
		{
			FillFakeSetupDiInterfaceData(DeviceInterfaceData, InterfaceClassGuid);
			SetLastError(ERROR_SUCCESS);
			rslog::info_ts() << "  -> synthesized fake KSCATEGORY_AUDIO interface" << std::endl;
			return TRUE;
		}
	}

	if (!ok && gle == ERROR_NO_MORE_ITEMS && IsAudioInterfaceGuid(InterfaceClassGuid))
	{
		if (MemberIndex == 0 && !allowSynthesis)
		{
			rslog::info_ts() << "SetupDi synthesis disabled for this run (no Rocksmith capture endpoint probe match)." << std::endl;
		}

		HDEVINFO captureSet = EnsureCaptureInterfaceInfoSet();
		if (captureSet != INVALID_HANDLE_VALUE)
		{
			SP_DEVICE_INTERFACE_DATA captureInterfaceData{};
			captureInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
			if (SetupDiEnumDeviceInterfaces(captureSet, nullptr, &KSCATEGORY_CAPTURE, MemberIndex, &captureInterfaceData))
			{
				if (DeviceInterfaceData && DeviceInterfaceData->cbSize == sizeof(SP_DEVICE_INTERFACE_DATA))
				{
					auto* token = CreateCaptureFallbackToken(captureInterfaceData);
					DeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
					DeviceInterfaceData->InterfaceClassGuid = InterfaceClassGuid ? *InterfaceClassGuid : KSCATEGORY_AUDIO;
					DeviceInterfaceData->Flags = captureInterfaceData.Flags;
					DeviceInterfaceData->Reserved = reinterpret_cast<ULONG_PTR>(token);
					SetLastError(ERROR_SUCCESS);
					rslog::info_ts() << "  -> fallback to real KSCATEGORY_CAPTURE interface" << std::endl;
					return TRUE;
				}
			}

			const DWORD captureErr = GetLastError();
			if (MemberIndex == 0)
			{
				rslog::error_ts() << "SetupDiEnumDeviceInterfaces: no KSCATEGORY_AUDIO interfaces returned."
				                 << " capture-fallback failed with gle=" << captureErr
				                 << ". Rocksmith cable validation will fail under this runtime." << std::endl;
			}
		}
		else if (MemberIndex == 0)
		{
			const DWORD captureSetErr = GetLastError();
			rslog::error_ts() << "SetupDiEnumDeviceInterfaces: no KSCATEGORY_AUDIO interfaces returned."
			                 << " capture-fallback could not acquire capture set, gle=" << captureSetErr
			                 << ". Rocksmith cable validation will fail under this runtime." << std::endl;
		}
	}

	rslog::info_ts() << "  -> " << std::dec << ok << "  gle=" << gle << std::endl;
	return ok;
}

static BOOL WINAPI Patched_SetupDiGetDeviceInterfaceAlias(
	HDEVINFO DeviceInfoSet,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
	const GUID* AliasInterfaceClassGuid,
	PSP_DEVICE_INTERFACE_DATA AliasDeviceInterfaceData)
{
	std::ostringstream msg;
	msg << "Patched_SetupDiGetDeviceInterfaceAlias called";
	if (AliasInterfaceClassGuid)
		msg << "  aliasClassGuid: " << *AliasInterfaceClassGuid;
	rslog::info_ts() << msg.str() << std::endl;

	if (auto* captureToken = GetCaptureFallbackToken(DeviceInterfaceData))
	{
		if (AliasInterfaceClassGuid && AliasDeviceInterfaceData && AliasDeviceInterfaceData->cbSize == sizeof(SP_DEVICE_INTERFACE_DATA))
		{
			AliasDeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
			AliasDeviceInterfaceData->InterfaceClassGuid = *AliasInterfaceClassGuid;
			AliasDeviceInterfaceData->Flags = captureToken->captureInterfaceData.Flags;
			AliasDeviceInterfaceData->Reserved = reinterpret_cast<ULONG_PTR>(captureToken);
			SetLastError(ERROR_SUCCESS);
			rslog::info_ts() << "  -> 1  gle=0  via=real-capture-self-alias" << std::endl;
			return TRUE;
		}

		SetLastError(ERROR_INVALID_USER_BUFFER);
		rslog::info_ts() << "  -> 0  gle=" << std::dec << ERROR_INVALID_USER_BUFFER << "  via=real-capture-self-alias" << std::endl;
		return FALSE;
	}

	if (kEnableSetupDiSynthesis && IsFakeSetupDiInterfaceData(DeviceInterfaceData))
	{
		if (AliasDeviceInterfaceData && AliasDeviceInterfaceData->cbSize == sizeof(SP_DEVICE_INTERFACE_DATA))
		{
			FillFakeSetupDiInterfaceData(AliasDeviceInterfaceData, AliasInterfaceClassGuid);
			SetLastError(ERROR_SUCCESS);
			rslog::info_ts() << "  -> synthesized alias for fake interface" << std::endl;
			return TRUE;
		}

		SetLastError(ERROR_INVALID_USER_BUFFER);
		rslog::info_ts() << "  -> 0  gle=" << std::dec << ERROR_INVALID_USER_BUFFER << std::endl;
		return FALSE;
	}

	if (!AliasInterfaceClassGuid || !DeviceInterfaceData)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		rslog::info_ts() << "  -> 0  gle=" << std::dec << ERROR_INVALID_PARAMETER << std::endl;
		return FALSE;
	}

	if (!AliasDeviceInterfaceData || AliasDeviceInterfaceData->cbSize != sizeof(SP_DEVICE_INTERFACE_DATA))
	{
		SetLastError(ERROR_INVALID_USER_BUFFER);
		rslog::info_ts() << "  -> 0  gle=" << std::dec << ERROR_INVALID_USER_BUFFER << std::endl;
		return FALSE;
	}

	// Some Wine/Proton builds expose SetupDiGetDeviceInterfaceAlias as an unimplemented
	// stub that aborts the process. For Rocksmith's validation flow, a pass-through alias
	// is sufficient and avoids hard runtime crashes.
	AliasDeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	AliasDeviceInterfaceData->InterfaceClassGuid = *AliasInterfaceClassGuid;
	AliasDeviceInterfaceData->Flags = DeviceInterfaceData->Flags;
	AliasDeviceInterfaceData->Reserved = DeviceInterfaceData->Reserved;
	SetLastError(ERROR_SUCCESS);
	rslog::info_ts() << "  -> 1  gle=0  via=pass-through-alias" << std::endl;
	return TRUE;
}

static BOOL WINAPI Patched_SetupDiGetDeviceInterfaceDetailW(
	HDEVINFO DeviceInfoSet,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
	PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
	DWORD DeviceInterfaceDetailDataSize,
	PDWORD RequiredSize,
	PSP_DEVINFO_DATA DeviceInfoData)
{
	rslog::info_ts() << "Patched_SetupDiGetDeviceInterfaceDetailW called - detailSize: " << std::dec << DeviceInterfaceDetailDataSize << std::endl;

	if (auto* captureToken = GetCaptureFallbackToken(DeviceInterfaceData))
	{
		HDEVINFO captureSet = EnsureCaptureInterfaceInfoSet();
		if (captureSet != INVALID_HANDLE_VALUE)
		{
			BOOL okCapture = SetupDiGetDeviceInterfaceDetailW(
				captureSet,
				&captureToken->captureInterfaceData,
				DeviceInterfaceDetailData,
				DeviceInterfaceDetailDataSize,
				RequiredSize,
				DeviceInfoData);

			const DWORD gleCapture = GetLastError();
			rslog::info_ts() << "  -> " << std::dec << okCapture << "  gle=" << gleCapture << "  via=real-capture-fallback";
			if (RequiredSize)
				rslog::info_ts() << "  requiredSize=" << *RequiredSize;
			rslog::info_ts() << std::endl;

			if (okCapture && DeviceInterfaceDetailData)
			{
				rslog::info_ts() << "  devicePath: " << DeviceInterfaceDetailData->DevicePath << std::endl;
			}

			return okCapture;
		}
	}

	if (kEnableSetupDiSynthesis && IsFakeSetupDiInterfaceData(DeviceInterfaceData))
	{
		const DWORD requiredBytes = static_cast<DWORD>(FIELD_OFFSET(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + ((wcslen(kFakeSetupDiDevicePath) + 1) * sizeof(wchar_t)));
		if (RequiredSize)
			*RequiredSize = requiredBytes;

		if (!DeviceInterfaceDetailData || DeviceInterfaceDetailDataSize < requiredBytes)
		{
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			rslog::info_ts() << "  -> 0  gle=" << std::dec << ERROR_INSUFFICIENT_BUFFER << "  requiredSize=" << requiredBytes << std::endl;
			return FALSE;
		}

		memcpy(DeviceInterfaceDetailData->DevicePath, kFakeSetupDiDevicePath, (wcslen(kFakeSetupDiDevicePath) + 1) * sizeof(wchar_t));
		if (DeviceInfoData && DeviceInfoData->cbSize == sizeof(SP_DEVINFO_DATA))
		{
			DeviceInfoData->ClassGuid = KSCATEGORY_AUDIO;
			DeviceInfoData->DevInst = 0;
			DeviceInfoData->Reserved = kFakeSetupDiInterfaceTag;
		}

		SetLastError(ERROR_SUCCESS);
		rslog::info_ts() << "  -> 1  gle=0  requiredSize=" << requiredBytes << std::endl;
		rslog::info_ts() << "  devicePath: " << kFakeSetupDiDevicePath << std::endl;
		return TRUE;
	}

	BOOL ok = SetupDiGetDeviceInterfaceDetailW(
		DeviceInfoSet,
		DeviceInterfaceData,
		DeviceInterfaceDetailData,
		DeviceInterfaceDetailDataSize,
		RequiredSize,
		DeviceInfoData);

	const DWORD gle = GetLastError();
	rslog::info_ts() << "  -> " << std::dec << ok << "  gle=" << gle;
	if (RequiredSize)
		rslog::info_ts() << "  requiredSize=" << *RequiredSize;
	rslog::info_ts() << std::endl;

	if (ok && DeviceInterfaceDetailData)
	{
		rslog::info_ts() << "  devicePath: " << DeviceInterfaceDetailData->DevicePath << std::endl;
	}

	return ok;
}

static BOOL WINAPI Patched_SetupDiGetDeviceRegistryPropertyW(
	HDEVINFO DeviceInfoSet,
	PSP_DEVINFO_DATA DeviceInfoData,
	DWORD Property,
	PDWORD PropertyRegDataType,
	PBYTE PropertyBuffer,
	DWORD PropertyBufferSize,
	PDWORD RequiredSize)
{
	rslog::info_ts() << "Patched_SetupDiGetDeviceRegistryPropertyW called - property: " << std::dec << Property << std::endl;

	if (kEnableSetupDiSynthesis && DeviceInfoData && DeviceInfoData->Reserved == kFakeSetupDiInterfaceTag)
	{
		BOOL ok = FALSE;
		switch (Property)
		{
			case SPDRP_FRIENDLYNAME:
				ok = CopyRegSzToBuffer(kFakeSetupDiFriendlyName, PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
			case SPDRP_DEVICEDESC:
				ok = CopyRegSzToBuffer(kFakeSetupDiDeviceDesc, PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
			case SPDRP_MFG:
				ok = CopyRegSzToBuffer(L"Ubisoft", PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
			case SPDRP_HARDWAREID:
				ok = CopyRegMultiSzToBuffer(L"USB\\VID_12BA&PID_00FF", PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
			case SPDRP_COMPATIBLEIDS:
				ok = CopyRegMultiSzToBuffer(L"USB\\Class_01", PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
			default:
				ok = CopyRegSzToBuffer(kFakeSetupDiRegValue, PropertyRegDataType, PropertyBuffer, PropertyBufferSize, RequiredSize);
				break;
		}

		const DWORD gle = GetLastError();
		rslog::info_ts() << "  -> " << std::dec << ok << "  gle=" << gle;
		if (PropertyRegDataType)
			rslog::info_ts() << "  type=" << *PropertyRegDataType;
		if (RequiredSize)
			rslog::info_ts() << "  requiredSize=" << *RequiredSize;
		rslog::info_ts() << std::endl;
		return ok;
	}

	BOOL ok = SetupDiGetDeviceRegistryPropertyW(
		DeviceInfoSet,
		DeviceInfoData,
		Property,
		PropertyRegDataType,
		PropertyBuffer,
		PropertyBufferSize,
		RequiredSize);

	const DWORD gle = GetLastError();
	rslog::info_ts() << "  -> " << std::dec << ok << "  gle=" << gle;
	if (PropertyRegDataType)
		rslog::info_ts() << "  type=" << *PropertyRegDataType;
	if (RequiredSize)
		rslog::info_ts() << "  requiredSize=" << *RequiredSize;
	rslog::info_ts() << std::endl;

	return ok;
}

static HKEY WINAPI Patched_SetupDiOpenDeviceInterfaceRegKey(
	HDEVINFO DeviceInfoSet,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
	DWORD Reserved,
	REGSAM samDesired)
{
	rslog::info_ts() << "Patched_SetupDiOpenDeviceInterfaceRegKey called - samDesired: 0x" << std::hex << samDesired << std::dec << std::endl;

	if (auto* captureToken = GetCaptureFallbackToken(DeviceInterfaceData))
	{
		HDEVINFO captureSet = EnsureCaptureInterfaceInfoSet();
		if (captureSet != INVALID_HANDLE_VALUE)
		{
			HKEY key = SetupDiOpenDeviceInterfaceRegKey(captureSet, &captureToken->captureInterfaceData, Reserved, samDesired);
			const DWORD gle = GetLastError();
			rslog::info_ts() << "  -> " << key << "  gle=" << std::dec << gle << "  via=real-capture-fallback" << std::endl;
			if (key && key != GetInvalidHKeyValue())
			{
				TrackSetupDiRegKey(key, true, false);
			}
			return key;
		}
	}

	if (kEnableSetupDiSynthesis && IsFakeSetupDiInterfaceData(DeviceInterfaceData))
	{
		HKEY key = nullptr;
		DWORD ignoredDisposition = 0;
		LSTATUS status = RegCreateKeyExW(
			HKEY_CURRENT_USER,
			L"Software\\RS_ASIO\\FakeRealToneInterface",
			0,
			nullptr,
			REG_OPTION_NON_VOLATILE,
			KEY_READ | KEY_WRITE,
			nullptr,
			&key,
			&ignoredDisposition);

		if (status == ERROR_SUCCESS && key)
		{
			const DWORD bytes = static_cast<DWORD>((wcslen(kFakeSetupDiRegValue) + 1) * sizeof(wchar_t));
			RegSetValueExW(key, L"DeviceInstance", 0, REG_SZ, reinterpret_cast<const BYTE*>(kFakeSetupDiRegValue), bytes);
			TrackSetupDiRegKey(key, true, true);
			SetLastError(ERROR_SUCCESS);
			rslog::info_ts() << "  -> created fake reg key: " << key << std::endl;
			return key;
		}

		SetLastError(status);
		rslog::info_ts() << "  -> failed to create fake reg key, status=" << std::dec << status << std::endl;
		return GetInvalidHKeyValue();
	}

	HKEY key = SetupDiOpenDeviceInterfaceRegKey(DeviceInfoSet, DeviceInterfaceData, Reserved, samDesired);
	const DWORD gle = GetLastError();
	rslog::info_ts() << "  -> " << key << "  gle=" << std::dec << gle << std::endl;

	if (key && key != GetInvalidHKeyValue())
	{
		TrackSetupDiRegKey(key, true, false);
	}

	return key;
}

static BOOL WINAPI Patched_SetupDiDestroyDeviceInfoList(HDEVINFO DeviceInfoSet)
{
	rslog::info_ts() << "Patched_SetupDiDestroyDeviceInfoList called" << std::endl;
	BOOL ok = SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	const DWORD gle = GetLastError();
	ClearCaptureFallbackState();
	rslog::info_ts() << "  -> " << std::dec << ok << "  gle=" << gle << std::endl;
	return ok;
}

static LSTATUS WINAPI Patched_RegQueryValueExW(
	HKEY hKey,
	LPCWSTR lpValueName,
	LPDWORD lpReserved,
	LPDWORD lpType,
	LPBYTE lpData,
	LPDWORD lpcbData)
{
	const bool tracked = IsTrackedSetupDiRegKey(hKey);
	const bool fake = IsFakeSetupDiRegKey(hKey);
	if (tracked)
	{
		rslog::info_ts() << "Patched_RegQueryValueExW (SetupDi key) - value: "
		                 << (lpValueName ? lpValueName : L"<default>")
		                 << std::endl;
	}

	LSTATUS status = RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);

	if (kEnableSetupDiSynthesis && fake && status != ERROR_SUCCESS)
	{
		if (lpType)
			*lpType = REG_SZ;

		const DWORD requiredBytes = static_cast<DWORD>((wcslen(kFakeSetupDiRegValue) + 1) * sizeof(wchar_t));
		if (lpcbData)
		{
			if (!lpData || *lpcbData < requiredBytes)
			{
				*lpcbData = requiredBytes;
				status = ERROR_MORE_DATA;
			}
			else
			{
				memcpy(lpData, kFakeSetupDiRegValue, requiredBytes);
				*lpcbData = requiredBytes;
				status = ERROR_SUCCESS;
			}
		}
		else
		{
			status = ERROR_SUCCESS;
		}
	}

	if (tracked)
	{
		rslog::info_ts() << "  -> status=" << std::dec << status;
		if (lpType)
			rslog::info_ts() << " type=" << *lpType;
		if (lpcbData)
			rslog::info_ts() << " size=" << *lpcbData;
		rslog::info_ts() << std::endl;
	}

	return status;
}

static LSTATUS WINAPI Patched_RegCloseKey(HKEY hKey)
{
	const bool tracked = IsTrackedSetupDiRegKey(hKey);
	if (tracked)
	{
		rslog::info_ts() << "Patched_RegCloseKey (SetupDi key)" << std::endl;
	}

	LSTATUS status = RegCloseKey(hKey);
	if (tracked)
	{
		TrackSetupDiRegKey(hKey, false, false);
		rslog::info_ts() << "  -> status=" << std::dec << status << std::endl;
	}

	return status;
}

void PatchOriginalCode_e0f686e0()
{
	rslog::info_ts() << __FUNCTION__ << " - patching Rocksmith 2011 via IAT" << std::endl;

	bool ok = true;

	ok &= PatchIATEntry(IAT_RVA_CoCreateInstance,
	                    reinterpret_cast<void*>(&Patched_CoCreateInstance),
	                    "CoCreateInstance");

	ok &= PatchIATEntry(IAT_RVA_RegQueryValueExW,
	                    reinterpret_cast<void*>(&Patched_RegQueryValueExW),
	                    "RegQueryValueExW");

	ok &= PatchIATEntry(IAT_RVA_RegCloseKey,
	                    reinterpret_cast<void*>(&Patched_RegCloseKey),
	                    "RegCloseKey");

	ok &= PatchIATEntry(IAT_RVA_SetupDiGetClassDevsW,
	                    reinterpret_cast<void*>(&Patched_SetupDiGetClassDevsW),
	                    "SetupDiGetClassDevsW");

	ok &= PatchIATEntry(IAT_RVA_SetupDiOpenDeviceInterfaceRegKey,
	                    reinterpret_cast<void*>(&Patched_SetupDiOpenDeviceInterfaceRegKey),
	                    "SetupDiOpenDeviceInterfaceRegKey");

	ok &= PatchIATEntry(IAT_RVA_SetupDiGetDeviceRegistryPropertyW,
	                    reinterpret_cast<void*>(&Patched_SetupDiGetDeviceRegistryPropertyW),
	                    "SetupDiGetDeviceRegistryPropertyW");

	ok &= PatchIATEntry(IAT_RVA_SetupDiGetDeviceInterfaceDetailW,
	                    reinterpret_cast<void*>(&Patched_SetupDiGetDeviceInterfaceDetailW),
	                    "SetupDiGetDeviceInterfaceDetailW");

	ok &= PatchIATEntry(IAT_RVA_SetupDiDestroyDeviceInfoList,
	                    reinterpret_cast<void*>(&Patched_SetupDiDestroyDeviceInfoList),
	                    "SetupDiDestroyDeviceInfoList");

	ok &= PatchIATEntry(IAT_RVA_SetupDiGetDeviceInterfaceAlias,
	                    reinterpret_cast<void*>(&Patched_SetupDiGetDeviceInterfaceAlias),
	                    "SetupDiGetDeviceInterfaceAlias");

	ok &= PatchIATEntry(IAT_RVA_SetupDiGetClassDevsA,
	                    reinterpret_cast<void*>(&Patched_SetupDiGetClassDevsA),
	                    "SetupDiGetClassDevsA");

	ok &= PatchIATEntry(IAT_RVA_SetupDiEnumDeviceInterfaces,
	                    reinterpret_cast<void*>(&Patched_SetupDiEnumDeviceInterfaces),
	                    "SetupDiEnumDeviceInterfaces");

	ok &= PatchIATEntry(IAT_RVA_CoMarshalInterThreadInterfaceInStream,
	                    reinterpret_cast<void*>(&Patched_CoMarshalInterThreadInterfaceInStream),
	                    "CoMarshalInterThreadInterfaceInStream");

	ok &= PatchIATEntry(IAT_RVA_CoGetInterfaceAndReleaseStream,
	                    reinterpret_cast<void*>(&Patched_CoGetInterfaceAndReleaseStream),
	                    "CoGetInterfaceAndReleaseStream");

	if (!ok)
	{
		rslog::error_ts() << __FUNCTION__ << " - one or more IAT patches failed" << std::endl;
	}
	else
	{
		rslog::info_ts() << __FUNCTION__ << " - all IAT patches applied successfully" << std::endl;
	}
}
