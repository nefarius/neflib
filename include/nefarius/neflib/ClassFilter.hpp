// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/AnyString.hpp>
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

	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> AddDeviceClassFilter(const GUID* ClassGuid,
	                                                                          const StringType& FilterName,
	                                                                          DeviceClassFilterPosition Position);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::AddDeviceClassFilter(const GUID* ClassGuid,
		const std::wstring& FilterName,
		DeviceClassFilterPosition Position);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::AddDeviceClassFilter(const GUID* ClassGuid,
		const std::string& FilterName,
		DeviceClassFilterPosition Position);

	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> RemoveDeviceClassFilter(const GUID* ClassGuid,
		const StringType& FilterName,
		DeviceClassFilterPosition Position);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::RemoveDeviceClassFilter(
		const GUID* ClassGuid, const std::wstring& FilterName,
		DeviceClassFilterPosition Position);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::RemoveDeviceClassFilter(
		const GUID* ClassGuid, const std::string& FilterName,
		DeviceClassFilterPosition Position);

	template <nefarius::utilities::string_type StringType>
	std::expected<bool, nefarius::utilities::Win32Error> HasDeviceClassFilter(const GUID* ClassGuid,
	                                                                          const StringType& FilterName,
	                                                                          DeviceClassFilterPosition Position);

	template
	std::expected<bool, nefarius::utilities::Win32Error> nefarius::devcon::HasDeviceClassFilter(const GUID* ClassGuid,
		const std::wstring& FilterName,
		DeviceClassFilterPosition Position);

	template
	std::expected<bool, nefarius::utilities::Win32Error> nefarius::devcon::HasDeviceClassFilter(const GUID* ClassGuid,
		const std::string& FilterName,
		DeviceClassFilterPosition Position);
}
