// ReSharper disable CppRedundantQualifier
#include "pch.h"

using namespace nefarius::utilities::guards;
using namespace nefarius::utilities;

namespace
{
	// Helper function to build a multi-string from a vector<wstring>
	std::vector<wchar_t> BuildMultiString(const std::vector<std::wstring>& data)
	{
		// Special case of the empty multi-string
		if (data.empty())
		{
			// Build a vector containing just two NULs
			return std::vector(2, L'\0');
		}

		// Get the total length in wchar_ts of the multi-string
		size_t totalLen = 0;
		for (const auto& s : data)
		{
			// Add one to current string's length for the terminating NUL
			totalLen += (s.length() + 1);
		}

		// Add one for the last NUL terminator (making the whole structure double-NUL terminated)
		totalLen++;

		// Allocate a buffer to store the multi-string
		std::vector<wchar_t> multiString;
		multiString.reserve(totalLen);

		// Copy the single strings into the multi-string
		for (const auto& s : data)
		{
			multiString.insert(multiString.end(), s.begin(), s.end());

			// Don't forget to NUL-terminate the current string
			multiString.push_back(L'\0');
		}

		// Add the last NUL-terminator
		multiString.push_back(L'\0');

		return multiString;
	}
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::AddDeviceClassFilter(const GUID* ClassGuid,
                                                                       const StringType& FilterName,
                                                                       DeviceClassFilterPosition Position)
{
	const auto filterName = ConvertToWide(FilterName);

	HKEYHandleGuard key(SetupDiOpenClassRegKey(ClassGuid, KEY_ALL_ACCESS));

	if (key.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiOpenClassRegKey"));
	}

	LPCWSTR filterValue = (Position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
	DWORD type, size;
	std::vector<std::wstring> filters;

	auto status = RegQueryValueExW(
		key.get(),
		filterValue,
		nullptr,
		&type,
		nullptr,
		&size
	);

	//
	// Value exists already, read it with returned buffer size
	// 
	if (status == ERROR_SUCCESS)
	{
		std::vector<wchar_t> temp(size / sizeof(wchar_t));

		status = RegQueryValueExW(
			key.get(),
			filterValue,
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(temp.data()),
			&size
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegQueryValueExW"));
		}

		size_t index = 0;
		size_t len = wcslen(temp.data());
		while (len > 0)
		{
			filters.emplace_back(&temp[index]);
			index += len + 1;
			len = wcslen(&temp[index]);
		}

		//
		// Filter not there yet, add
		// 
		if (std::ranges::find(filters, filterName) == filters.end())
		{
			filters.emplace_back(filterName);
		}

		const std::vector<wchar_t> multiString = ::BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key.get(),
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(multiString.data()),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegSetValueExW"));
		}

		return {};
	}
	//
	// Value doesn't exist, create and populate
	// 
	if (status == ERROR_FILE_NOT_FOUND)
	{
		filters.emplace_back(filterName);

		const std::vector<wchar_t> multiString = ::BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key.get(),
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(multiString.data()),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegSetValueExW"));
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::RemoveDeviceClassFilter(
	const GUID* ClassGuid, const StringType& FilterName,
	DeviceClassFilterPosition Position)
{
	const std::wstring filterName = ConvertToWide(FilterName);

	HKEYHandleGuard key(SetupDiOpenClassRegKey(ClassGuid, KEY_ALL_ACCESS));

	if (key.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiOpenClassRegKey"));
	}

	LPCWSTR filterValue = (Position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
	DWORD type, size;

	auto status = RegQueryValueExW(
		key.get(),
		filterValue,
		nullptr,
		&type,
		nullptr,
		&size
	);

	//
	// Value exists already, read it with returned buffer size
	// 
	if (status == ERROR_SUCCESS)
	{
		std::vector<std::wstring> filters;
		std::vector<wchar_t> temp(size / sizeof(wchar_t));

		status = RegQueryValueExW(
			key.get(),
			filterValue,
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(temp.data()),
			&size
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegQueryValueExW"));
		}

		//
		// Remove value, if found
		//
		size_t index = 0;
		size_t len = wcslen(temp.data());
		while (len > 0)
		{
			if (filterName != &temp[index])
			{
				filters.emplace_back(&temp[index]);
			}
			index += len + 1;
			len = wcslen(&temp[index]);
		}

		const std::vector<wchar_t> multiString = ::BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key.get(),
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(multiString.data()),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegSetValueExW"));
		}

		return {};
	}
	//
	// Value doesn't exist, return
	// 
	if (status == ERROR_FILE_NOT_FOUND)
	{
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::expected<bool, Win32Error> nefarius::devcon::HasDeviceClassFilter(const GUID* ClassGuid,
                                                                       const StringType& FilterName,
                                                                       DeviceClassFilterPosition Position)
{
	const std::wstring filterName = ConvertToWide(FilterName);

	HKEYHandleGuard key(SetupDiOpenClassRegKey(ClassGuid, KEY_READ));

	if (key.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiOpenClassRegKey"));
	}

	LPCWSTR filterValue = (Position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
	DWORD type, size;
	std::vector<std::wstring> filters;

	auto status = RegQueryValueExW(
		key.get(),
		filterValue,
		nullptr,
		&type,
		nullptr,
		&size
	);

	//
	// Value exists already, read it with returned buffer size
	// 
	if (status == ERROR_SUCCESS)
	{
		std::vector<wchar_t> temp(size / sizeof(wchar_t));

		status = RegQueryValueExW(
			key.get(),
			filterValue,
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(temp.data()),
			&size
		);

		if (status != ERROR_SUCCESS)
		{
			return std::unexpected(Win32Error(status, "RegQueryValueExW"));
		}

		//
		// Enumerate values
		//
		size_t index = 0;
		size_t len = wcslen(temp.data());
		while (len > 0)
		{
			if (filterName == &temp[index])
			{
				return true;
			}
			index += len + 1;
			len = wcslen(&temp[index]);
		}

		return false;
	}
	//
	// Value doesn't exist, return
	// 
	if (status == ERROR_FILE_NOT_FOUND)
	{
		return false;
	}

	return std::unexpected(Win32Error());
}
