#include "pch.h"

#include <nefarius/neflib/Devcon.hpp>


using namespace nefarius::utilities;

namespace
{
	std::expected<wil::unique_hlocal_ptr<uint8_t[]>, Win32Error> GetDeviceRegistryProperty(
		_In_ HDEVINFO DeviceInfoSet,
		_In_ PSP_DEVINFO_DATA DeviceInfoData,
		_In_ DWORD Property,
		_Out_opt_ PDWORD PropertyRegDataType,
		_Out_opt_ PDWORD BufferSize
	)
	{
		DWORD sizeRequired = 0;

		//
		// Query required size
		// 
		(void)SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
		                                       DeviceInfoData,
		                                       Property,
		                                       PropertyRegDataType,
		                                       NULL,
		                                       0,
		                                       &sizeRequired);

		DWORD win32Error = GetLastError();

		//
		// Property doesn't exist
		// 
		if (win32Error == ERROR_INVALID_DATA)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "SetupDiGetDeviceRegistryProperty"));
		}

		//
		// Unexpected status other than required size
		// 
		if (win32Error != ERROR_INSUFFICIENT_BUFFER)
		{
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
		}

		auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(sizeRequired);

		//
		// Query property value
		// 
		if (!SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
		                                      DeviceInfoData,
		                                      Property,
		                                      PropertyRegDataType,
		                                      buffer.get(),
		                                      sizeRequired,
		                                      &sizeRequired))
		{
			win32Error = GetLastError();
			buffer.release();
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
		}

		if (BufferSize)
			*BufferSize = sizeRequired;

		return buffer;
	}
}


std::expected<void, Win32Error> nefarius::devcon::Create(const std::wstring& className, const GUID* classGuid,
                                                         const WideMultiStringArray& hardwareId)
{
	guards::HDEVINFOHandleGuard hDevInfo(SetupDiCreateDeviceInfoList(classGuid, nullptr));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiCreateDeviceInfoList"));
	}

	SP_DEVINFO_DATA deviceInfoData;
	deviceInfoData.cbSize = sizeof(deviceInfoData);

	//
	// Create new device node
	// 
	if (!SetupDiCreateDeviceInfoW(
		hDevInfo.get(),
		className.c_str(),
		classGuid,
		nullptr,
		nullptr,
		DICD_GENERATE_ID,
		&deviceInfoData
	))
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiCreateDeviceInfoW"));
	}

	//
	// Add the HardwareID to the Device's HardwareID property.
	//
	if (!SetupDiSetDeviceRegistryPropertyW(
		hDevInfo.get(),
		&deviceInfoData,
		SPDRP_HARDWAREID,
		hardwareId.data(),
		static_cast<DWORD>(hardwareId.size())
	))
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiSetDeviceRegistryPropertyW"));
	}

	//
	// Transform the registry element into an actual device node in the PnP HW tree
	//
	if (!SetupDiCallClassInstaller(
		DIF_REGISTERDEVICE,
		hDevInfo.get(),
		&deviceInfoData
	))
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiCallClassInstaller"));
	}

	return {};
}

std::expected<void, Win32Error> nefarius::devcon::Update(const std::wstring& hardwareId,
                                                         const std::wstring& fullInfPath,
                                                         bool* rebootRequired, bool force)
{
	Newdev newdev;
	DWORD flags = 0;
	BOOL reboot = FALSE;
	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	if (force)
		flags |= INSTALLFLAG_FORCE;

	switch (newdev.CallFunction(
		newdev.fpUpdateDriverForPlugAndPlayDevicesW,
		nullptr,
		hardwareId.c_str(),
		normalisedInfPath,
		flags,
		&reboot
	))
	{
	case FunctionCallResult::NotAvailable:
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
	case FunctionCallResult::Success:
		if (rebootRequired)
			*rebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> nefarius::devcon::InstallDriver(const std::wstring& fullInfPath,
                                                                bool* rebootRequired)
{
	Newdev newdev;
	BOOL reboot;
	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	switch (newdev.CallFunction(
		newdev.fpDiInstallDriverW,
		nullptr,
		normalisedInfPath,
		DIIRFLAG_FORCE_INF,
		&reboot
	))
	{
	case FunctionCallResult::NotAvailable:
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
	case FunctionCallResult::Success:
		if (rebootRequired)
			*rebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> nefarius::devcon::UninstallDriver(const std::wstring& fullInfPath,
                                                                  bool* rebootRequired)
{
	Newdev newdev;
	BOOL reboot;
	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	switch (newdev.CallFunction(
		newdev.fpDiUninstallDriverW,
		nullptr,
		normalisedInfPath,
		0,
		&reboot
	))
	{
	case FunctionCallResult::NotAvailable:
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
	case FunctionCallResult::Success:
		if (rebootRequired)
			*rebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}
