// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::winapi
{
	bool GUIDFromString(const std::string& input, GUID* guid);

	std::expected<bool, nefarius::utilities::Win32Error> IsAppRunningAsAdminMode();

	std::expected<void, nefarius::utilities::Win32Error> AdjustProcessPrivileges();

	std::expected<PSID, nefarius::utilities::Win32Error> GetLogonSID(HANDLE hToken);

	std::expected<void, nefarius::utilities::Win32Error> SetPrivilege(LPCWSTR privilege, int enable, HANDLE process = GetCurrentProcess());

	namespace fs
	{
		std::expected<void, nefarius::utilities::Win32Error> TakeFileOwnership(LPCWSTR file);
	}

	namespace services
	{
		std::expected<void, nefarius::utilities::Win32Error> CreateDriverService(PCSTR ServiceName, PCSTR DisplayName, PCSTR BinaryPath);

		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(PCSTR ServiceName);
	}
}
