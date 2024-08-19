// ReSharper disable CppRedundantQualifier
#pragma once


#include <nefarius/neflib/Win32Error.hpp>

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

	template <typename StringType>
	std::expected<void, nefarius::utilities::Win32Error> AddDeviceClassFilter(const GUID* ClassGuid,
	                                                                          const StringType& FilterName,
	                                                                          DeviceClassFilterPosition Position);

	template <typename StringType>
	std::expected<void, nefarius::utilities::Win32Error> RemoveDeviceClassFilter(const GUID* ClassGuid,
		const StringType& FilterName,
		DeviceClassFilterPosition Position);

	template <typename StringType>
	std::expected<bool, nefarius::utilities::Win32Error> HasDeviceClassFilter(const GUID* ClassGuid,
	                                                                          const StringType& FilterName,
	                                                                          DeviceClassFilterPosition Position);
}
