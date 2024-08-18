// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::winapi
{
	std::expected<GUID, nefarius::utilities::Win32Error> GUIDFromString(const std::string& input);

	namespace security
	{
		std::expected<bool, nefarius::utilities::Win32Error> IsAppRunningAsAdminMode();

		std::expected<void, nefarius::utilities::Win32Error> AdjustProcessPrivileges();

		std::expected<PSID, nefarius::utilities::Win32Error> GetLogonSID(HANDLE hToken);

		std::expected<void, nefarius::utilities::Win32Error> SetPrivilege(
			LPCWSTR privilege, int enable, HANDLE process = GetCurrentProcess());
	}

	namespace fs
	{
		struct Version
		{
			union
			{
				struct // NOLINT(clang-diagnostic-nested-anon-types)
				{
					uint16_t Major;
					uint16_t Minor;
					uint16_t Build;
					uint16_t Private;
				};

				uint64_t Value;
			};
		};

		inline std::string to_string(Version const& version)
		{
			std::ostringstream ss;
			ss << version.Major << "." << version.Minor << "." << version.Build << "." << version.Private;
			return std::move(ss).str();
		}

		std::expected<void, nefarius::utilities::Win32Error> TakeFileOwnership(LPCWSTR file);

		std::expected<Version, nefarius::utilities::Win32Error> GetProductVersionFromFile(const std::string& filePath);

		std::expected<Version, nefarius::utilities::Win32Error> GetFileVersionFromFile(const std::string& filePath);
	}

	namespace services
	{
		std::expected<void, nefarius::utilities::Win32Error> CreateDriverService(
			PCSTR ServiceName, PCSTR DisplayName, PCSTR BinaryPath);

		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(PCSTR ServiceName);
	}

	namespace cli
	{
		struct CliArgsResult
		{
			std::vector<std::string> Arguments;

			std::vector<const char*> AsArgv(int* argc);
		};

		std::expected<nefarius::winapi::cli::CliArgsResult, nefarius::utilities::Win32Error> GetCommandLineArgs();
	}
}
