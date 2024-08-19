// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;

namespace
{
	using GUIDFromString_t = BOOL(WINAPI*)(_In_ LPCSTR, _Out_ LPGUID);
}

std::expected<GUID, Win32Error> nefarius::winapi::GUIDFromString(const std::string& input)
{
	GUID guid = {};

	// try without brackets...
	if (UuidFromStringA(RPC_CSTR(input.data()), &guid) == RPC_S_INVALID_STRING_UUID)
	{
		const HMODULE shell32 = LoadLibraryA("Shell32.dll");

		if (shell32 == nullptr)
		{
			return std::unexpected(Win32Error("LoadLibraryA"));
		}

		SCOPE_GUARD_CAPTURE({ FreeLibrary(shell32); }, shell32);

		const auto pFnGUIDFromString = reinterpret_cast<GUIDFromString_t>(
			GetProcAddress(shell32, MAKEINTRESOURCEA(703)));

		// ...finally try with brackets
		if (!pFnGUIDFromString(input.c_str(), &guid))
		{
			return std::unexpected(Win32Error("pFnGUIDFromString"));
		}
	}

	return guid;
}

SYSTEM_INFO nefarius::winapi::SafeGetNativeSystemInfo()
{
	SYSTEM_INFO systemInfo{};

	using GetNativeSystemInfoProc = void(WINAPI*)(LPSYSTEM_INFO lpSystemInfo);
    const auto pFun = (GetNativeSystemInfoProc)GetProcAddress(GetModuleHandleW(L"kernel32"), "GetNativeSystemInfo");

	if (pFun)
	{
		pFun(&systemInfo);		
	}
	else
	{
		GetSystemInfo(&systemInfo);
	}

	return systemInfo;
}

std::expected<DWORD, Win32Error> nefarius::winapi::GetParentProcessID(DWORD ProcessId)
{
	PROCESSENTRY32W pe32{sizeof(PROCESSENTRY32W)};
	DWORD dwParentPID = 0;

	const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
	{
		return std::unexpected(Win32Error("CreateToolhelp32Snapshot"));
	}

	SCOPE_GUARD_CAPTURE({ CloseHandle(hSnapshot); }, hSnapshot);

	if (Process32FirstW(hSnapshot, &pe32))
	{
		do
		{
			if (pe32.th32ProcessID == ProcessId)
			{
				dwParentPID = pe32.th32ParentProcessID;
				break;
			}
		}
		while (Process32NextW(hSnapshot, &pe32));

		if (dwParentPID == 0)
		{
			return std::unexpected(Win32Error("Process32NextW"));
		}
	}
	else
	{
		return std::unexpected(Win32Error("Process32FirstW"));
	}

	return dwParentPID;
}
