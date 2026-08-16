// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;


template
std::expected<void, Win32Error> nefarius::winapi::services::CreateDriverService(const std::wstring& ServiceName,
	const std::wstring& DisplayName,
	const std::wstring& BinaryPath);

template
std::expected<void, Win32Error> nefarius::winapi::services::CreateDriverService(const std::string& ServiceName,
	const std::string& DisplayName,
	const std::string& BinaryPath);

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::winapi::services::CreateDriverService(const StringType& ServiceName,
	const StringType& DisplayName,
	const StringType& BinaryPath)
{
	const auto serviceName = ConvertToNarrow(ServiceName);
	const auto displayName = ConvertToNarrow(DisplayName);
	const auto binaryPath = ConvertToNarrow(BinaryPath);

	SC_HANDLE hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager)
	{
		return std::unexpected(Win32Error("OpenSCManagerA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hSCManager); }, hSCManager);

	SC_HANDLE hService = CreateServiceA(
		hSCManager,
		serviceName.c_str(),
		displayName.c_str(),
		SERVICE_START | DELETE | SERVICE_STOP,
		SERVICE_KERNEL_DRIVER,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_IGNORE,
		binaryPath.c_str(),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr
	);

	if (!hService)
	{
		return std::unexpected(Win32Error("CreateServiceA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	return {};
}

template
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverService(
	const std::wstring& ServiceName, std::chrono::milliseconds StopTimeout, bool* RebootRequired);

template
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverService(
	const std::string& ServiceName, std::chrono::milliseconds StopTimeout, bool* RebootRequired);

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverService(
	const StringType& ServiceName, std::chrono::milliseconds StopTimeout, bool* RebootRequired)
{
	const auto serviceName = ConvertToNarrow(ServiceName);

	SC_HANDLE hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager)
	{
		return std::unexpected(Win32Error("OpenSCManagerA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hSCManager); }, hSCManager);

	SC_HANDLE hService = OpenServiceA(
		hSCManager,
		serviceName.c_str(),
		SERVICE_START | DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS
	);

	if (!hService)
	{
		return std::unexpected(Win32Error("OpenServiceA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	SERVICE_STATUS_PROCESS status = {};
	DWORD bytesNeeded = 0;

	if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status),
	                         &bytesNeeded))
	{
		return std::unexpected(Win32Error("QueryServiceStatusEx"));
	}

	//
	// A service still running (or stopping) when DeleteService is called merely gets flagged for
	// deletion once its last handle closes; the .sys file stays locked/loaded until then. Stop it
	// and wait for SERVICE_STOPPED first so the caller can rely on the driver actually being gone.
	// Accumulated locally (rather than trusting the caller to have pre-initialized *RebootRequired)
	// and only written back once DeleteService has actually succeeded.
	// 
	bool rebootRequiredLocal = false;

	if (status.dwCurrentState != SERVICE_STOPPED)
	{
		const auto deadline = std::chrono::steady_clock::now() + StopTimeout;
		bool stopRequested = (status.dwCurrentState == SERVICE_STOP_PENDING);

		while (status.dwCurrentState != SERVICE_STOPPED)
		{
			if (!stopRequested)
			{
				SERVICE_STATUS controlStatus = {};

				if (ControlService(hService, SERVICE_CONTROL_STOP, &controlStatus))
				{
					stopRequested = true;
				}
				else
				{
					const DWORD stopError = GetLastError();

					if (stopError == ERROR_SERVICE_NOT_ACTIVE)
					{
						//
						// The service may have stopped on its own between the status query above
						// and this call; that is success, not failure.
						// 
						break;
					}

					if (stopError == ERROR_INVALID_SERVICE_CONTROL)
					{
						//
						// The driver never advertised SERVICE_ACCEPT_STOP (no unload routine), so
						// it can never be stopped live; waiting for SERVICE_STOPPED would just
						// spin until StopTimeout for nothing. Proceed to mark it for deletion
						// anyway instead of failing outright, and let the caller know a reboot is
						// still required for the removal to fully take effect.
						// 
						rebootRequiredLocal = true;
						break;
					}

					//
					// ERROR_SERVICE_CANNOT_ACCEPT_CTRL means the service is transiently unable to
					// accept a stop right now (e.g. still SERVICE_START_PENDING); fall through to
					// poll/retry below instead of failing outright. Any other error is fatal.
					// 
					if (stopError != ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
					{
						return std::unexpected(Win32Error(stopError, "ControlService"));
					}
				}
			}

			if (status.dwCurrentState == SERVICE_STOPPED)
			{
				break;
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				return std::unexpected(Win32Error(ERROR_SERVICE_REQUEST_TIMEOUT,
				                                  "Timed out waiting for service to stop"));
			}

			Sleep(std::clamp(status.dwWaitHint / 10, 50UL, 1000UL));

			if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status),
			                         sizeof(status), &bytesNeeded))
			{
				return std::unexpected(Win32Error("QueryServiceStatusEx"));
			}
		}
	}

	if (!DeleteService(hService))
	{
		return std::unexpected(Win32Error("DeleteService"));
	}

	if (RebootRequired)
	{
		*RebootRequired = rebootRequiredLocal;
	}

	return {};
}

template
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::GetServiceStatus(
	const std::string& ServiceName);

template
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::GetServiceStatus(
	const std::wstring& ServiceName);

template <nefarius::utilities::string_type StringType>
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::GetServiceStatus(
	const StringType& ServiceName)
{
	const auto serviceName = ConvertToWide(ServiceName);

	SC_HANDLE sch = nullptr;
	SC_HANDLE svc = nullptr;

	SCOPE_GUARD_CAPTURE({
	                    if (svc)
	                    CloseServiceHandle(svc);
	                    if (sch)
	                    CloseServiceHandle(sch);
	                    }, svc, sch);

	sch = OpenSCManagerW(
		nullptr,
		nullptr,
		SC_MANAGER_CONNECT
	);
	if (sch == nullptr)
	{
		return std::unexpected(Win32Error("OpenSCManagerW"));
	}

	svc = OpenServiceW(
		sch,
		serviceName.c_str(),
		SERVICE_QUERY_STATUS
	);
	if (svc == nullptr)
	{
		return std::unexpected(Win32Error("OpenServiceW"));
	}

	SERVICE_STATUS_PROCESS stat{};
	DWORD needed = 0;
	const BOOL ret = QueryServiceStatusEx(
		svc,
		SC_STATUS_PROCESS_INFO,
		(BYTE*)&stat,
		sizeof stat,
		&needed
	);
	if (ret == FALSE)
	{
		return std::unexpected(Win32Error("QueryServiceStatusEx"));
	}

	return stat;
}

template
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::WaitForServiceState(
	const std::wstring& ServiceName, DWORD DesiredState, std::chrono::milliseconds Timeout);

template
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::WaitForServiceState(
	const std::string& ServiceName, DWORD DesiredState, std::chrono::milliseconds Timeout);

template <nefarius::utilities::string_type StringType>
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::WaitForServiceState(
	const StringType& ServiceName, DWORD DesiredState, std::chrono::milliseconds Timeout)
{
	const auto serviceName = ConvertToWide(ServiceName);

	SC_HANDLE hSCManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

	if (!hSCManager)
	{
		return std::unexpected(Win32Error("OpenSCManagerW"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hSCManager); }, hSCManager);

	SC_HANDLE hService = OpenServiceW(hSCManager, serviceName.c_str(), SERVICE_QUERY_STATUS);

	if (!hService)
	{
		return std::unexpected(Win32Error("OpenServiceW"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	SERVICE_STATUS_PROCESS status = {};
	DWORD bytesNeeded = 0;

	if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status),
	                         &bytesNeeded))
	{
		return std::unexpected(Win32Error("QueryServiceStatusEx"));
	}

	const auto deadline = std::chrono::steady_clock::now() + Timeout;

	while (status.dwCurrentState != DesiredState)
	{
		auto now = std::chrono::steady_clock::now();

		if (now >= deadline)
		{
			break;
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
		const auto waitHint = std::clamp(status.dwWaitHint / 10, 50UL, 1000UL);
		Sleep(static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(waitHint, remaining.count())));

		now = std::chrono::steady_clock::now();

		if (now >= deadline)
		{
			break;
		}

		if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status),
		                         sizeof(status), &bytesNeeded))
		{
			return std::unexpected(Win32Error("QueryServiceStatusEx"));
		}
	}

	//
	// Deliberately returned even when dwCurrentState never reached DesiredState within Timeout;
	// the caller decides whether "present but not yet running" is acceptable (e.g. a demand-start
	// filter service with no bound device present) or should be reported as a warning.
	// 
	return status;
}

template
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverServiceWithRetry(
	const std::wstring& ServiceName, std::chrono::milliseconds StopTimeout, std::chrono::milliseconds RetryTimeout,
	bool* RebootRequired);

template
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverServiceWithRetry(
	const std::string& ServiceName, std::chrono::milliseconds StopTimeout, std::chrono::milliseconds RetryTimeout,
	bool* RebootRequired);

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverServiceWithRetry(
	const StringType& ServiceName, std::chrono::milliseconds StopTimeout, std::chrono::milliseconds RetryTimeout,
	bool* RebootRequired)
{
	if (RebootRequired)
	{
		*RebootRequired = false;
	}

	const auto deadline = std::chrono::steady_clock::now() + RetryTimeout;

	for (;;)
	{
		auto result = DeleteDriverService(ServiceName, StopTimeout, RebootRequired);

		if (result)
		{
			return {};
		}

		const DWORD errorCode = result.error().getErrorCode();

		//
		// A concurrent/previous attempt already deleted it; that is success, not failure, for an
		// idempotent "make sure it's gone" operation.
		// 
		if (errorCode == ERROR_SERVICE_DOES_NOT_EXIST)
		{
			return {};
		}

		//
		// These are the transient errors observed in the brief window after a filter driver's
		// last bound device has been detached/restarted, before the kernel has fully released the
		// driver image; retrying after a short backoff resolves them without a caller-visible
		// failure. Anything else (e.g. a genuine permissions problem) is returned immediately.
		// 
		const bool transient = (errorCode == ERROR_SERVICE_MARKED_FOR_DELETE) ||
			(errorCode == ERROR_SHARING_VIOLATION) ||
			(errorCode == ERROR_SERVICE_REQUEST_TIMEOUT) ||
			(errorCode == ERROR_SERVICE_CANNOT_ACCEPT_CTRL);

		const auto now = std::chrono::steady_clock::now();

		if (!transient || now >= deadline)
		{
			return std::unexpected(result.error());
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
		Sleep(static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(100, remaining.count())));

		//
		// Don't start another DeleteDriverService attempt (which can itself block for up to
		// StopTimeout) once the retry budget is already spent; return the transient error from
		// this attempt instead of overshooting RetryTimeout.
		// 
		if (std::chrono::steady_clock::now() >= deadline)
		{
			return std::unexpected(result.error());
		}
	}
}
