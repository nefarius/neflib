// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::winapi
{
	bool GUIDFromString(const std::string& input, GUID* guid);

	std::expected<bool, nefarius::utilities::Win32Error> IsAppRunningAsAdminMode();

	std::expected<void, nefarius::utilities::Win32Error> AdjustProcessPrivileges();
}
