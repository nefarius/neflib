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

	const auto guard = sg::make_scope_guard([adminSID]() noexcept
	{
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
