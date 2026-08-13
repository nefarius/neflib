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
