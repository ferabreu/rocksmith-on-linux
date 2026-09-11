#include "stdafx.h"
#include "DebugWrapperDevicePropertyStore.h"

// This changes DEFINE_PROPERTYKEY to declare (on top of defined)
#include <initguid.h>
#include <propkey.h>

// example: L"{1}.TUSBAUDIO_ENUM\\VID_1397&PID_0508&KS\\9&34A5FE73&4&4"
DEFINE_PROPERTYKEY(PKEY_Device_DeviceIdHiddenKey1, 0xb3f8fa53, 0x0004, 0x438e, 0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc, 2);

// example: L"oem24.inf:7a9b59c8209f0782:_Install_8.NTamd64:4.59.0.56775:TUSBAUDIO_ENUM\\VID_1397&PID_0508&KS"
DEFINE_PROPERTYKEY(PKEY_Device_DeviceIdHiddenKey2, 0x83DA6326, 0x97A6, 0x4088, 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29, 3);


#define DEBUG_PRINT_HR(hr) if(FAILED(hr)) rslog::info_ts() << "  hr: " << HResultToStr(hr) << std::endl

DebugWrapperDevicePropertyStore::DebugWrapperDevicePropertyStore(IPropertyStore& realPropertyStore, const std::wstring& deviceId)
	: m_RealPropertyStore(realPropertyStore)
	, m_DeviceId(deviceId)
{
	m_RealPropertyStore.AddRef();
}

DebugWrapperDevicePropertyStore::~DebugWrapperDevicePropertyStore()
{
	m_RealPropertyStore.Release();
}

// Returns a human-readable label for well-known WASAPI and device property keys,
// or nullptr for unknown keys.  Uses IsEqualGUID for comparison (available from combaseapi.h).
// This lets you identify which properties the game reads without looking up GUIDs manually.
static const char* PropKeyName(REFPROPERTYKEY key)
{
	// {A45C254E-DF1C-4EFD-8020-67D146A850E0} = DEVPKEY_Device_*
	static const GUID kDevpkey   = {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}};
	// {1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E} = PKEY_AudioEndpoint_*
	static const GUID kEndpoint  = {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}};
	// {F19F064D-082C-4E27-BC73-6882A1BB8E4C} = PKEY_AudioEngine_*
	static const GUID kEngine    = {0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}};
	// {78C34FC8-104A-4ACA-9EA4-524D52996E57} pid=256 = PKEY_Device_InstanceId
	static const GUID kInstId    = {0x78c34fc8, 0x104a, 0x4aca, {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}};
	// {B3F8FA53-0004-438E-9003-51A46E139BFC} pid=2 — contains USB VID/PID path string.
	// PRESENT in winepulse cable endpoints (10 props); MISSING in winepipewire (8 props).
	// If this returns VT_EMPTY for the cable device, cable detection will fail.
	static const GUID kHidden1   = {0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}};
	// {83DA6326-97A6-4088-9453-A1923F573B29} pid=3 — contains driver INF + USB VID/PID path.
	// Also PRESENT in winepulse, MISSING in winepipewire.
	static const GUID kHidden2   = {0x83da6326, 0x97a6, 0x4088, {0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29}};

	if (IsEqualGUID(key.fmtid, kDevpkey))
	{
		switch (key.pid)
		{
		case 2:  return "PKEY_Device_DeviceDesc";
		case 3:  return "PKEY_Device_HardwareIds";
		case 4:  return "PKEY_Device_CompatibleIds";
		case 6:  return "PKEY_Device_Class";
		case 10: return "PKEY_Device_Manufacturer";
		case 14: return "PKEY_Device_FriendlyName";
		default: return nullptr;
		}
	}
	if (IsEqualGUID(key.fmtid, kEndpoint))
	{
		switch (key.pid)
		{
		case 0:  return "PKEY_AudioEndpoint_FormFactor";
		case 2:  return "PKEY_AudioEndpoint_FullRangeSpeakers";
		case 3:  return "PKEY_AudioEndpoint_PhysicalSpeakers";
		case 4:  return "PKEY_AudioEndpoint_GUID";
		case 5:  return "PKEY_AudioEndpoint_Disable_SysFX";
		case 7:  return "PKEY_AudioEndpoint_Supports_EventDriven_Mode";
		default: return nullptr;
		}
	}
	if (IsEqualGUID(key.fmtid, kEngine))
	{
		switch (key.pid)
		{
		case 0:  return "PKEY_AudioEngine_DeviceFormat";
		case 3:  return "PKEY_AudioEngine_OEMFormat";
		default: return nullptr;
		}
	}
	if (IsEqualGUID(key.fmtid, kInstId) && key.pid == 256)
		return "PKEY_Device_InstanceId";
	if (IsEqualGUID(key.fmtid, kHidden1) && key.pid == 2)
		return "PKEY_USB_DeviceId1 [** CABLE-DETECT: USB VID/PID path -- missing under winepipewire **]";
	if (IsEqualGUID(key.fmtid, kHidden2) && key.pid == 3)
		return "PKEY_USB_DeviceId2 [** CABLE-DETECT: driver INF path -- missing under winepipewire **]";
	return nullptr;
}

HRESULT STDMETHODCALLTYPE DebugWrapperDevicePropertyStore::GetCount(DWORD *cProps)
{
	rslog::info_ts() << m_DeviceId << " " << __FUNCTION__ << std::endl;

	HRESULT hr = m_RealPropertyStore.GetCount(cProps);

	DEBUG_PRINT_HR(hr);

	if (SUCCEEDED(hr) && cProps)
	{
		m_RealPropertyCount = *cProps;
		rslog::info_ts() << "  *cProps: " << std::dec << *cProps;

		if (m_IsCableDevice)
		{
			// Probe the real store to see if it already exposes the USB ID key natively
			// (e.g., a future winepipewire that adds the properties itself).  Only inject
			// the extra 2 entries when they are genuinely absent, and use realCount+2
			// rather than a hardcoded 10 so we stay correct if the property count grows
			// for unrelated reasons in future Wine/PipeWire versions.
			PROPVARIANT pvProbe;
			PropVariantInit(&pvProbe);
			const bool hasUsbId =
				SUCCEEDED(m_RealPropertyStore.GetValue(PKEY_Device_DeviceIdHiddenKey1, &pvProbe))
				&& pvProbe.vt != VT_EMPTY;
			PropVariantClear(&pvProbe);

			if (!hasUsbId)
			{
				*cProps = m_RealPropertyCount + 2;
				rslog::info_ts() << " -> overriding to " << std::dec << *cProps
				                 << " [injecting 2 USB ID properties for cable detection;"
				                    " real count=" << m_RealPropertyCount << "]";
			}
			else
			{
				rslog::info_ts() << " [USB ID properties present natively - no injection needed]";
			}
		}
		else if (*cProps == 0)
			rslog::info_ts() << " [WARN: empty property store - device may be misconfigured]";
		rslog::info_ts() << std::endl;
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE DebugWrapperDevicePropertyStore::GetAt(DWORD iProp, PROPERTYKEY *pkey)
{
	// For the cable device, inject the 2 missing USB ID property keys at indices
	// m_RealPropertyCount (8) and m_RealPropertyCount+1 (9).  The game loops
	// GetAt(0..count-1) and calls GetValue for each returned key.  These two
	// injected keys are present in winepulse cable endpoints (count=10) but absent
	// under winepipewire (count=8).  GetValue will supply their VID_12BA values.
	if (m_IsCableDevice && m_RealPropertyCount > 0 && iProp >= m_RealPropertyCount)
	{
		if (!pkey) return E_POINTER;
		if (iProp == m_RealPropertyCount)
		{
			*pkey = PKEY_Device_DeviceIdHiddenKey1;
			rslog::info_ts() << m_DeviceId << " PropertyStore::GetAt iProp=" << std::dec << iProp
			                 << " [INJECTED PKEY_USB_DeviceId1 - cable detection key 1]" << std::endl;
			return S_OK;
		}
		if (iProp == m_RealPropertyCount + 1)
		{
			*pkey = PKEY_Device_DeviceIdHiddenKey2;
			rslog::info_ts() << m_DeviceId << " PropertyStore::GetAt iProp=" << std::dec << iProp
			                 << " [INJECTED PKEY_USB_DeviceId2 - cable detection key 2]" << std::endl;
			return S_OK;
		}
		return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
	}

	HRESULT hr = m_RealPropertyStore.GetAt(iProp, pkey);
	rslog::info_ts() << m_DeviceId << " PropertyStore::GetAt iProp=" << std::dec << iProp;
	if (SUCCEEDED(hr) && pkey)
	{
		char guidStr[40];
		const auto& g = pkey->fmtid;
		sprintf_s(guidStr, sizeof(guidStr),
		          "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		          g.Data1, g.Data2, g.Data3,
		          g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
		          g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
		const char* keyName = PropKeyName(*pkey);
		rslog::info_ts() << " key=" << guidStr << " pid=" << pkey->pid;
		if (keyName) rslog::info_ts() << " (" << keyName << ")";
	}
	rslog::info_ts() << std::endl;
	DEBUG_PRINT_HR(hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE DebugWrapperDevicePropertyStore::GetValue(REFPROPERTYKEY key, PROPVARIANT *pv)
{
	HRESULT hr = m_RealPropertyStore.GetValue(key, pv);

	if (pv)
	{
		// Identify the cable device so we know when to inject.
		// Primary signal: PKEY_Device_FriendlyName (pid=14) = "Microphone (Rocksmith Guitar Adapter Mono)"
		// Fallback signal: Wine short-name property {026E516E...} pid=2 = "Rocksmith Guitar Adapter Mono"
		// Both are derived from the USB iProduct descriptor burned into the cable firmware,
		// so they are identical for every unit on every USB port.  The fallback ensures
		// detection survives any future change to Wine's FriendlyName formatting.
		if (!m_IsCableDevice && pv->vt == VT_LPWSTR && pv->pwszVal
		    && wcsstr(pv->pwszVal, L"Rocksmith"))
		{
			static const GUID kFriendlyName = {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}};
			static const GUID kShortName    = {0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}};
			if ((IsEqualGUID(key.fmtid, kFriendlyName) && key.pid == 14) ||
			    (IsEqualGUID(key.fmtid, kShortName)    && key.pid == 2))
			{
				m_IsCableDevice = true;
				rslog::info_ts() << m_DeviceId << " [cable device identified"
				                    " - will inject 2 USB ID properties to enable cable detection]" << std::endl;
			}
		}

		// Inject the 2 USB ID properties that are absent under winepipewire.
		// The game's property enumeration loop will reach these via the injected GetAt
		// keys, call GetValue, and find VID_12BA&PID_00FF to confirm it's the cable.
		if (m_IsCableDevice && pv->vt == VT_EMPTY)
		{
			if (IsEqualPropertyKey(key, PKEY_Device_DeviceIdHiddenKey1))
			{
				static const wchar_t kVal[] = L"{1}.USB\\VID_12BA&PID_00FF\\7182&2AD191BF";
				pv->vt = VT_LPWSTR;
				pv->pwszVal = (LPWSTR)CoTaskMemAlloc(sizeof(kVal));
				if (pv->pwszVal) memcpy(pv->pwszVal, kVal, sizeof(kVal));
				rslog::info_ts() << m_DeviceId << " PropertyStore::GetValue"
				                    " [INJECTED PKEY_USB_DeviceId1] val=" << kVal << std::endl;
				return pv->pwszVal ? S_OK : E_OUTOFMEMORY;
			}
			if (IsEqualPropertyKey(key, PKEY_Device_DeviceIdHiddenKey2))
			{
				static const wchar_t kVal[] = L"USB\\VID_12BA&PID_00FF";
				pv->vt = VT_LPWSTR;
				pv->pwszVal = (LPWSTR)CoTaskMemAlloc(sizeof(kVal));
				if (pv->pwszVal) memcpy(pv->pwszVal, kVal, sizeof(kVal));
				rslog::info_ts() << m_DeviceId << " PropertyStore::GetValue"
				                    " [INJECTED PKEY_USB_DeviceId2] val=" << kVal << std::endl;
				return pv->pwszVal ? S_OK : E_OUTOFMEMORY;
			}
		}

		char guidStr[40];
		const auto& g = key.fmtid;
		sprintf_s(guidStr, sizeof(guidStr),
		          "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		          g.Data1, g.Data2, g.Data3,
		          g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
		          g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
		const char* keyName = PropKeyName(key);

		if (pv->vt == VT_EMPTY)
		{
			// A VT_EMPTY result means this property does not exist on the device.
			// Under winepipewire the cable endpoint is missing the USB ID properties
			// (marked [** CABLE-DETECT **]) that winepulse provides.  If you see those
			// keys returning VT_EMPTY for the cable device, that is the root cause of
			// the game not detecting the cable.
			rslog::info_ts() << m_DeviceId << " PropertyStore::GetValue key=" << guidStr
			                 << " pid=" << std::dec << key.pid;
			if (keyName)
				rslog::info_ts() << " (" << keyName << ")";
			rslog::info_ts() << " -> VT_EMPTY [property not present on this device]" << std::endl;
		}
		else
		{
			// Property is present - log key, type, and value for cross-driver comparison.
			rslog::info_ts() << m_DeviceId << " PropertyStore::GetValue key=" << guidStr
			                 << " pid=" << std::dec << key.pid;
			if (keyName)
				rslog::info_ts() << " (" << keyName << ")";
			rslog::info_ts() << " vt=" << pv->vt;
			if (pv->vt == VT_LPWSTR && pv->pwszVal)
				rslog::info_ts() << " val=" << std::wstring(pv->pwszVal);
			else if (pv->vt == VT_UI4)
				rslog::info_ts() << " val=" << std::dec << pv->uintVal;
			else if (pv->vt == VT_BLOB)
				rslog::info_ts() << " (blob " << pv->blob.cbSize << " bytes)";
			rslog::info_ts() << std::endl;
		}
	}

	DEBUG_PRINT_HR(hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE DebugWrapperDevicePropertyStore::SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar)
{
	rslog::info_ts() << m_DeviceId << " " << __FUNCTION__ << " - key: " << key << std::endl;

	HRESULT hr = m_RealPropertyStore.SetValue(key, propvar);
	DEBUG_PRINT_HR(hr);

	return hr;
}

HRESULT STDMETHODCALLTYPE DebugWrapperDevicePropertyStore::Commit()
{
	rslog::info_ts() << m_DeviceId << " " << __FUNCTION__ << std::endl;

	HRESULT hr = m_RealPropertyStore.Commit();
	DEBUG_PRINT_HR(hr);

	return hr;
}
