// ReSharper disable CppRedundantQualifier
// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <chrono>

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

		//
		// Stops the service (waiting up to StopTimeout for SERVICE_STOPPED, if it isn't already)
		// before deleting it, so callers don't have to remember to do this themselves; a service
		// still running when DeleteService is called merely gets marked "pending deletion" until
		// the last handle to it closes, which silently leaves the old driver resident. Class filter
		// drivers generally never advertise SERVICE_ACCEPT_STOP (no unload routine) and can only be
		// unloaded by the PnP manager tearing down their device stacks; that case is not treated as
		// failure and does not, by itself, set RebootRequired (whether a reboot is actually needed
		// depends on the affected devices being reset, which the caller drives separately). To get
		// an outright removal rather than a lingering "marked for deletion" registration, restart
		// the bound devices and wait for SERVICE_STOPPED (e.g. via WaitForServiceState) first.
		// 
		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverService(
			const StringType& ServiceName, std::chrono::milliseconds StopTimeout = std::chrono::seconds(10),
			bool* RebootRequired = nullptr);

		template <nefarius::utilities::string_type StringType>
		std::expected<SERVICE_STATUS_PROCESS, nefarius::utilities::Win32Error> GetServiceStatus(
			const StringType& ServiceName);

		//
		// Polls a service's status until it reaches DesiredState or Timeout elapses, whichever
		// comes first. Always returns the last observed status rather than failing on timeout, so
		// a caller that just installed/started a driver can distinguish "not there at all"
		// (Win32Error, e.g. ERROR_SERVICE_DOES_NOT_EXIST) from "present but not (yet) in the
		// desired state" (a returned status whose dwCurrentState != DesiredState), instead of a
		// naive immediate probe mistaking the latter for driver failure.
		// 
		template <nefarius::utilities::string_type StringType>
		std::expected<SERVICE_STATUS_PROCESS, nefarius::utilities::Win32Error> WaitForServiceState(
			const StringType& ServiceName, DWORD DesiredState,
			std::chrono::milliseconds Timeout = std::chrono::seconds(10));

		template
		std::expected<SERVICE_STATUS_PROCESS, nefarius::utilities::Win32Error> nefarius::winapi::services::
		WaitForServiceState(const std::wstring& ServiceName, DWORD DesiredState, std::chrono::milliseconds Timeout);

		template
		std::expected<SERVICE_STATUS_PROCESS, nefarius::utilities::Win32Error> nefarius::winapi::services::
		WaitForServiceState(const std::string& ServiceName, DWORD DesiredState, std::chrono::milliseconds Timeout);

		//
		// Wraps DeleteDriverService in a bounded retry loop, absorbing the brief window after a
		// filter driver's last bound device has been detached/restarted where the kernel has not
		// yet fully released the driver image; without this, a caller that removes the class
		// filter and immediately deletes the service can observe a spurious transient failure.
		// ERROR_SERVICE_DOES_NOT_EXIST is treated as success (already gone). Existing
		// DeleteDriverService is untouched, so --remove-driver-service's behavior is unaffected.
		// 
		template <nefarius::utilities::string_type StringType>
		std::expected<void, nefarius::utilities::Win32Error> DeleteDriverServiceWithRetry(
			const StringType& ServiceName, std::chrono::milliseconds StopTimeout = std::chrono::seconds(10),
			std::chrono::milliseconds RetryTimeout = std::chrono::seconds(5), bool* RebootRequired = nullptr);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::services::
		DeleteDriverServiceWithRetry(const std::wstring& ServiceName, std::chrono::milliseconds StopTimeout,
			std::chrono::milliseconds RetryTimeout, bool* RebootRequired);

		template
		std::expected<void, nefarius::utilities::Win32Error> nefarius::winapi::services::
		DeleteDriverServiceWithRetry(const std::string& ServiceName, std::chrono::milliseconds StopTimeout,
			std::chrono::milliseconds RetryTimeout, bool* RebootRequired);
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
