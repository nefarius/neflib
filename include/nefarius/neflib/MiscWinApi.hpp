// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::winapi
{
	std::expected<GUID, nefarius::utilities::Win32Error> GUIDFromString(const std::string& input);

	std::expected<bool, nefarius::utilities::Win32Error> IsAppRunningAsAdminMode();

	std::expected<void, nefarius::utilities::Win32Error> AdjustProcessPrivileges();

	std::expected<PSID, nefarius::utilities::Win32Error> GetLogonSID(HANDLE hToken);

	std::expected<void, nefarius::utilities::Win32Error> SetPrivilege(LPCWSTR privilege, int enable, HANDLE process = GetCurrentProcess());

	namespace fs
	{
		struct Version
		{
			union
			{
				struct  // NOLINT(clang-diagnostic-nested-anon-types)
				{
					uint16_t Major;
					uint16_t Minor;
					uint16_t Build;
					uint16_t Private;
				};

				uint64_t Value;
			};
		};

		std::expected<void, nefarius::utilities::Win32Error> TakeFileOwnership(LPCWSTR file);

		std::expected<Version, nefarius::utilities::Win32Error> GetProductVersionFromFile(const std::string& filePath);

		std::expected<Version, nefarius::utilities::Win32Error> GetFileVersionFromFile(const std::string& filePath);
	}

	namespace services
	{
		std::expected<void, nefarius::utilities::Win32Error> CreateDriverService(PCSTR ServiceName, PCSTR DisplayName, PCSTR BinaryPath);

		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(PCSTR ServiceName);
	}
}
