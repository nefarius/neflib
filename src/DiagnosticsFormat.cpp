#include "pch.h"

#include <nefarius/neflib/DiagnosticsFormat.hpp>
#include <nefarius/neflib/UniUtil.hpp>
#include <nefarius/neflib/Win32Error.hpp>

using namespace nefarius::utilities;

const char* nefarius::devcon::ToString(RestartStrategy strategy) noexcept
{
	switch (strategy)
	{
	case RestartStrategy::UsbPortCycle:
		return "USB port cycle";
	case RestartStrategy::PropertyChange:
		return "property change";
	case RestartStrategy::RemoveAndReenumerate:
		return "remove and re-enumerate";
	case RestartStrategy::None:
	default:
		return "none";
	}
}

// Only the CM_PROB_* codes that are plausible for a device that just went through a restart
// ladder are named; anything else is reported as its raw numeric value so nothing is hidden.
std::string nefarius::devcon::ProblemCodeToString(ULONG problemCode)
{
	switch (problemCode)
	{
	case CM_PROB_NEED_RESTART:
		return "CM_PROB_NEED_RESTART";
	case CM_PROB_WILL_BE_REMOVED:
		return "CM_PROB_WILL_BE_REMOVED";
	case CM_PROB_MOVED:
		return "CM_PROB_MOVED";
	case CM_PROB_TOO_EARLY:
		return "CM_PROB_TOO_EARLY";
	case CM_PROB_NO_VALID_LOG_CONF:
		return "CM_PROB_NO_VALID_LOG_CONF";
	case CM_PROB_FAILED_INSTALL:
		return "CM_PROB_FAILED_INSTALL";
	case CM_PROB_HARDWARE_DISABLED:
		return "CM_PROB_HARDWARE_DISABLED";
	case CM_PROB_NOT_CONFIGURED:
		return "CM_PROB_NOT_CONFIGURED";
	case CM_PROB_FAILED_ADD:
		return "CM_PROB_FAILED_ADD";
	case CM_PROB_DISABLED_SERVICE:
		return "CM_PROB_DISABLED_SERVICE";
	case CM_PROB_DEVICE_NOT_THERE:
		return "CM_PROB_DEVICE_NOT_THERE";
	case CM_PROB_REGISTRY:
		return "CM_PROB_REGISTRY";
	case CM_PROB_PHANTOM:
		return "CM_PROB_PHANTOM";
	default:
		return std::to_string(problemCode);
	}
}

std::string nefarius::devcon::DescribeFinalDevNodeState(const DeviceRestartResult& result)
{
	if (!result.DevicePresent)
	{
		return " (device is no longer present)";
	}

	if (!result.FinalStatusValid)
	{
		return std::format(" (final devnode status could not be queried, error 0x{:X})",
		                   static_cast<unsigned long>(result.FinalStatusError));
	}

	std::string detail = " (final devnode state: started=";
	detail += result.FinalStarted ? "true" : "false";
	detail += result.FinalHasProblem
		          ? (", problem=" + ProblemCodeToString(result.FinalProblemCode))
		          : std::string(", no problem code");
	detail += ")";
	return detail;
}

std::string nefarius::devcon::DescribeDeviceRestartResult(const DeviceRestartResult& result)
{
	const std::wstring displayName = result.FriendlyName.empty() ? result.InstanceId : result.FriendlyName;
	const std::string displayNameA = ConvertWideToANSI(displayName);

	if (result.Succeeded)
	{
		return std::format("Restarted device \"{}\" via {}", displayNameA, ToString(result.Strategy));
	}

	if (!result.DevicePresent)
	{
		return std::format("Device \"{}\" is no longer present; nothing to restart", displayNameA);
	}

	const std::string finalStateDetail = DescribeFinalDevNodeState(result);

	if (result.TimedOut)
	{
		return std::format("Timed out attempting to restart device \"{}\" (last attempted: {}){}",
		                   displayNameA, ToString(result.LastAttempted), finalStateDetail);
	}

	if (!result.VetoName.empty())
	{
		return std::format("Could not restart device \"{}\", blocked by \"{}\" ({}){}",
		                   displayNameA, ConvertWideToANSI(result.VetoName),
		                   Win32Error(result.LastError).getErrorMessageA(), finalStateDetail);
	}

	return std::format("Could not restart device \"{}\", last attempted: {}, error: {}{}",
	                   displayNameA, ToString(result.LastAttempted),
	                   Win32Error(result.LastError).getErrorMessageA(), finalStateDetail);
}

std::string nefarius::devcon::DescribeDetachResult(const DetachResult& result)
{
	const std::wstring displayName = result.FriendlyName.empty() ? result.InstanceId : result.FriendlyName;
	const std::string displayNameA = ConvertWideToANSI(displayName);

	if (result.Succeeded)
	{
		return std::format("Detached device \"{}\"", displayNameA);
	}

	if (result.TimedOut)
	{
		return std::format("Timed out attempting to detach device \"{}\"", displayNameA);
	}

	if (!result.VetoName.empty())
	{
		return std::format("Could not detach device \"{}\", blocked by \"{}\" ({})",
		                   displayNameA, ConvertWideToANSI(result.VetoName),
		                   Win32Error(result.LastError).getErrorMessageA());
	}

	return std::format("Could not detach device \"{}\", error: {}",
	                   displayNameA, Win32Error(result.LastError).getErrorMessageA());
}

std::string nefarius::devcon::DescribeReenumerateResult(const ReenumerateResult& result)
{
	const std::string instanceIdA = ConvertWideToANSI(result.InstanceId);

	if (result.Succeeded)
	{
		return std::format("Re-enumerated devnode \"{}\"", instanceIdA);
	}

	if (result.TimedOut)
	{
		return std::format("Timed out re-enumerating devnode \"{}\"", instanceIdA);
	}

	return std::format("Failed to re-enumerate devnode \"{}\", error: {}",
	                   instanceIdA, Win32Error(result.LastError).getErrorMessageA());
}
