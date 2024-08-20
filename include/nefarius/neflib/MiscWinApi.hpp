// ReSharper disable CppRedundantQualifier
// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <nefarius/neflib/AnyString.hpp>
#include <nefarius/neflib/UniUtil.hpp>
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

		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> SetPrivilege(
			const StringType& Privilege, int Enable, HANDLE Process = GetCurrentProcess());

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::security::SetPrivilege(
			const std::wstring& Privilege, int Enable, HANDLE Process);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::security::SetPrivilege(
			const std::string& Privilege, int Enable, HANDLE Process);
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

		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> TakeFileOwnership(const StringType& FilePath);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::fs::TakeFileOwnership(
			const std::wstring& FilePath);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::fs::TakeFileOwnership(
			const std::string& FilePath);

		template <nefarius::utilities::string_type StringType>
		std::expected<Version, nefarius::utilities::Win32Error> GetProductVersionFromFile(const StringType& FilePath);

		template
		std::expected<nefarius::winapi::fs::Version, nefarius::utilities::Win32Error>
		nefarius::winapi::fs::GetProductVersionFromFile(
			const std::wstring& filePath);

		template
		std::expected<nefarius::winapi::fs::Version, nefarius::utilities::Win32Error>
		nefarius::winapi::fs::GetProductVersionFromFile(
			const std::string& filePath);

		template <nefarius::utilities::string_type StringType>
		std::expected<Version, nefarius::utilities::Win32Error> GetFileVersionFromFile(const StringType& FilePath);

		template
		std::expected<nefarius::winapi::fs::Version, nefarius::utilities::Win32Error> nefarius::winapi::fs::
		GetFileVersionFromFile(const std::wstring& FilePath);

		template
		std::expected<nefarius::winapi::fs::Version, nefarius::utilities::Win32Error> nefarius::winapi::fs::
		GetFileVersionFromFile(const std::string& FilePath);

		template <nefarius::utilities::string_type StringType>
		std::expected<bool, nefarius::utilities::Win32Error> DirectoryExists(const StringType& Path);

		template
		std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryExists(
			const std::wstring& Path);

		template
		std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryExists(
			const std::string& Path);

		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> DirectoryCreate(const StringType& Path);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryCreate(
			const std::wstring& Path);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryCreate(
			const std::string& Path);
	}

	namespace services
	{
		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> CreateDriverService(
			const StringType& ServiceName, const StringType& DisplayName, const StringType& BinaryPath);

		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(const StringType& ServiceName);

		template <nefarius::utilities::string_type StringType>
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

//
// Include stuff below here that can not be shipped pre-compiled
// 

#define NEFLIB_MISCWINAPI_IMPL_INCLUDED
#include <nefarius/neflib/MiscWinApi.Impl.hpp>
