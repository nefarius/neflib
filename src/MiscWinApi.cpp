#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>


namespace
{
	using GUIDFromString_t = BOOL(WINAPI*)(_In_ LPCSTR, _Out_ LPGUID);
}

bool nefarius::winapi::GUIDFromString(const std::string& input, GUID* guid)
{
	// try without brackets...
	if (UuidFromStringA(RPC_CSTR(input.data()), guid) == RPC_S_INVALID_STRING_UUID)
	{
		const HMODULE shell32 = LoadLibraryA("Shell32.dll");

		if (shell32 == nullptr)
			return false;

		const auto pFnGUIDFromString = reinterpret_cast<GUIDFromString_t>(
			GetProcAddress(shell32, MAKEINTRESOURCEA(703)));

		// ...finally try with brackets
		return pFnGUIDFromString(input.c_str(), guid);
	}

	return true;
}

std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::IsAppRunningAsAdminMode()
{
	PSID adminSID = nullptr;

	SCOPE_GUARD_CAPTURE({
	                    if (adminSID)
	                    {
	                    FreeSid(adminSID);
	                    }
	                    }, adminSID);

	// Allocate and initialize a SID of the administrators group.
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(
		&authority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&adminSID))
	{
		return std::unexpected(utilities::Win32Error("AllocateAndInitializeSid"));
	}

	BOOL isMember = FALSE;

	// Determine whether the SID of administrators group is enabled in 
	// the primary access token of the process.
	if (!CheckTokenMembership(nullptr, adminSID, &isMember))
	{
		return std::unexpected(utilities::Win32Error("CheckTokenMembership"));
	}

	return isMember != FALSE;
}

std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::AdjustProcessPrivileges()
{
	HANDLE procToken;
	LUID luid;
	TOKEN_PRIVILEGES tp;

	if (!OpenProcessToken(
		GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
		&procToken
	))
	{
		return std::unexpected(utilities::Win32Error("OpenProcessToken"));
	}

	SCOPE_GUARD_CAPTURE({
	                    if (procToken)
	                    {
	                    CloseHandle(procToken);
	                    }
	                    }, procToken);

	if (!LookupPrivilegeValue(nullptr, SE_LOAD_DRIVER_NAME, &luid))
	{
		return std::unexpected(utilities::Win32Error("LookupPrivilegeValue"));
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	//
	// AdjustTokenPrivileges can succeed even when privileges are not adjusted.
	// In such case GetLastError returns ERROR_NOT_ALL_ASSIGNED.
	//
	// Hence, we check for GetLastError in both success and failure case.
	//

	(void)AdjustTokenPrivileges(
		procToken,
		FALSE,
		&tp,
		sizeof(TOKEN_PRIVILEGES),
		nullptr,
		nullptr
	);

	if (GetLastError() != ERROR_SUCCESS)
	{
		return std::unexpected(utilities::Win32Error("AdjustTokenPrivileges"));
	}

	return {};
}

std::expected<PSID, nefarius::utilities::Win32Error> nefarius::winapi::GetLogonSID(HANDLE hToken)
{
	DWORD dwLength = 0;
	PTOKEN_GROUPS ptg = nullptr;

	// Get required buffer size and allocate the TOKEN_GROUPS buffer.
	(void)GetTokenInformation(hToken, TokenGroups, ptg, 0, &dwLength);

	ptg = static_cast<PTOKEN_GROUPS>(HeapAlloc(
		GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		dwLength
	));

	// Get the token group information from the access token.
	if (!GetTokenInformation(hToken, TokenGroups, ptg, dwLength, &dwLength))
	{
		return std::unexpected(utilities::Win32Error("GetTokenInformation"));
	}

	// Loop through the groups to find the logon SID.
	for (DWORD dwIndex = 0; dwIndex < ptg->GroupCount; dwIndex++)
	{
		if ((ptg->Groups[dwIndex].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
		{
			// Found the logon SID; make a copy of it.

			dwLength = GetLengthSid(ptg->Groups[dwIndex].Sid);
			PSID pSID = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength);
			CopySid(dwLength, pSID, ptg->Groups[dwIndex].Sid);

			return pSID;
		}
	}

	return std::unexpected(utilities::Win32Error(ERROR_NOT_FOUND));
}

std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::services::CreateDriverService(PCSTR ServiceName,
	PCSTR DisplayName, PCSTR BinaryPath)
{
	SC_HANDLE hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager)
	{
		return std::unexpected(utilities::Win32Error("OpenSCManagerA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hSCManager); }, hSCManager);

	SC_HANDLE hService = CreateServiceA(
		hSCManager,
		ServiceName,
		DisplayName,
		SERVICE_START | DELETE | SERVICE_STOP,
		SERVICE_KERNEL_DRIVER,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_IGNORE,
		BinaryPath,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr
	);

	if (!hService)
	{
		return std::unexpected(utilities::Win32Error("CreateServiceA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	return {};
}

std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::services::DeleteDriverService(PCSTR ServiceName)
{
	SC_HANDLE hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager)
	{
		return std::unexpected(utilities::Win32Error("OpenSCManagerA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hSCManager); }, hSCManager);

	SC_HANDLE hService = OpenServiceA(
		hSCManager,
		ServiceName,
		SERVICE_START | DELETE | SERVICE_STOP
	);

	if (!hService)
	{
		return std::unexpected(utilities::Win32Error("OpenServiceA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	if (!DeleteService(hService))
	{
		return std::unexpected(utilities::Win32Error("DeleteService"));
	}

	return {};
}
