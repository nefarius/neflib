#pragma once

#include <cwchar>
#include <string_view>

#include <nefarius/neflib/DeviceRestart.hpp>

namespace nefarius::devcon::restart_detail
{
	inline bool IsAcpiDeviceInstanceId(const std::wstring_view instanceId) noexcept
	{
		constexpr std::wstring_view acpiPrefix = L"ACPI\\";

		return instanceId.size() >= acpiPrefix.size()
			&& _wcsnicmp(instanceId.data(), acpiPrefix.data(), acpiPrefix.size()) == 0;
	}

	inline DeviceRestartSkipReason GetDeviceRestartSkipReason(
		const std::wstring_view instanceId, const DeviceRestartOptions& options) noexcept
	{
		return !options.AllowAcpiDeviceRestart && IsAcpiDeviceInstanceId(instanceId)
			       ? DeviceRestartSkipReason::AcpiDevice
			       : DeviceRestartSkipReason::None;
	}
}
