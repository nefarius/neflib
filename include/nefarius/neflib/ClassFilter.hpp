#pragma once


namespace nefarius::devcon
{
	/**
	 * Specifies the attaching position of a class filter driver service.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	07.08.2024
	 */
	enum class DeviceClassFilterPosition
	{
		///< Upper filters
		Upper,
		///< Lower filters
		Lower
	};

	std::expected<void, utilities::Win32Error> AddDeviceClassFilter(const GUID* classGuid,
	                                                                const std::wstring& filterName,
	                                                                DeviceClassFilterPosition position);

	std::expected<void, utilities::Win32Error> RemoveDeviceClassFilter(const GUID* classGuid,
	                                                                   const std::wstring& filterName,
	                                                                   DeviceClassFilterPosition position);

	std::expected<bool, utilities::Win32Error> HasDeviceClassFilter(const GUID* classGuid,
	                                                                const std::wstring& filterName,
	                                                                DeviceClassFilterPosition position);
}
