// ReSharper disable CppRedundantQualifier
#pragma once

#include <chrono>

#include <nefarius/neflib/AnyString.hpp>
#include <nefarius/neflib/Win32Error.hpp>
#include <nefarius/neflib/ClassFilter.hpp>

namespace nefarius::devcon
{
	/**
	 * The mechanism that was used (or attempted) to bring a device back online without a reboot.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	enum class RestartStrategy
	{
		///< No strategy succeeded (or none was attempted)
		None,
		///< The USB hub port the device is attached to was power-cycled
		UsbPortCycle,
		///< A DIF_PROPERTYCHANGE/DICS_PROPCHANGE was sent to the device
		PropertyChange,
		///< The device sub-tree was removed and the parent was re-enumerated
		RemoveAndReenumerate
	};

	/**
	 * Tuning knobs for RestartDeviceInstance.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	struct DeviceRestartOptions
	{
		///< Upper bound each individual strategy attempt may take before it is abandoned
		std::chrono::milliseconds PerDeviceTimeout{std::chrono::seconds(10)};
		///< Allow attempting a USB hub port cycle
		bool AllowUsbPortCycle = true;
		///< Allow attempting a DIF_PROPERTYCHANGE restart
		bool AllowPropertyChange = true;
		///< Allow attempting a query-remove + re-enumerate of the parent devnode
		bool AllowRemoveAndReenumerate = true;
	};

	/**
	 * Outcome of a single RestartDeviceInstance call.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	struct DeviceRestartResult
	{
		///< Instance ID of the device this result refers to
		std::wstring InstanceId;
		///< Friendly name/description, if it could be resolved, for logging purposes
		std::wstring FriendlyName;
		///< The strategy that succeeded; RestartStrategy::None if every attempt failed
		RestartStrategy Strategy = RestartStrategy::None;
		///< True if the device could be brought back online without a reboot
		bool Succeeded = false;
		///< True if the last attempted strategy hit PerDeviceTimeout
		bool TimedOut = false;
		///< True if Windows reported DI_NEEDRESTART/DI_NEEDREBOOT for this device regardless of Succeeded
		bool RebootRequired = false;
		///< Win32 error code of the last failed attempt, ERROR_SUCCESS if Succeeded
		DWORD LastError = ERROR_SUCCESS;
		///< Populated with the blocking driver/application name if a query-remove was vetoed
		std::wstring VetoName;
		///< Populated alongside VetoName with the veto reason reported by the PnP manager
		PNP_VETO_TYPE VetoType = static_cast<PNP_VETO_TYPE>(0);
	};

	/**
	 * A single class filter registration a given INF's [DefaultInstall]/[DefaultUninstall]
	 * section would add or remove.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	struct InfClassFilterTarget
	{
		///< Device setup class this filter registration targets
		GUID ClassGuid = {};
		///< Whether the INF registers/deregisters an upper or a lower filter
		nefarius::devcon::DeviceClassFilterPosition Position = nefarius::devcon::DeviceClassFilterPosition::Upper;
		///< Name of the filter driver service being (de-)registered
		std::wstring ServiceName;
	};

	/**
	 * Parses an INF's [DefaultInstall]/[DefaultUninstall] AddReg/DelReg directives and returns
	 * every UpperFilters/LowerFilters class filter registration it would apply. Never fails just
	 * because an INF has no filter directives; the returned vector is simply empty in that case.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	FullInfPath	Full pathname to the INF file.
	 *
	 * @returns	A std::expected&lt;std::vector&lt;InfClassFilterTarget&gt;,nefarius::utilities::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<std::vector<InfClassFilterTarget>, nefarius::utilities::Win32Error> GetInfClassFilterTargets(
		const StringType& FullInfPath);

	template
	std::expected<std::vector<InfClassFilterTarget>, nefarius::utilities::Win32Error>
	nefarius::devcon::GetInfClassFilterTargets(const std::wstring& FullInfPath);

	template
	std::expected<std::vector<InfClassFilterTarget>, nefarius::utilities::Win32Error>
	nefarius::devcon::GetInfClassFilterTargets(const std::string& FullInfPath);

	/**
	 * Enumerates the instance IDs of every device currently belonging to a device setup class.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	ClassGuid  	Device class GUID to enumerate.
	 * @param 	PresentOnly	(Optional) True to only return devices currently present in the system.
	 *
	 * @returns	A std::expected&lt;std::vector&lt;std::wstring&gt;,nefarius::utilities::Win32Error&gt;
	 */
	std::expected<std::vector<std::wstring>, nefarius::utilities::Win32Error> ListDeviceInstancesByClass(
		const GUID* ClassGuid, bool PresentOnly = true);

	/**
	 * Power-cycles the USB hub port a device is attached to, forcing it to restart, even if the
	 * device itself refuses to be closed/reopened by any driver in its stack. Fails with
	 * ERROR_NOT_SUPPORTED if no USB hub ancestor could be found for the device.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	InstanceId	Instance ID of the device to restart.
	 *
	 * @returns	A std::expected&lt;void,nefarius::utilities::Win32Error&gt;
	 */
	std::expected<void, nefarius::utilities::Win32Error> CycleUsbPortOfDevice(const std::wstring& InstanceId);

	/**
	 * Outcome of a single DetachDeviceInstance call.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	struct DetachResult
	{
		///< Instance ID of the device this result refers to
		std::wstring InstanceId;
		///< Friendly name/description, if it could be resolved, for logging purposes
		std::wstring FriendlyName;
		///< Instance ID of the device's parent devnode; only populated if Succeeded, pass this to
		///< ReenumerateParentDevNode later to make Windows re-discover the detached device
		std::wstring ParentInstanceId;
		///< True if the device sub-tree could be removed (its driver is no longer loaded/locking files)
		bool Succeeded = false;
		///< True if the attempt hit the given timeout
		bool TimedOut = false;
		///< Win32 error code of the failed attempt, ERROR_SUCCESS if Succeeded
		DWORD LastError = ERROR_SUCCESS;
		///< Populated with the blocking driver/application name if the removal was vetoed
		std::wstring VetoName;
		///< Populated alongside VetoName with the veto reason reported by the PnP manager
		PNP_VETO_TYPE VetoType = static_cast<PNP_VETO_TYPE>(0);
	};

	/**
	 * Outcome of a single ReenumerateParentDevNode call.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 */
	struct ReenumerateResult
	{
		///< Instance ID of the (parent) devnode this result refers to
		std::wstring InstanceId;
		///< True if the devnode could be located and re-enumerated
		bool Succeeded = false;
		///< True if the attempt hit the given timeout
		bool TimedOut = false;
		///< Win32 error code of the failed attempt, ERROR_SUCCESS if Succeeded
		DWORD LastError = ERROR_SUCCESS;
	};

	/**
	 * Enumerates the instance IDs of every device currently bound to a given driver service,
	 * across all device setup classes. Intended for finding devices that would be affected by
	 * stopping/deleting/upgrading a driver service, regardless of which class(es) it serves.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	ServiceName	Name of the driver service to match against DEVPKEY_Device_Service.
	 * @param 	PresentOnly	(Optional) True to only return devices currently present in the system.
	 *
	 * @returns	A std::expected&lt;std::vector&lt;std::wstring&gt;,nefarius::utilities::Win32Error&gt;
	 */
	std::expected<std::vector<std::wstring>, nefarius::utilities::Win32Error> ListDeviceInstancesByService(
		const std::wstring& ServiceName, bool PresentOnly = true);

	/**
	 * Detaches a single device from the system by removing its devnode sub-tree, WITHOUT
	 * re-enumerating the parent afterwards. This releases any file locks its driver holds (e.g.
	 * on the .sys file) so the driver's files/service can be safely replaced or deleted, while
	 * leaving the caller in control of when (or whether) the device is brought back with
	 * ReenumerateParentDevNode. Never forces the removal: if the PnP manager vetoes it (a
	 * driver/application is actively using the device), the veto reason is reported and nothing
	 * is torn down. Bounded by Timeout so a stuck driver can never block the caller forever.
	 * Never throws.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	InstanceId	Instance ID of the device to detach.
	 * @param 	Timeout   	(Optional) Upper bound this attempt may take before it is abandoned.
	 *
	 * @returns	A DetachResult
	 */
	DetachResult DetachDeviceInstance(const std::wstring& InstanceId,
	                                  std::chrono::milliseconds Timeout = std::chrono::seconds(10));

	/**
	 * Re-enumerates a devnode previously recorded as the parent of a device detached via
	 * DetachDeviceInstance, making Windows re-discover and re-bind a driver to whatever is now
	 * attached to it (e.g. after a driver upgrade replaced the .sys file). Bounded by Timeout so
	 * a stuck bus driver can never block the caller forever. Never throws.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	ParentInstanceId	Instance ID of the parent devnode, as returned in
	 * 								DetachResult::ParentInstanceId.
	 * @param 	Timeout				(Optional) Upper bound this attempt may take before it is
	 * 								abandoned.
	 *
	 * @returns	A ReenumerateResult
	 */
	ReenumerateResult ReenumerateParentDevNode(const std::wstring& ParentInstanceId,
	                                           std::chrono::milliseconds Timeout = std::chrono::seconds(10));

	/**
	 * Attempts to bring a single device back online without requiring a reboot, trying multiple
	 * strategies in order of reliability rather than of invasiveness: a USB hub port cycle first
	 * (the only strategy that works on a device with a handle held open elsewhere, e.g. a
	 * keyboard), then a software property-change restart, and finally the most invasive removal
	 * and re-enumeration of the parent devnode as a last resort. Each attempt is bounded by
	 * DeviceRestartOptions::PerDeviceTimeout so that a stuck driver can never block the caller
	 * forever. Never throws and never escalates beyond what Options allows.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	13.08.2026
	 *
	 * @param 	InstanceId	Instance ID of the device to restart.
	 * @param 	Options   	(Optional) Restart behaviour tuning knobs.
	 *
	 * @returns	A DeviceRestartResult
	 */
	DeviceRestartResult RestartDeviceInstance(const std::wstring& InstanceId,
	                                          const DeviceRestartOptions& Options = {});
}
