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

	SCOPE_GUARD_CAPTURE(adminSID, {
	                    if (adminSID)
	                    {
	                    FreeSid(adminSID);
	                    }
	                    });

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

	SCOPE_GUARD_CAPTURE(procToken, {
	                    if (procToken)
	                    {
	                    CloseHandle(procToken);
	                    }
	                    });

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
