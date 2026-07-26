#include "stdafx.h"
#include "dllmain.h"
#include "Patcher.h"

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

	int created = 0;
	for (const auto* guid : kClassGuids)
	{
		// Registry key under DeviceClasses for this interface:
		//   SYSTEM\...\DeviceClasses\{GUID}\##?#USB#VID_12BA&PID_00FF#RS_ASIO#{GUID}
		// (Windows convention: \\?\ → ##?# and each path separator → #)
		std::wstring wGuid = guid;
		std::wstring ifaceKey = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\") +
		                        wGuid + L"\\##?#USB#VID_12BA&PID_00FF#" + kInstanceSuffix + L"#" + wGuid;
		std::wstring paramsKey = ifaceKey + L"\\#\\Device Parameters";

		// SymbolicLink value: lowercase device path (Windows convention)
		std::wstring symlink = std::wstring(L"\\\\?\\USB#VID_12BA&PID_00FF#") + kInstanceSuffix + L"#" + wGuid;
		std::transform(symlink.begin(), symlink.end(), symlink.begin(),
		               [](wchar_t c) { return (wchar_t)::towlower(c); });

		// Skip if the interface key already exists (old Wine that still creates these)
		{
			HKEY hCheck = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ifaceKey.c_str(), 0, KEY_READ, &hCheck) == ERROR_SUCCESS)
			{
				RegCloseKey(hCheck);
				rslog::info_ts() << "  already registered: " << wGuid << std::endl;
				continue;
			}
		}

		// Create interface key with SymbolicLink and DeviceInstance values
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
			++created;
		}

		// Create #\Device Parameters key with FriendlyName
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

	if (!ok)
	{
		rslog::error_ts() << __FUNCTION__ << " - one or more IAT patches failed" << std::endl;
	}
	else
	{
		rslog::info_ts() << __FUNCTION__ << " - all IAT patches applied successfully" << std::endl;
	}
}
