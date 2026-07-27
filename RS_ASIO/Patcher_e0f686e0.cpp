#include "stdafx.h"
#include "dllmain.h"
#include "Patcher.h"
#include <setupapi.h>

// ---------------------------------------------------------------------------
// SetupAPI and CreateFile diagnostic hooks (pass-through, logging only).
// These let us see exactly what the game does after our registry entries are
// created: whether SetupAPI finds them, what device path it gets, and whether
// the game then tries CreateFile on that path.
// ---------------------------------------------------------------------------

// IAT RVAs for the diagnostic functions (objdump format; actual address = ImageBase + RVA)
static const DWORD IAT_RVA_SetupDiGetClassDevsA          = 0x0088d2d4;
static const DWORD IAT_RVA_SetupDiEnumDeviceInterfaces   = 0x0088d2d8;
static const DWORD IAT_RVA_SetupDiGetDeviceInterfaceDetailW = 0x0088d2c8;
static const DWORD IAT_RVA_SetupDiGetDeviceInterfaceAlias  = 0x0088d2d0;
static const DWORD IAT_RVA_SetupDiOpenDeviceInterfaceRegKey = 0x0088d2c0;
static const DWORD IAT_RVA_SetupDiDestroyDeviceInfoList    = 0x0088d2cc;
static const DWORD IAT_RVA_CreateFileW                     = 0x0088d128;

typedef HDEVINFO (WINAPI* PFN_SetupDiGetClassDevsA)(const GUID*, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI* PFN_SetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL (WINAPI* PFN_SetupDiGetDeviceInterfaceDetailW)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI* PFN_SetupDiGetDeviceInterfaceAlias)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, const GUID*, PSP_DEVICE_INTERFACE_DATA);
typedef HKEY (WINAPI* PFN_SetupDiOpenDeviceInterfaceRegKey)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, DWORD, REGSAM);
typedef BOOL (WINAPI* PFN_SetupDiDestroyDeviceInfoList)(HDEVINFO);
typedef HANDLE (WINAPI* PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static PFN_SetupDiGetClassDevsA           s_Real_SetupDiGetClassDevsA          = nullptr;
static PFN_SetupDiEnumDeviceInterfaces    s_Real_SetupDiEnumDeviceInterfaces   = nullptr;
static PFN_SetupDiGetDeviceInterfaceDetailW s_Real_SetupDiGetDeviceInterfaceDetailW = nullptr;
static PFN_SetupDiGetDeviceInterfaceAlias  s_Real_SetupDiGetDeviceInterfaceAlias = nullptr;
static PFN_SetupDiOpenDeviceInterfaceRegKey s_Real_SetupDiOpenDeviceInterfaceRegKey = nullptr;
static PFN_SetupDiDestroyDeviceInfoList    s_Real_SetupDiDestroyDeviceInfoList  = nullptr;
static PFN_CreateFileW                     s_Real_CreateFileW                   = nullptr;

static std::string DiagFmtGuid(const GUID* g)
{
	if (!g) return "(null)";
	char buf[40];
	sprintf_s(buf, sizeof(buf),
	          "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
	          g->Data1, g->Data2, g->Data3,
	          g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
	          g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
	return std::string(buf);
}

static HDEVINFO WINAPI Diag_SetupDiGetClassDevsA(const GUID* ClassGuid, PCSTR Enumerator, HWND hwndParent, DWORD Flags)
{
	HDEVINFO ret = s_Real_SetupDiGetClassDevsA(ClassGuid, Enumerator, hwndParent, Flags);
	DWORD gle = GetLastError();
	rslog::info_ts() << "Patched_SetupDiGetClassDevsA - flags=0x" << std::hex << Flags
	                 << " classGuid=" << DiagFmtGuid(ClassGuid)
	                 << " -> " << ret << " gle=" << std::dec << gle << std::endl;
	return ret;
}

static BOOL WINAPI Diag_SetupDiEnumDeviceInterfaces(HDEVINFO Set, PSP_DEVINFO_DATA DevData, const GUID* IfaceGuid, DWORD MemberIdx, PSP_DEVICE_INTERFACE_DATA IfaceData)
{
	BOOL ret = s_Real_SetupDiEnumDeviceInterfaces(Set, DevData, IfaceGuid, MemberIdx, IfaceData);
	DWORD gle = GetLastError();
	rslog::info_ts() << "Patched_SetupDiEnumDeviceInterfaces - MemberIndex=" << MemberIdx
	                 << " ifaceClassGuid=" << DiagFmtGuid(IfaceGuid)
	                 << " -> " << ret << " gle=" << gle << std::endl;
	return ret;
}

static BOOL WINAPI Diag_SetupDiGetDeviceInterfaceDetailW(HDEVINFO Set, PSP_DEVICE_INTERFACE_DATA IfaceData, PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail, DWORD DetailSize, PDWORD RequiredSize, PSP_DEVINFO_DATA DevData)
{
	BOOL ret = s_Real_SetupDiGetDeviceInterfaceDetailW(Set, IfaceData, Detail, DetailSize, RequiredSize, DevData);
	DWORD gle = GetLastError();
	rslog::info_ts() << "Patched_SetupDiGetDeviceInterfaceDetailW - detailSize=" << DetailSize
	                 << " -> " << ret << " gle=" << gle;
	if (ret && Detail && Detail->DevicePath[0])
		rslog::info_ts() << " path: " << std::wstring(Detail->DevicePath);
	rslog::info_ts() << std::endl;
	return ret;
}

static BOOL WINAPI Diag_SetupDiGetDeviceInterfaceAlias(HDEVINFO Set, PSP_DEVICE_INTERFACE_DATA IfaceData, const GUID* AliasGuid, PSP_DEVICE_INTERFACE_DATA AliasIfaceData)
{
	// SetupDiGetDeviceInterfaceAlias is declared as "@ stub" in Wine's setupapi.spec for
	// proton-cachyos 11.0 — calling s_Real_... would invoke Wine's __wine_spec_unimplemented_stub
	// which prints "unimplemented function setupapi.dll.SetupDiGetDeviceInterfaceAlias, aborting"
	// and calls ExitProcess.  We must NOT call the real function.
	// Return FALSE / ERROR_NO_SUCH_DEVINST so the game treats this as "no alias found" and
	// continues with the original device path from SetupDiGetDeviceInterfaceDetailW.
	rslog::info_ts() << "Patched_SetupDiGetDeviceInterfaceAlias - aliasClassGuid=" << DiagFmtGuid(AliasGuid)
	                 << " (skipping unimplemented Wine stub -> FALSE / ERROR_NO_SUCH_DEVINST)" << std::endl;
	SetLastError(ERROR_NO_SUCH_DEVINST);
	return FALSE;
}

static HKEY WINAPI Diag_SetupDiOpenDeviceInterfaceRegKey(HDEVINFO Set, PSP_DEVICE_INTERFACE_DATA IfaceData, DWORD Reserved, REGSAM samDesired)
{
	HKEY ret = s_Real_SetupDiOpenDeviceInterfaceRegKey(Set, IfaceData, Reserved, samDesired);
	rslog::info_ts() << "Patched_SetupDiOpenDeviceInterfaceRegKey - samDesired=0x" << std::hex << samDesired
	                 << " -> " << ret << " gle=" << GetLastError() << std::endl;
	return ret;
}

static BOOL WINAPI Diag_SetupDiDestroyDeviceInfoList(HDEVINFO Set)
{
	BOOL ret = s_Real_SetupDiDestroyDeviceInfoList(Set);
	rslog::info_ts() << "Patched_SetupDiDestroyDeviceInfoList -> " << ret << " gle=" << GetLastError() << std::endl;
	return ret;
}

static HANDLE WINAPI Diag_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSA, DWORD dwCD, DWORD dwFlags, HANDLE hTemplate)
{
	HANDLE ret = s_Real_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSA, dwCD, dwFlags, hTemplate);
	// Only log paths that could relate to KS/USB audio device access
	if (lpFileName && (wcsstr(lpFileName, L"VID_12BA") || wcsstr(lpFileName, L"RS_ASIO") || wcsstr(lpFileName, L"KSCATEGORY")))
	{
		rslog::info_ts() << "Patched_CreateFileW: " << std::wstring(lpFileName)
		                 << " access=0x" << std::hex << dwDesiredAccess
		                 << " -> " << ret << " gle=" << GetLastError() << std::endl;
	}
	return ret;
}

// ---------------------------------------------------------------------------
// Ensure the Real Tone Cable (USB VID_12BA:PID_00FF) is registered in Wine's
// KS device class registry.
//
// Newer Wine/Proton no longer automatically populates these entries when a USB
// audio device is connected via PipeWire. Without them, the game's SetupAPI
// cable-presence scan (SetupDiGetClassDevsA for KSCATEGORY_AUDIO) returns no
// results and the game never opens guitar input.
//
// The cable's USB product string is "Rocksmith USB Guitar Adapter" — this is
// the value Wine's old USB audio driver stored as the KS interface FriendlyName
// and is what the game's cable-detection code expects to find.
//
// We create the entries if they are absent; if they already exist (old Wine
// version) we leave them alone. The "RS_ASIO" suffix is used as the device
// instance ID so we can identify our own entries unambiguously.
// ---------------------------------------------------------------------------

static void EnsureRealToneCableRegistered()
{
	rslog::info_ts() << "EnsureRealToneCableRegistered - checking KS device class registrations..." << std::endl;

	// USB product string for VID_12BA:00FF (confirmed via lsusb iProduct field)
	static const wchar_t kFriendlyName[]   = L"Rocksmith USB Guitar Adapter";
	static const wchar_t kDeviceInstance[] = L"USB\\VID_12BA&PID_00FF\\RS_ASIO";
	static const wchar_t kInstanceSuffix[] = L"RS_ASIO";

	// KS class GUIDs the game's cable-detection scan queries:
	//   {6994AD04-...} = KSCATEGORY_AUDIO          (primary scan)
	//   {65E8773E-...} = KSCATEGORY_AUDIO_DEVICE   (alias query 1)
	//   {65E8773D-...} = KSCATEGORY_AUDIO_CONTROL  (alias query 2)
	//   {EB115FFC-...} = KSCATEGORY_WDMAUD         (alias query 3, second loop)
	static const wchar_t* kClassGuids[] = {
		L"{6994AD04-93EF-11D0-A3CC-00A0C9223196}",
		L"{65E8773E-8F56-11D0-A3B9-00A0C9223196}",
		L"{65E8773D-8F56-11D0-A3B9-00A0C9223196}",
		L"{EB115FFC-10C8-4964-831D-6DCB02E6F23F}",
	};

	// Wine's SETUPDI_EnumerateMatchingInterfaces reads the DeviceInstance value from the
	// DeviceClasses interface key and then looks up that instance in
	// HKLM\SYSTEM\CurrentControlSet\Enum.  If the Enum entry is absent the interface is
	// never surfaced, regardless of what exists under DeviceClasses.
	// Create the Enum device node (USB\VID_12BA&PID_00FF\RS_ASIO) if it is missing.
	{
		static const wchar_t kEnumKey[] =
			L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_12BA&PID_00FF\\RS_ASIO";
		// Use KSCATEGORY_AUDIO as the device class GUID (the value is arbitrary for
		// our purposes; Wine only requires a valid GUID string to call create_device).
		static const wchar_t kClassGuid[] = L"{6994AD04-93EF-11D0-A3CC-00A0C9223196}";
		HKEY hKey = nullptr;
		DWORD disp = 0;
		LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kEnumKey, 0, nullptr,
		                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
		if (rc == ERROR_SUCCESS)
		{
			if (disp == REG_CREATED_NEW_KEY)
			{
				RegSetValueExW(hKey, L"ClassGUID", 0, REG_SZ,
				               (const BYTE*)kClassGuid,
				               (DWORD)((wcslen(kClassGuid) + 1) * sizeof(wchar_t)));
				RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ,
				               (const BYTE*)kFriendlyName,
				               (DWORD)((wcslen(kFriendlyName) + 1) * sizeof(wchar_t)));
				rslog::info_ts() << "  created Enum device node" << std::endl;
			}
			RegCloseKey(hKey);
		}
		else
		{
			rslog::error_ts() << "  RegCreateKeyExW failed for Enum device node (rc=" << rc << ")" << std::endl;
		}
	}

	int created = 0;
	for (const auto* guid : kClassGuids)
	{
		// Registry key under DeviceClasses for this interface:
		//   SYSTEM\...\DeviceClasses\{GUID}\##?#USB#VID_12BA&PID_00FF#RS_ASIO#{GUID}
		// (Windows convention: \\?\ → ##?# and each path separator → #)
		std::wstring wGuid = guid;
		std::wstring ifaceKey = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\") +
		                        wGuid + L"\\##?#USB#VID_12BA&PID_00FF#" + kInstanceSuffix + L"#" + wGuid;
		std::wstring instanceKey = ifaceKey + L"\\#";
		std::wstring controlKey  = instanceKey + L"\\Control";
		std::wstring paramsKey   = instanceKey + L"\\Device Parameters";

		// SymbolicLink value: lowercase device path (Windows convention)
		std::wstring symlink = std::wstring(L"\\\\?\\USB#VID_12BA&PID_00FF#") + kInstanceSuffix + L"#" + wGuid;
		std::transform(symlink.begin(), symlink.end(), symlink.begin(),
		               [](wchar_t c) { return (wchar_t)::towlower(c); });

		// Skip only if the \#\Control\Linked value already exists.
		// Wine's is_linked() checks exactly this; without it DIGCF_PRESENT will skip
		// the interface even if the \# subkey exists.
		{
			HKEY hCheck = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, controlKey.c_str(), 0, KEY_READ, &hCheck) == ERROR_SUCCESS)
			{
				DWORD linked = 0, type = 0, size = sizeof(linked);
				bool hasLinked = (RegQueryValueExW(hCheck, L"Linked", nullptr, &type,
				                                   (BYTE*)&linked, &size) == ERROR_SUCCESS
				                  && type == REG_DWORD && linked != 0);
				RegCloseKey(hCheck);
				if (hasLinked)
				{
					rslog::info_ts() << "  already registered (Control\\Linked present): " << wGuid << std::endl;
					continue;
				}
			}
		}

		// Create/open interface key with SymbolicLink and DeviceInstance values
		{
			HKEY hKey = nullptr;
			DWORD disp = 0;
			LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, ifaceKey.c_str(), 0, nullptr,
			                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
			if (rc != ERROR_SUCCESS)
			{
				rslog::error_ts() << "  RegCreateKeyExW failed for interface key " << wGuid
				                  << " (rc=" << rc << ")" << std::endl;
				continue;
			}
			RegSetValueExW(hKey, L"SymbolicLink", 0, REG_SZ,
			               (const BYTE*)symlink.c_str(),
			               (DWORD)((symlink.size() + 1) * sizeof(wchar_t)));
			RegSetValueExW(hKey, L"DeviceInstance", 0, REG_SZ,
			               (const BYTE*)kDeviceInstance,
			               (DWORD)((wcslen(kDeviceInstance) + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}

		// Create/open \# instance subkey with SymbolicLink
		{
			HKEY hKey = nullptr;
			DWORD disp = 0;
			LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, nullptr,
			                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
			if (rc != ERROR_SUCCESS)
			{
				rslog::error_ts() << "  RegCreateKeyExW failed for instance key " << wGuid
				                  << " (rc=" << rc << ")" << std::endl;
				continue;
			}
			RegSetValueExW(hKey, L"SymbolicLink", 0, REG_SZ,
			               (const BYTE*)symlink.c_str(),
			               (DWORD)((symlink.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}

		// Create \#\Control with Linked=1.
		// Wine's is_linked() reads this to decide if the interface is PRESENT
		// (DIGCF_PRESENT / SPINT_ACTIVE).  Without it SetupDiEnumDeviceInterfaces
		// returns ERROR_NO_MORE_ITEMS even though the \# subkey exists.
		{
			HKEY hKey = nullptr;
			DWORD disp = 0;
			LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, controlKey.c_str(), 0, nullptr,
			                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
			if (rc != ERROR_SUCCESS)
			{
				rslog::error_ts() << "  RegCreateKeyExW failed for Control key " << wGuid
				                  << " (rc=" << rc << ")" << std::endl;
				continue;
			}
			DWORD linked = 1;
			RegSetValueExW(hKey, L"Linked", 0, REG_DWORD,
			               (const BYTE*)&linked, sizeof(linked));
			RegCloseKey(hKey);
			++created;
		}

		// Create \#\Device Parameters with FriendlyName
		{
			HKEY hKey = nullptr;
			DWORD disp = 0;
			LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, paramsKey.c_str(), 0, nullptr,
			                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disp);
			if (rc == ERROR_SUCCESS)
			{
				RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ,
				               (const BYTE*)kFriendlyName,
				               (DWORD)((wcslen(kFriendlyName) + 1) * sizeof(wchar_t)));
				RegCloseKey(hKey);
			}
			else
			{
				rslog::error_ts() << "  RegCreateKeyExW failed for params key " << wGuid
				                  << " (rc=" << rc << ")" << std::endl;
			}
		}
	}

	rslog::info_ts() << "EnsureRealToneCableRegistered: created " << created
	                 << " missing interface registration(s)" << std::endl;
}

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
//   CoCreateInstance                      RVA 0x0088d47c  (ole32.dll)
//   CoMarshalInterThreadInterfaceInStream  RVA 0x0088d490  (ole32.dll)
//   CoGetInterfaceAndReleaseStream         RVA 0x0088d494  (ole32.dll)

// Known IAT RVAs - these are fixed offsets from the module base and do not change
// because the executable does not use ASLR (ImageBase is fixed at 0x00400000).
static const DWORD IAT_RVA_CoCreateInstance                     = 0x0088d47c;
static const DWORD IAT_RVA_CoMarshalInterThreadInterfaceInStream = 0x0088d490;
static const DWORD IAT_RVA_CoGetInterfaceAndReleaseStream        = 0x0088d494;

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
	rslog::info_ts() << "Patched_CoMarshalInterThreadInterfaceInStream called" << std::endl;
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
	rslog::info_ts() << "Patched_CoGetInterfaceAndReleaseStream called" << std::endl;
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

void PatchOriginalCode_e0f686e0()
{
	rslog::info_ts() << __FUNCTION__ << " - patching Rocksmith 2011 via IAT" << std::endl;

	// Wine/Proton regression: newer versions no longer auto-register USB audio devices
	// in the KS device class registry. Create the missing entries so that the game's
	// SetupAPI cable-presence check finds the Real Tone Cable.
	EnsureRealToneCableRegistered();

	// Capture original function pointers from the IAT before we overwrite the slots.
	// The diagnostic wrappers call through these to preserve real behaviour.
	{
		const HMODULE hMod = GetModuleHandle(NULL);
		auto ReadSlot = [hMod](DWORD rva) -> void* {
			return *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(hMod) + rva);
		};
		s_Real_SetupDiGetClassDevsA          = (PFN_SetupDiGetClassDevsA)         ReadSlot(IAT_RVA_SetupDiGetClassDevsA);
		s_Real_SetupDiEnumDeviceInterfaces   = (PFN_SetupDiEnumDeviceInterfaces)  ReadSlot(IAT_RVA_SetupDiEnumDeviceInterfaces);
		s_Real_SetupDiGetDeviceInterfaceDetailW = (PFN_SetupDiGetDeviceInterfaceDetailW)ReadSlot(IAT_RVA_SetupDiGetDeviceInterfaceDetailW);
		s_Real_SetupDiGetDeviceInterfaceAlias   = (PFN_SetupDiGetDeviceInterfaceAlias)  ReadSlot(IAT_RVA_SetupDiGetDeviceInterfaceAlias);
		s_Real_SetupDiOpenDeviceInterfaceRegKey = (PFN_SetupDiOpenDeviceInterfaceRegKey)ReadSlot(IAT_RVA_SetupDiOpenDeviceInterfaceRegKey);
		s_Real_SetupDiDestroyDeviceInfoList  = (PFN_SetupDiDestroyDeviceInfoList) ReadSlot(IAT_RVA_SetupDiDestroyDeviceInfoList);
		s_Real_CreateFileW                   = (PFN_CreateFileW)                  ReadSlot(IAT_RVA_CreateFileW);
	}

	bool ok = true;

	ok &= PatchIATEntry(IAT_RVA_CoCreateInstance,
	                    reinterpret_cast<void*>(&Patched_CoCreateInstance),
	                    "CoCreateInstance");

	ok &= PatchIATEntry(IAT_RVA_CoMarshalInterThreadInterfaceInStream,
	                    reinterpret_cast<void*>(&Patched_CoMarshalInterThreadInterfaceInStream),
	                    "CoMarshalInterThreadInterfaceInStream");

	ok &= PatchIATEntry(IAT_RVA_CoGetInterfaceAndReleaseStream,
	                    reinterpret_cast<void*>(&Patched_CoGetInterfaceAndReleaseStream),
	                    "CoGetInterfaceAndReleaseStream");

	// Diagnostic-only hooks: pass through to real functions, log inputs/results.
	PatchIATEntry(IAT_RVA_SetupDiGetClassDevsA,           reinterpret_cast<void*>(&Diag_SetupDiGetClassDevsA),           "SetupDiGetClassDevsA");
	PatchIATEntry(IAT_RVA_SetupDiEnumDeviceInterfaces,    reinterpret_cast<void*>(&Diag_SetupDiEnumDeviceInterfaces),    "SetupDiEnumDeviceInterfaces");
	PatchIATEntry(IAT_RVA_SetupDiGetDeviceInterfaceDetailW,reinterpret_cast<void*>(&Diag_SetupDiGetDeviceInterfaceDetailW),"SetupDiGetDeviceInterfaceDetailW");
	PatchIATEntry(IAT_RVA_SetupDiGetDeviceInterfaceAlias, reinterpret_cast<void*>(&Diag_SetupDiGetDeviceInterfaceAlias), "SetupDiGetDeviceInterfaceAlias");
	PatchIATEntry(IAT_RVA_SetupDiOpenDeviceInterfaceRegKey,reinterpret_cast<void*>(&Diag_SetupDiOpenDeviceInterfaceRegKey),"SetupDiOpenDeviceInterfaceRegKey");
	PatchIATEntry(IAT_RVA_SetupDiDestroyDeviceInfoList,   reinterpret_cast<void*>(&Diag_SetupDiDestroyDeviceInfoList),   "SetupDiDestroyDeviceInfoList");
	PatchIATEntry(IAT_RVA_CreateFileW,                    reinterpret_cast<void*>(&Diag_CreateFileW),                    "CreateFileW");

	if (!ok)
	{
		rslog::error_ts() << __FUNCTION__ << " - one or more IAT patches failed" << std::endl;
	}
	else
	{
		rslog::info_ts() << __FUNCTION__ << " - all IAT patches applied successfully" << std::endl;
	}
}

