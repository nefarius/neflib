// ReSharper disable CppCStyleCast
// ReSharper disable CppRedundantQualifier
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;


std::expected<bool, Win32Error> nefarius::winapi::security::IsAppRunningAsAdminMode()
{
	PSID adminSID = nullptr;

	SCOPE_GUARD_CAPTURE({ if (adminSID) FreeSid(adminSID); }, adminSID);

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
		return std::unexpected(Win32Error("AllocateAndInitializeSid"));
	}

	BOOL isMember = FALSE;

	// Determine whether the SID of administrators group is enabled in 
	// the primary access token of the process.
	if (!CheckTokenMembership(nullptr, adminSID, &isMember))
	{
		return std::unexpected(Win32Error("CheckTokenMembership"));
	}

	return isMember != FALSE;
}

std::expected<void, Win32Error> nefarius::winapi::security::AdjustProcessPrivileges()
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
		return std::unexpected(Win32Error("OpenProcessToken"));
	}

	SCOPE_GUARD_CAPTURE({ if (procToken) CloseHandle(procToken); }, procToken);

	if (!LookupPrivilegeValue(nullptr, SE_LOAD_DRIVER_NAME, &luid))
	{
		return std::unexpected(Win32Error("LookupPrivilegeValue"));
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
		return std::unexpected(Win32Error("AdjustTokenPrivileges"));
	}

	return {};
}

std::expected<PSID, Win32Error> nefarius::winapi::security::GetLogonSID(HANDLE hToken)
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
		return std::unexpected(Win32Error("GetTokenInformation"));
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

	return std::unexpected(Win32Error(ERROR_NOT_FOUND));
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::winapi::security::SetPrivilege(
	const StringType& Privilege, int Enable, HANDLE Process)
{
	const std::wstring privilege = ConvertToWide(Privilege);

	TOKEN_PRIVILEGES tp;
	LUID luid;
	HANDLE token;

	if (!OpenProcessToken(Process, TOKEN_ADJUST_PRIVILEGES, &token))
	{
		return std::unexpected(Win32Error("OpenProcessToken"));
	}

	SCOPE_GUARD_CAPTURE({ CloseHandle(token); }, token);

	if (!LookupPrivilegeValueW(nullptr, privilege.c_str(), &luid))
	{
		return std::unexpected(Win32Error("LookupPrivilegeValueW"));
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : FALSE;

	// Enable the privilege or disable all privileges.
	if (!AdjustTokenPrivileges(token, 0, &tp, NULL, nullptr, nullptr))
	{
		return std::unexpected(Win32Error("AdjustTokenPrivileges"));
	}

	return {};
}
