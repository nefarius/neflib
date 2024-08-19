// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;

template <typename StringType>
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

template <typename StringType>
std::expected<void, Win32Error> nefarius::winapi::services::DeleteDriverService(const StringType& ServiceName)
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
		SERVICE_START | DELETE | SERVICE_STOP
	);

	if (!hService)
	{
		return std::unexpected(Win32Error("OpenServiceA"));
	}

	SCOPE_GUARD_CAPTURE({ CloseServiceHandle(hService); }, hService);

	if (!DeleteService(hService))
	{
		return std::unexpected(Win32Error("DeleteService"));
	}

	return {};
}

template <typename StringType>
std::expected<SERVICE_STATUS_PROCESS, Win32Error> nefarius::winapi::services::GetServiceStatus(
	const StringType& ServiceName)
{
	const auto serviceName = ConvertToNarrow(ServiceName);

	SC_HANDLE sch = nullptr;
	SC_HANDLE svc = nullptr;

	SCOPE_GUARD_CAPTURE({
	                    if (svc)
	                    CloseServiceHandle(svc);
	                    if (sch)
	                    CloseServiceHandle(sch);
	                    }, svc, sch);

	sch = OpenSCManagerA(
		nullptr,
		nullptr,
		STANDARD_RIGHTS_REQUIRED | SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE
	);
	if (sch == nullptr)
	{
		return std::unexpected(Win32Error("OpenSCManagerA"));
	}

	svc = OpenServiceA(
		sch,
		serviceName.c_str(),
		SERVICE_QUERY_STATUS
	);
	if (svc == nullptr)
	{
		return std::unexpected(Win32Error("OpenServiceA"));
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
