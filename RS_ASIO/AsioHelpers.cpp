#include "stdafx.h"
#include "AsioHelpers.h"
#include "AsioSharedHost.h"

static const TCHAR* ASIO_PATH = TEXT("software\\asio");
static const TCHAR* ASIO_DESC = TEXT("description");
static const TCHAR* COM_CLSID = TEXT("clsid");
static const TCHAR* INPROC_SERVER = TEXT("InprocServer32");

template<size_t bufferSize>
static bool ReadRegistryStringA(std::string& out, HKEY hKey, const TCHAR* valueName)
{
	char buffer[bufferSize];
	DWORD dataType = 0;
	DWORD readSize = bufferSize;
	LSTATUS regStatus = RegQueryValueEx(hKey, valueName, nullptr, &dataType, (BYTE*)buffer, &readSize);
	if (regStatus != ERROR_SUCCESS || dataType != REG_SZ)
		return false;

	if (readSize == 0)
	{
		out.clear();
		return false;
	}

#ifdef UNICODE
	char converted[bufferSize];

	if (WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)buffer, readSize / sizeof(wchar_t), converted, bufferSize, nullptr, nullptr) == 0)
		return false;

	out = converted;
#else
	out = buffer;
#endif

	return true;
}

template<size_t bufferSize>
static bool ReadRegistryStringW(std::wstring& out, HKEY hKey, const TCHAR* valueName)
{
	char buffer[bufferSize];
	DWORD dataType = 0;
	DWORD readSize = bufferSize;
	LSTATUS regStatus = RegQueryValueEx(hKey, valueName, nullptr, &dataType, (BYTE*)buffer, &readSize);
	if (regStatus != ERROR_SUCCESS || dataType != REG_SZ)
		return false;

	if (readSize == 0)
	{
		out.clear();
		return false;
	}

#ifdef UNICODE
	out = (wchar_t*)buffer;
#else
	wchar_t converted[bufferSize];

	if (MultiByteToWideChar(CP_ACP, 0, buffer, readSize, converted, bufferSize) == 0)
		return false;

	out = converted;
#endif

	return true;
}

static bool ReadRegistryClsid(CLSID& out, HKEY hKey, const TCHAR* valueName)
{
	//RegQueryValueEx

#ifdef UNICODE
	std::wstring str;
	bool res = ReadRegistryStringW<128>(str, hKey, valueName);
#else
	std::string str;
	bool res = ReadRegistryStringA<128>(str, hKey, valueName);
#endif
	if (!res)
		return false;

	HRESULT hr = CLSIDFromString(str.c_str(), &out);
	if (FAILED(hr))
		return false;

	return true;
}

static bool GetRegistryAsioDriverPath(std::string& out, const CLSID& clsid)
{
	LPOLESTR clsidStr = nullptr;
	if (FAILED(StringFromCLSID(clsid, &clsidStr)))
	{
		return false;
	}

	bool result = false;
	HKEY hKeyClsidRoot = nullptr;

	LSTATUS regStatus = RegOpenKey(HKEY_CLASSES_ROOT, COM_CLSID, &hKeyClsidRoot);
	if (regStatus == ERROR_SUCCESS)
	{
		HKEY hKeyClsidFound = nullptr;
		regStatus = RegOpenKeyEx(hKeyClsidRoot, clsidStr, 0, KEY_READ, &hKeyClsidFound);
		if (regStatus == ERROR_SUCCESS)
		{
			HKEY hKeyInprocServer = nullptr;
			regStatus = RegOpenKeyEx(hKeyClsidFound, INPROC_SERVER, 0, KEY_READ, &hKeyInprocServer);
			if (regStatus == ERROR_SUCCESS)
			{
				result = ReadRegistryStringA<MAX_PATH>(out, hKeyInprocServer, nullptr);
				RegCloseKey(hKeyInprocServer);
			}

			RegCloseKey(hKeyClsidFound);
		}

		RegCloseKey(hKeyClsidRoot);
	}

	CoTaskMemFree(clsidStr);

	return result;
}


static bool GetRegistryDriverInfo(AsioHelpers::DriverInfo& outInfo, HKEY hKey, const TCHAR* keyName)
{
	bool result = false;

	HKEY	hksub;
	LSTATUS regStatus = RegOpenKeyEx(hKey, keyName, 0, KEY_READ, &hksub);
	if (regStatus == ERROR_SUCCESS)
	{
		AsioHelpers::DriverInfo tmpInfo;

		// set name
		tmpInfo.Name = ConvertWStrToStr(keyName);

		// read CLSID
		result = ReadRegistryClsid(tmpInfo.Clsid, hksub, COM_CLSID);
		if (result)
		{
			// read description
			ReadRegistryStringA<128>(tmpInfo.Description, hksub, ASIO_DESC);

			// figure out DLL path
			result = GetRegistryAsioDriverPath(tmpInfo.DllPath, tmpInfo.Clsid);
		}

		RegCloseKey(hksub);

		if (result)
		{
			outInfo = std::move(tmpInfo);
		}
	}

	return result;
}

static std::optional<AsioHelpers::DriverInfo> GetWineAsioInfo()
{
	static std::optional<AsioHelpers::DriverInfo> result;

	static bool isFirstCall = true;
	if (!isFirstCall)
		return result;
	isFirstCall = false;

	// Under Wine WOW64 (64-bit Wine running a 32-bit game), 32-bit DLLs live in
	// syswow64, not system32.  A bare LoadLibraryExA("wineasio32.dll") may not find
	// them via the DLL search path, so we also probe explicit system32 and syswow64
	// paths as fallbacks.
	char sysDir[MAX_PATH] = {};
	char wow64Dir[MAX_PATH] = {};
	GetSystemDirectoryA(sysDir, MAX_PATH);
	// GetSystemWow64DirectoryA returns 0 on pure 32-bit Windows; that's fine below.
	if (!GetSystemWow64DirectoryA(wow64Dir, MAX_PATH))
		strcpy_s(wow64Dir, "C:\\windows\\syswow64");

	std::array dllNames = { "wineasio32.dll", "wineasio.dll" };

	for (const char* dllName : dllNames)
	{
		if (result.has_value())
			break;

		rslog::info_ts() << __FUNCTION__ << " - Looking for \"" << dllName << "\"..." << std::endl;

		// Try bare name first (standard DLL search path), then explicit directories.
		std::vector<std::string> candidates;
		candidates.push_back(dllName);
		if (sysDir[0])   candidates.push_back(std::string(sysDir)   + "\\" + dllName);
		if (wow64Dir[0]) candidates.push_back(std::string(wow64Dir) + "\\" + dllName);

		for (const auto& candidate : candidates)
		{
			if (result.has_value())
				break;

			rslog::info_ts() << "  Trying \"" << candidate << "\"...";

			HMODULE hWineAsio = LoadLibraryExA(candidate.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
			if (hWineAsio)
			{
				char path[512] = {};
				if (GetModuleFileNameA(hWineAsio, path, sizeof(path) - 1))
				{
					rslog::info << "  found at \"" << path << "\"." << std::endl;

					AsioHelpers::DriverInfo info;
					info.Clsid = { 0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };
					info.Description = "Auto-detected wineasio dll";
					info.DllPath = path;
					info.Name = "wineasio-rsasio";

					rslog::info_ts() << "  name: " << info.Name.c_str() << std::endl;
					result = info;
				}
				else
				{
					rslog::info << "  loaded but GetModuleFileNameA failed." << std::endl;
				}
				FreeLibrary(hWineAsio);
			}
			else
			{
				DWORD gle = GetLastError();
				rslog::info << "  gle=" << gle;
				if (gle == ERROR_BAD_EXE_FORMAT)
					rslog::info << " (ERROR_BAD_EXE_FORMAT - wrong architecture: 64-bit DLL in a 32-bit process?)";
				else if (gle == ERROR_MOD_NOT_FOUND)
					rslog::info << " (ERROR_MOD_NOT_FOUND)";
				rslog::info << std::endl;
			}
		}
	}

	return result;
}

std::vector<AsioHelpers::DriverInfo> AsioHelpers::FindDrivers()
{
	static std::optional<AsioHelpers::DriverInfo> WineAsioInfo = GetWineAsioInfo();

	rslog::info_ts() << __FUNCTION__ << std::endl;

	std::vector<AsioHelpers::DriverInfo> result;

	HKEY hkEnum = 0;
	LSTATUS regStatus = RegOpenKey(HKEY_LOCAL_MACHINE, ASIO_PATH, &hkEnum);
	if (regStatus == ERROR_SUCCESS)
	{
		TCHAR keyName[MAX_PATH];
		DWORD keyIndex = 0;

		while (RegEnumKey(hkEnum, keyIndex++, keyName, MAX_PATH) == ERROR_SUCCESS)
		{
			AsioHelpers::DriverInfo info;
			if (GetRegistryDriverInfo(info, hkEnum, keyName))
			{
				rslog::info_ts() << "  " << info.Name.c_str() << std::endl;
				result.push_back(std::move(info));
			}
		}

		RegCloseKey(hkEnum);
	}

	if (WineAsioInfo.has_value())
	{
		rslog::info_ts() << "  " << (*WineAsioInfo).Name.c_str() << std::endl;
		result.push_back(*WineAsioInfo);
	}

	return result;
}

AsioSharedHost* AsioHelpers::CreateAsioHost(const DriverInfo& driverInfo)
{
	AsioSharedHost* host = new AsioSharedHost(driverInfo.Clsid, driverInfo.DllPath);
	if (!host->IsValid())
	{
		host->Release();
		host = nullptr;
	}

	return host;
}
