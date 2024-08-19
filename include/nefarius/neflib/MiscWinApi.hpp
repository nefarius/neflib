// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::winapi
{
	std::expected<GUID, nefarius::utilities::Win32Error> GUIDFromString(const std::string& input);

	SYSTEM_INFO SafeGetNativeSystemInfo();

	std::expected<DWORD, nefarius::utilities::Win32Error> GetParentProcessID(DWORD ProcessId);

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
			return std::format("{}.{}.{}.{}", version.Major, version.Minor, version.Build, version.Private);
		}

		inline std::wstring to_wstring(Version const& version)
		{
			return nefarius::utilities::ConvertAnsiToWide(to_string(version));
		}

		template <typename StringType>
		std::expected<void, nefarius::utilities::Win32Error> TakeFileOwnership(const StringType& FilePath);

		template <typename StringType>
		std::expected<Version, nefarius::utilities::Win32Error> GetProductVersionFromFile(const StringType& FilePath);

		template <typename StringType>
		std::expected<Version, nefarius::utilities::Win32Error> GetFileVersionFromFile(const StringType& FilePath);
	}

	namespace services
	{
		template <typename StringType>
		std::expected<void, nefarius::utilities::Win32Error> CreateDriverService(
			const StringType& ServiceName, const StringType& DisplayName, const StringType& BinaryPath);

		template <typename StringType>
		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(const StringType& ServiceName);

		template <typename StringType>
		std::expected<SERVICE_STATUS_PROCESS, nefarius::utilities::Win32Error> GetServiceStatus(
			const StringType& ServiceName);
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
