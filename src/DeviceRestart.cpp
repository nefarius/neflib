// ReSharper disable CppClangTidyModernizeUseEmplace
// ReSharper disable CppCStyleCast
// ReSharper disable CppRedundantQualifier
#include "pch.h"

#include <array>
#include <functional>
#include <future>
#include <thread>
#include <algorithm>

#include <winioctl.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <devpkey.h>

#include <nefarius/neflib/DeviceRestart.hpp>
#include <nefarius/neflib/GenHandleGuard.hpp>
#include <nefarius/neflib/MiscWinApi.hpp>
#include <nefarius/neflib/Diagnostics.hpp>
#include <nefarius/neflib/DiagnosticsFormat.hpp>

using namespace nefarius::utilities;

namespace
{
	//
	// Thin wrapper so instrumented call sites read as a single statement instead of constructing
	// a DiagnosticEvent inline every time. Level is almost always Verbose here: the final outcome
	// of every public API in this file is already available to the caller via its return value
	// (DeviceRestartResult/DetachResult/ReenumerateResult), so these events exist purely to make
	// the intermediate steps (which strategy was tried, why it was rejected, ...) visible under a
	// consumer's "--verbose"-equivalent flag, without duplicating the final summary a caller would
	// otherwise log twice.
	//
	void EmitDiag(DiagnosticLevel level, DiagnosticPhase phase, std::string_view operation,
	             const std::wstring& subject, std::optional<DWORD> win32Code, std::string message)
	{
		detail::EmitDiagnostic({level, phase, operation, subject, win32Code, std::move(message)});
	}

	//
	// Bundles the outcome of a single restart strategy attempt, self-contained so it can be
	// passed by value out of a worker thread without any references to the caller's stack.
	// 
	struct StrategyOutcome
	{
		std::expected<void, Win32Error> Result;

		bool RebootRequired = false;

		std::wstring VetoName;

		PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
	};

	//
	// Runs Fn on a worker thread and waits up to Timeout for it to finish. On timeout the worker
	// keeps running detached (a stuck SetupDiCallClassInstaller/CM_* call cannot be cancelled),
	// so the caller must never touch anything the closure references after a timeout is reported.
	// Templated so it can bound any outcome type that default-constructs and exposes a
	// std::expected<void, Win32Error> Result member (StrategyOutcome, DetachOutcome, ...).
	// 
	template <typename TOutcome>
	std::optional<TOutcome> RunBounded(std::chrono::milliseconds Timeout, std::function<TOutcome()> Fn)
	{
		std::promise<TOutcome> promise;
		std::future<TOutcome> future = promise.get_future();

		std::thread worker([promise = std::move(promise), fn = std::move(Fn)]() mutable
		{
			try
			{
				promise.set_value(fn());
			}
			catch (...)
			{
				promise.set_exception(std::current_exception());
			}
		});
		worker.detach();

		if (future.wait_for(Timeout) == std::future_status::ready)
		{
			try
			{
				return future.get();
			}
			catch (...)
			{
				//
				// Fn is not expected to throw, but a stuck-thread caller can never be allowed
				// to propagate an exception out of the no-throw contract of the public APIs.
				// 
				EmitDiag(DiagnosticLevel::Warning, DiagnosticPhase::Failure, "RunBounded", L"", ERROR_UNHANDLED_EXCEPTION,
				        "A restart/detach strategy worker threw an unexpected C++ exception; this indicates a bug "
				        "in neflib rather than a Win32 API failure");

				TOutcome outcome;
				outcome.Result = std::unexpected(Win32Error(ERROR_UNHANDLED_EXCEPTION));
				return outcome;
			}
		}

		return std::nullopt;
	}

	std::expected<std::wstring, Win32Error> GetDevNodePropertyString(DEVINST DevInst, const DEVPROPKEY& Key)
	{
		DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
		ULONG size = 0;

		CONFIGRET cr = CM_Get_DevNode_PropertyW(DevInst, &Key, &type, nullptr, &size, 0);

		if (cr == CR_NO_SUCH_VALUE)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "CM_Get_DevNode_PropertyW"));
		}

		if (cr != CR_BUFFER_SMALL)
		{
			return std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                  "CM_Get_DevNode_PropertyW"));
		}

		std::wstring value(size / sizeof(WCHAR), L'\0');

		cr = CM_Get_DevNode_PropertyW(DevInst, &Key, &type, reinterpret_cast<PBYTE>(value.data()), &size, 0);

		if (cr != CR_SUCCESS)
		{
			return std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                  "CM_Get_DevNode_PropertyW"));
		}

		if (type != DEVPROP_TYPE_STRING)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_DATATYPE, "CM_Get_DevNode_PropertyW"));
		}

		StripNullCharacters(value);

		return value;
	}

	std::expected<ULONG, Win32Error> GetDevNodePropertyUint32(DEVINST DevInst, const DEVPROPKEY& Key)
	{
		DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
		ULONG value = 0;
		ULONG size = sizeof(value);

		const CONFIGRET cr = CM_Get_DevNode_PropertyW(DevInst, &Key, &type, reinterpret_cast<PBYTE>(&value), &size,
		                                              0);

		if (cr != CR_SUCCESS)
		{
			return std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                  "CM_Get_DevNode_PropertyW"));
		}

		if (type != DEVPROP_TYPE_UINT32)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_DATATYPE, "CM_Get_DevNode_PropertyW"));
		}

		return value;
	}

	std::expected<DEVINST, Win32Error> LocateDevNode(const std::wstring& InstanceId, ULONG Flags)
	{
		std::wstring id = InstanceId;
		DEVINST devInst = 0;

		const CONFIGRET cr = CM_Locate_DevNodeW(&devInst, id.data(), Flags);

		if (cr != CR_SUCCESS)
		{
			return std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_NOT_FOUND), "CM_Locate_DevNodeW"));
		}

		return devInst;
	}

	//
	// Snapshot of a single CM_Get_DevNode_Status observation, self-contained so callers can tell
	// "device is present but stuck with a problem code" apart from "device is no longer present
	// at all" (a phantom/removed node) instead of collapsing both into a single bool.
	// 
	struct DevNodeObservation
	{
		bool Located = false;
		///< True if CM_Get_DevNode_Status was actually queried successfully for this devnode;
		///< Started/HasProblem/ProblemCode are only meaningful when this is true. A device can be
		///< Located but still have StatusValid == false if the status query itself failed (e.g.
		///< CR_NO_SUCH_DEVNODE if it vanished between the locate and the status call).
		bool StatusValid = false;
		///< CM_Get_DevNode_Status's CONFIGRET when StatusValid is false and Located is true;
		///< CR_SUCCESS otherwise
		CONFIGRET StatusError = CR_SUCCESS;
		bool Started = false;
		bool HasProblem = false;
		ULONG ProblemCode = 0;
	};

	//
	// A restart strategy reporting success (e.g. CM_Reenumerate_DevNode/SetupDiCallClassInstaller
	// returning CR_SUCCESS/TRUE) only means the restart *mechanism* didn't error out; it does not
	// guarantee the device is actually back and working (the driver could fail to load, or the
	// devnode could settle into a problem state). Polls the devnode status until it reports
	// DN_STARTED with no DN_HAS_PROBLEM, or Timeout elapses, returning whichever observation was
	// current at that point (online or not). A device that has disappeared entirely (e.g.
	// unplugged mid-restart, or a phantom node) is reflected as Located == false rather than as an
	// error, so the caller can simply try a more invasive strategy, or give up, or - for the final
	// authoritative re-check in RestartDeviceInstance - treat it as "nothing left to restart"
	// rather than "stuck, needs a reboot".
	// 
	DevNodeObservation PollDevNodeStatus(const std::wstring& InstanceId, std::chrono::milliseconds Timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + Timeout;

		for (;;)
		{
			DevNodeObservation observation;

			if (const auto devInst = ::LocateDevNode(InstanceId, CM_LOCATE_DEVNODE_NORMAL); devInst)
			{
				observation.Located = true;

				ULONG status = 0;
				ULONG problemNumber = 0;

				if (const auto statusResult = CM_Get_DevNode_Status(&status, &problemNumber, devInst.value(), 0);
					statusResult == CR_SUCCESS)
				{
					observation.StatusValid = true;
					observation.Started = (status & DN_STARTED) != 0;
					observation.HasProblem = (status & DN_HAS_PROBLEM) != 0;
					observation.ProblemCode = observation.HasProblem ? problemNumber : 0;
				}
				else
				{
					observation.StatusError = statusResult;
				}
			}

			if (observation.Located && observation.StatusValid && observation.Started && !observation.HasProblem)
			{
				return observation;
			}

			const auto now = std::chrono::steady_clock::now();

			if (now >= deadline)
			{
				return observation;
			}

			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
			Sleep(static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(100, remaining.count())));
		}
	}

	bool WaitForDeviceOnline(const std::wstring& InstanceId, std::chrono::milliseconds Timeout)
	{
		const auto observation = ::PollDevNodeStatus(InstanceId, Timeout);
		return observation.Located && observation.StatusValid && observation.Started && !observation.HasProblem;
	}

	std::wstring GetDeviceFriendlyNameBestEffort(const std::wstring& InstanceId)
	{
		const auto devInst = ::LocateDevNode(InstanceId, CM_LOCATE_DEVNODE_PHANTOM);

		if (!devInst)
		{
			return {};
		}

		if (auto name = ::GetDevNodePropertyString(devInst.value(), DEVPKEY_Device_FriendlyName); name)
		{
			return std::move(name.value());
		}

		if (auto desc = ::GetDevNodePropertyString(devInst.value(), DEVPKEY_Device_DeviceDesc); desc)
		{
			return std::move(desc.value());
		}

		return {};
	}

	//
	// Extracts the first {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} substring, if any, and parses it.
	// This handles fully-qualified AddReg/DelReg subkeys such as
	// "SYSTEM\CurrentControlSet\Control\Class\{<guid>}" without needing to validate the
	// remainder of the path.
	// 
	std::expected<GUID, Win32Error> ExtractGuidFromSubkeyPath(const std::wstring& Subkey)
	{
		const auto open = Subkey.find(L'{');

		if (open == std::wstring::npos)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND));
		}

		const auto close = Subkey.find(L'}', open);

		if (close == std::wstring::npos || close <= open)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND));
		}

		const std::wstring guidText = Subkey.substr(open, close - open + 1);

		return nefarius::winapi::GUIDFromString(ConvertWideToANSI(guidText));
	}

	//
	// Scans a single AddReg/DelReg-referenced section for UpperFilters/LowerFilters directives.
	// 
	void CollectFiltersFromRegSection(HINF Inf, const std::wstring& SectionName, const GUID& BaseClassGuid,
	                                  bool HaveBaseClassGuid,
	                                  std::vector<nefarius::devcon::InfClassFilterTarget>& Results)
	{
		INFCONTEXT ctx;

		if (!SetupFindFirstLineW(Inf, SectionName.c_str(), nullptr, &ctx))
		{
			return;
		}

		do
		{
			WCHAR root[LINE_LEN] = {};
			WCHAR subkey[LINE_LEN] = {};
			WCHAR valueName[LINE_LEN] = {};
			WCHAR valueData[LINE_LEN] = {};

			//
			// AddReg/DelReg lines have no key, so field numbering starts at 1: field 1 is the
			// root, field 2 the subkey, field 3 the value name, field 4 the flags, field 5 the data.
			// 
			if (!SetupGetStringFieldW(&ctx, 1, root, LINE_LEN, nullptr))
			{
				continue;
			}

			//
			// Subkey (field 2) is legitimately empty for class-relative (HKR) entries
			// 
			SetupGetStringFieldW(&ctx, 2, subkey, LINE_LEN, nullptr);

			if (!SetupGetStringFieldW(&ctx, 3, valueName, LINE_LEN, nullptr))
			{
				continue;
			}

			const bool isUpper = (_wcsicmp(valueName, L"UpperFilters") == 0);
			const bool isLower = (_wcsicmp(valueName, L"LowerFilters") == 0);

			if (!isUpper && !isLower)
			{
				continue;
			}

			GUID targetGuid = {};
			bool haveTarget = false;

			if (_wcsicmp(root, L"HKR") == 0 && wcslen(subkey) == 0)
			{
				if (HaveBaseClassGuid)
				{
					targetGuid = BaseClassGuid;
					haveTarget = true;
				}
			}
			else if (const auto extracted = ::ExtractGuidFromSubkeyPath(subkey); extracted.has_value())
			{
				targetGuid = extracted.value();
				haveTarget = true;
			}

			if (!haveTarget)
			{
				continue;
			}

			//
			// UpperFilters/LowerFilters is a REG_MULTI_SZ (flags 0x00010008); an AddReg/DelReg
			// line may list several filter service names starting at field 5, e.g.
			// HKR,,"UpperFilters",0x00010008,"filter1","filter2". Emit one target per name; a
			// DelReg line with no data fields at all (deleting the whole value) still yields a
			// single target with an empty service name so the affected class isn't lost.
			// 
			const DWORD fieldCount = SetupGetFieldCount(&ctx);
			const auto position = isLower
				? nefarius::devcon::DeviceClassFilterPosition::Lower
				: nefarius::devcon::DeviceClassFilterPosition::Upper;
			bool anyServiceName = false;

			for (DWORD field = 5; field <= fieldCount; field++)
			{
				if (!SetupGetStringFieldW(&ctx, field, valueData, LINE_LEN, nullptr) || valueData[0] == L'\0')
				{
					continue;
				}

				anyServiceName = true;
				Results.push_back(nefarius::devcon::InfClassFilterTarget{targetGuid, position, std::wstring(valueData)});
			}

			if (!anyServiceName)
			{
				Results.push_back(nefarius::devcon::InfClassFilterTarget{targetGuid, position, std::wstring()});
			}
		}
		while (SetupFindNextLine(&ctx, &ctx));
	}

	//
	// Resolves the (possibly multiple) AddReg=/DelReg= referenced sections of a DefaultInstall-style
	// section and scans each of them.
	// 
	void CollectRegDirectiveTargets(HINF Inf, const std::wstring& SectionName, PCWSTR Key, const GUID& BaseClassGuid,
	                                bool HaveBaseClassGuid, std::vector<nefarius::devcon::InfClassFilterTarget>&
	                                Results)
	{
		INFCONTEXT ctx;

		if (!SetupFindFirstLineW(Inf, SectionName.c_str(), Key, &ctx))
		{
			return;
		}

		do
		{
			const DWORD fieldCount = SetupGetFieldCount(&ctx);

			for (DWORD field = 1; field <= fieldCount; field++)
			{
				WCHAR referencedSection[LINE_LEN] = {};

				if (!SetupGetStringFieldW(&ctx, field, referencedSection, LINE_LEN, nullptr))
				{
					continue;
				}

				::CollectFiltersFromRegSection(Inf, referencedSection, BaseClassGuid, HaveBaseClassGuid, Results);
			}
		}
		while (SetupFindNextMatchLineW(&ctx, Key, &ctx));
	}

	//
	// Restarts a device via the classic DIF_PROPERTYCHANGE/DICS_PROPCHANGE mechanism. Works for
	// most bus types but is routinely ignored by drivers that keep a handle open (e.g. HID/keyboard
	// class drivers), which is why the USB port cycle is attempted first.
	// 
	StrategyOutcome TryPropertyChangeRestart(const std::wstring& InstanceId)
	{
		StrategyOutcome outcome;

		guards::HDEVINFOHandleGuard hDevInfo(SetupDiCreateDeviceInfoList(nullptr, nullptr));

		if (hDevInfo.is_invalid())
		{
			outcome.Result = std::unexpected(Win32Error("SetupDiCreateDeviceInfoList"));
			return outcome;
		}

		SP_DEVINFO_DATA devInfoData = {};
		devInfoData.cbSize = sizeof(devInfoData);

		if (!SetupDiOpenDeviceInfoW(hDevInfo.get(), InstanceId.c_str(), nullptr, 0, &devInfoData))
		{
			outcome.Result = std::unexpected(Win32Error("SetupDiOpenDeviceInfoW"));
			return outcome;
		}

		SP_PROPCHANGE_PARAMS params = {};
		params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
		params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
		params.Scope = DICS_FLAG_GLOBAL;
		params.StateChange = DICS_PROPCHANGE;

		if (!SetupDiSetClassInstallParams(hDevInfo.get(), &devInfoData, &params.ClassInstallHeader, sizeof(params)))
		{
			outcome.Result = std::unexpected(Win32Error("SetupDiSetClassInstallParams"));
			return outcome;
		}

		if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo.get(), &devInfoData))
		{
			//
			// Don't inspect the install params below on a failed call: DI_NEEDREBOOT/DI_NEEDRESTART
			// reflects the outcome of a class installer action that actually ran, and can otherwise
			// carry stale/incidental flags from this devinfo set that have nothing to do with this
			// particular (failed) attempt - a false "reboot needed" signal for a device that in fact
			// was never successfully touched by this strategy.
			// 
			outcome.Result = std::unexpected(Win32Error("SetupDiCallClassInstaller"));
			return outcome;
		}

		SP_DEVINSTALL_PARAMS_W installParams = {};
		installParams.cbSize = sizeof(installParams);

		if (SetupDiGetDeviceInstallParamsW(hDevInfo.get(), &devInfoData, &installParams))
		{
			outcome.RebootRequired = (installParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)) != 0;
		}

		outcome.Result = {};
		return outcome;
	}

	//
	// Bundles the outcome of a single detach attempt. Kept separate from StrategyOutcome (rather
	// than deriving from it) since RebootRequired has no meaning for a query-remove-subtree call.
	// 
	struct DetachOutcome
	{
		std::expected<void, Win32Error> Result;

		//
		// Valid immediately after a successful detach, for same-call-chain reenumeration
		// (TryRemoveAndReenumerate). Do not persist across process boundaries; use
		// ParentInstanceId (a stable string identifier) for that instead.
		// 
		DEVINST ParentDevInst = 0;

		std::wstring ParentInstanceId;

		std::wstring VetoName;

		PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
	};

	struct ReenumerateOutcome
	{
		std::expected<void, Win32Error> Result;
	};

	//
	// Removes a device's devnode sub-tree, releasing any file locks its driver holds, without
	// re-enumerating the parent. The PnP manager may veto the removal if a driver/application is
	// actively using the device, in which case the veto reason is surfaced and nothing is torn
	// down; the removal is never forced.
	// 
	DetachOutcome TryDetachDevice(const std::wstring& InstanceId)
	{
		DetachOutcome outcome;

		const auto devInst = ::LocateDevNode(InstanceId, CM_LOCATE_DEVNODE_NORMAL);

		if (!devInst)
		{
			outcome.Result = std::unexpected(devInst.error());
			return outcome;
		}

		DEVINST parent = 0;
		CONFIGRET cr = CM_Get_Parent(&parent, devInst.value(), 0);

		if (cr != CR_SUCCESS)
		{
			outcome.Result = std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_NOT_FOUND), "CM_Get_Parent"));
			return outcome;
		}

		WCHAR parentInstanceId[MAX_DEVICE_ID_LEN] = {};

		if (CM_Get_Device_IDW(parent, parentInstanceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
		{
			outcome.Result = std::unexpected(Win32Error(ERROR_NOT_FOUND, "CM_Get_Device_IDW"));
			return outcome;
		}

		PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
		WCHAR vetoName[MAX_PATH] = {};

		cr = CM_Query_And_Remove_SubTreeW(devInst.value(), &vetoType, vetoName, MAX_PATH, CM_REMOVE_UI_NOT_OK);

		if (cr == CR_REMOVE_VETOED)
		{
			outcome.VetoName = vetoName;
			outcome.VetoType = vetoType;
			outcome.Result = std::unexpected(Win32Error(ERROR_CANCELLED, "CM_Query_And_Remove_SubTreeW"));
			return outcome;
		}

		if (cr != CR_SUCCESS)
		{
			outcome.Result = std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                            "CM_Query_And_Remove_SubTreeW"));
			return outcome;
		}

		outcome.ParentDevInst = parent;
		outcome.ParentInstanceId = parentInstanceId;
		outcome.Result = {};
		return outcome;
	}

	ReenumerateOutcome TryReenumerateParent(const std::wstring& ParentInstanceId)
	{
		ReenumerateOutcome outcome;

		const auto devInst = ::LocateDevNode(ParentInstanceId, CM_LOCATE_DEVNODE_NORMAL);

		if (!devInst)
		{
			outcome.Result = std::unexpected(devInst.error());
			return outcome;
		}

		const CONFIGRET cr = CM_Reenumerate_DevNode(devInst.value(), CM_REENUMERATE_SYNCHRONOUS);

		if (cr != CR_SUCCESS)
		{
			outcome.Result = std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                            "CM_Reenumerate_DevNode"));
			return outcome;
		}

		outcome.Result = {};
		return outcome;
	}

	//
	// Removes the device sub-tree and forces its parent to re-enumerate. Most invasive restart
	// strategy; shares TryDetachDevice with the public DetachDeviceInstance API.
	// 
	StrategyOutcome TryRemoveAndReenumerate(const std::wstring& InstanceId)
	{
		StrategyOutcome outcome;

		const DetachOutcome detach = ::TryDetachDevice(InstanceId);

		outcome.VetoName = detach.VetoName;
		outcome.VetoType = detach.VetoType;

		if (!detach.Result.has_value())
		{
			outcome.Result = std::unexpected(detach.Result.error());
			return outcome;
		}

		const CONFIGRET cr = CM_Reenumerate_DevNode(detach.ParentDevInst, CM_REENUMERATE_SYNCHRONOUS);

		if (cr != CR_SUCCESS)
		{
			outcome.Result = std::unexpected(Win32Error(CM_MapCrToWin32Err(cr, ERROR_CAN_NOT_COMPLETE),
			                                            "CM_Reenumerate_DevNode"));
			return outcome;
		}

		outcome.Result = {};
		return outcome;
	}

	StrategyOutcome TryUsbPortCycle(const std::wstring& InstanceId)
	{
		StrategyOutcome outcome;
		outcome.Result = nefarius::devcon::CycleUsbPortOfDevice(InstanceId);
		return outcome;
	}
}

std::expected<std::vector<std::wstring>, Win32Error> nefarius::devcon::ListDeviceInstancesByClass(
	const GUID* ClassGuid, bool PresentOnly)
{
	const DWORD flags = PresentOnly ? DIGCF_PRESENT : 0;

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(ClassGuid, nullptr, nullptr, flags));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiGetClassDevs"));
	}

	std::vector<std::wstring> instances;
	SP_DEVINFO_DATA devInfoData = {};
	devInfoData.cbSize = sizeof(devInfoData);

	for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), index, &devInfoData); index++)
	{
		WCHAR instanceId[MAX_DEVICE_ID_LEN] = {};

		if (SetupDiGetDeviceInstanceIdW(hDevInfo.get(), &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr))
		{
			instances.emplace_back(instanceId);
		}
	}

	return instances;
}

std::expected<std::vector<std::wstring>, Win32Error> nefarius::devcon::ListDeviceInstancesByService(
	const std::wstring& ServiceName, bool PresentOnly)
{
	const DWORD flags = DIGCF_ALLCLASSES | (PresentOnly ? DIGCF_PRESENT : 0);

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(nullptr, nullptr, nullptr, flags));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiGetClassDevs"));
	}

	std::vector<std::wstring> instances;
	SP_DEVINFO_DATA devInfoData = {};
	devInfoData.cbSize = sizeof(devInfoData);

	for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), index, &devInfoData); index++)
	{
		const auto service = ::GetDevNodePropertyString(devInfoData.DevInst, DEVPKEY_Device_Service);

		if (!service || _wcsicmp(service.value().c_str(), ServiceName.c_str()) != 0)
		{
			continue;
		}

		WCHAR instanceId[MAX_DEVICE_ID_LEN] = {};

		if (SetupDiGetDeviceInstanceIdW(hDevInfo.get(), &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr))
		{
			instances.emplace_back(instanceId);
		}
	}

	if (const DWORD lastError = GetLastError(); lastError != ERROR_NO_MORE_ITEMS)
	{
		return std::unexpected(Win32Error(lastError, "SetupDiEnumDeviceInfo"));
	}

	return instances;
}

nefarius::devcon::DetachResult nefarius::devcon::DetachDeviceInstance(
	const std::wstring& InstanceId, std::chrono::milliseconds Timeout)
{
	DetachResult result;
	result.InstanceId = InstanceId;
	result.FriendlyName = ::GetDeviceFriendlyNameBestEffort(InstanceId);

	EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Begin, "DetachDeviceInstance", InstanceId, std::nullopt,
	        "Attempting to detach device sub-tree");

	auto outcome = ::RunBounded<DetachOutcome>(Timeout, [InstanceId] { return ::TryDetachDevice(InstanceId); });

	if (!outcome.has_value())
	{
		result.TimedOut = true;
		result.LastError = ERROR_TIMEOUT;
		return result;
	}

	if (outcome->Result.has_value())
	{
		result.Succeeded = true;
		result.LastError = ERROR_SUCCESS;
		result.ParentInstanceId = outcome->ParentInstanceId;
		return result;
	}

	result.LastError = outcome->Result.error().getErrorCode();
	result.VetoName = outcome->VetoName;
	result.VetoType = outcome->VetoType;
	return result;
}

nefarius::devcon::ReenumerateResult nefarius::devcon::ReenumerateParentDevNode(
	const std::wstring& ParentInstanceId, std::chrono::milliseconds Timeout)
{
	ReenumerateResult result;
	result.InstanceId = ParentInstanceId;

	EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Begin, "ReenumerateParentDevNode", ParentInstanceId,
	        std::nullopt, "Attempting to re-enumerate parent devnode");

	auto outcome = ::RunBounded<ReenumerateOutcome>(
		Timeout, [ParentInstanceId] { return ::TryReenumerateParent(ParentInstanceId); });

	if (!outcome.has_value())
	{
		result.TimedOut = true;
		result.LastError = ERROR_TIMEOUT;
		return result;
	}

	if (outcome->Result.has_value())
	{
		result.Succeeded = true;
		result.LastError = ERROR_SUCCESS;
		return result;
	}

	result.LastError = outcome->Result.error().getErrorCode();
	return result;
}

std::expected<void, Win32Error> nefarius::devcon::CycleUsbPortOfDevice(const std::wstring& InstanceId)
{
	const auto startDevInst = ::LocateDevNode(InstanceId, CM_LOCATE_DEVNODE_PHANTOM);

	if (!startDevInst)
	{
		return std::unexpected(startDevInst.error());
	}

	DEVINST current = startDevInst.value();
	DEVINST composite = startDevInst.value();
	DEVINST hub = 0;
	bool foundHub = false;

	//
	// Walk up the devnode tree until we hit a USB hub, remembering the last node visited before
	// it (the "composite" node), whose Device_Address property is the hub's port number.
	// 
	for (int depth = 0; depth < 64; depth++)
	{
		if (const auto service = ::GetDevNodePropertyString(current, DEVPKEY_Device_Service); service)
		{
			if (service.value().starts_with(L"USBHUB") ||
				_wcsnicmp(service.value().c_str(), L"USBHUB", 6) == 0)
			{
				hub = current;
				foundHub = true;
				break;
			}
		}

		composite = current;

		DEVINST parent = 0;
		const CONFIGRET cr = CM_Get_Parent(&parent, current, 0);

		if (cr != CR_SUCCESS)
		{
			break;
		}

		current = parent;
	}

	if (!foundHub)
	{
		return std::unexpected(Win32Error(ERROR_NOT_SUPPORTED, "No USB hub ancestor found for device"));
	}

	const auto port = ::GetDevNodePropertyUint32(composite, DEVPKEY_Device_Address);

	if (!port)
	{
		return std::unexpected(port.error());
	}

	WCHAR hubInstanceId[MAX_DEVICE_ID_LEN] = {};

	if (CM_Get_Device_IDW(hub, hubInstanceId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
	{
		return std::unexpected(Win32Error(ERROR_NOT_FOUND, "CM_Get_Device_IDW"));
	}

	GUID hubInterfaceGuid = GUID_DEVINTERFACE_USB_HUB;
	std::vector<WCHAR> listBuffer;

	//
	// The interface list can change between the size query and the list query (e.g. another hub
	// arrives/departs concurrently); retry a bounded number of times on CR_BUFFER_SMALL instead
	// of failing outright.
	// 
	for (int attempt = 0; attempt < 3; attempt++)
	{
		ULONG listLength = 0;

		if (CM_Get_Device_Interface_List_SizeW(&listLength, &hubInterfaceGuid, hubInstanceId,
		                                       CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "CM_Get_Device_Interface_List_SizeW"));
		}

		if (listLength <= 1)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "USB hub has no live device interface"));
		}

		listBuffer.assign(listLength, L'\0');

		const CONFIGRET cr = CM_Get_Device_Interface_ListW(&hubInterfaceGuid, hubInstanceId, listBuffer.data(),
		                                                   listLength, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

		if (cr == CR_SUCCESS)
		{
			break;
		}

		if (cr != CR_BUFFER_SMALL || attempt == 2)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "CM_Get_Device_Interface_ListW"));
		}
	}

	const std::wstring hubPath(listBuffer.data());

	if (hubPath.empty())
	{
		return std::unexpected(Win32Error(ERROR_NOT_FOUND, "Empty USB hub device interface path"));
	}

	guards::InvalidHandleGuard hubHandle(CreateFileW(
		hubPath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	));

	if (hubHandle.is_invalid())
	{
		return std::unexpected(Win32Error("CreateFileW"));
	}

	USB_CYCLE_PORT_PARAMS params = {};
	params.ConnectionIndex = port.value();

	DWORD bytesReturned = 0;

	const BOOL success = DeviceIoControl(
		hubHandle.get(),
		IOCTL_USB_HUB_CYCLE_PORT,
		&params,
		sizeof(params),
		&params,
		sizeof(params),
		&bytesReturned,
		nullptr
	);

	if (!success)
	{
		const DWORD win32Error = GetLastError();

		if (win32Error == ERROR_GEN_FAILURE)
		{
			return std::unexpected(Win32Error(win32Error,
			                                  "IOCTL_USB_HUB_CYCLE_PORT failed, this operation requires administrative privileges"));
		}

		if (win32Error == ERROR_NO_SUCH_DEVICE)
		{
			return std::unexpected(Win32Error(win32Error, "IOCTL_USB_HUB_CYCLE_PORT: port not found"));
		}

		return std::unexpected(Win32Error(win32Error, "IOCTL_USB_HUB_CYCLE_PORT"));
	}

	if (params.StatusReturned != 0)
	{
		return std::unexpected(Win32Error(ERROR_GEN_FAILURE, std::format(
			                       "IOCTL_USB_HUB_CYCLE_PORT reported a non-zero status: {}", params.StatusReturned)));
	}

	return {};
}

template <nefarius::utilities::string_type StringType>
std::expected<std::vector<nefarius::devcon::InfClassFilterTarget>, Win32Error>
nefarius::devcon::GetInfClassFilterTargets(const StringType& FullInfPath)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

	WCHAR normalisedInfPath[MAX_PATH] = {};

	if (const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, nullptr);
		(ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	guards::INFHandleGuard hInf(SetupOpenInfFileW(normalisedInfPath, nullptr, INF_STYLE_WIN4, nullptr));

	if (hInf.is_invalid())
	{
		return std::unexpected(Win32Error());
	}

	GUID baseClassGuid = {};
	bool haveBaseClassGuid = false;
	std::wstring className(MAX_CLASS_NAME_LEN, L'\0');

	if (SetupDiGetINFClassW(normalisedInfPath, &baseClassGuid, className.data(),
	                       (DWORD)className.size(), nullptr))
	{
		haveBaseClassGuid = true;
	}

	std::vector<InfClassFilterTarget> results;

	for (const auto* sectionKeyword : {L"DefaultInstall", L"DefaultUninstall"})
	{
		WCHAR resolvedSection[LINE_LEN] = {};
		DWORD reqSize = 0;

		if (!SetupDiGetActualSectionToInstallW(hInf.get(), sectionKeyword, resolvedSection, LINE_LEN, &reqSize,
		                                       nullptr))
		{
			//
			// Section not present for this INF/platform combination, not an error condition
			// 
			continue;
		}

		::CollectRegDirectiveTargets(hInf.get(), resolvedSection, L"AddReg", baseClassGuid, haveBaseClassGuid,
		                            results);
		::CollectRegDirectiveTargets(hInf.get(), resolvedSection, L"DelReg", baseClassGuid, haveBaseClassGuid,
		                            results);
	}

	std::vector<InfClassFilterTarget> deduped;

	for (auto& target : results)
	{
		const bool alreadyPresent = std::ranges::any_of(deduped, [&](const InfClassFilterTarget& existing)
		{
			return IsEqualGUID(existing.ClassGuid, target.ClassGuid)
				&& existing.Position == target.Position
				&& _wcsicmp(existing.ServiceName.c_str(), target.ServiceName.c_str()) == 0;
		});

		if (!alreadyPresent)
		{
			deduped.push_back(target);
		}
	}

	return deduped;
}

nefarius::devcon::DeviceRestartResult nefarius::devcon::RestartDeviceInstance(
	const std::wstring& InstanceId, const DeviceRestartOptions& Options)
{
	DeviceRestartResult result;
	result.InstanceId = InstanceId;
	result.FriendlyName = ::GetDeviceFriendlyNameBestEffort(InstanceId);

	EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Begin, "RestartDeviceInstance", InstanceId, std::nullopt,
	        "Attempting to bring device back online without a reboot");

	struct Attempt
	{
		RestartStrategy Strategy;
		bool Enabled;
		std::function<StrategyOutcome()> Fn;
	};

	const std::array<Attempt, 3> attempts{
		{
			{
				RestartStrategy::UsbPortCycle, Options.AllowUsbPortCycle,
				[InstanceId] { return ::TryUsbPortCycle(InstanceId); }
			},
			{
				RestartStrategy::PropertyChange, Options.AllowPropertyChange,
				[InstanceId] { return ::TryPropertyChangeRestart(InstanceId); }
			},
			{
				RestartStrategy::RemoveAndReenumerate, Options.AllowRemoveAndReenumerate,
				[InstanceId] { return ::TryRemoveAndReenumerate(InstanceId); }
			},
		}
	};

	//
	// Tracks the most recent strategy whose *mechanism* actually reported success (independent of
	// whether WaitForDeviceOnline verified it in time), so the delayed-verification path below can
	// credit the strategy that plausibly caused the device to come back, instead of whatever was
	// merely tried last (which may have been vetoed, errored out, or timed out).
	// 
	RestartStrategy lastMechanismSucceeded = RestartStrategy::None;

	for (const auto& attempt : attempts)
	{
		if (!attempt.Enabled)
		{
			continue;
		}

		result.LastAttempted = attempt.Strategy;

		EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Progress, "RestartDeviceInstance", InstanceId,
		        std::nullopt, std::format("Attempting strategy: {}", ToString(attempt.Strategy)));

		auto outcome = ::RunBounded<StrategyOutcome>(Options.PerDeviceTimeout, attempt.Fn);

		if (!outcome.has_value())
		{
			//
			// The worker may still be running; never start a second strategy racing against it
			// 
			EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Failure, "RestartDeviceInstance", InstanceId,
			        ERROR_TIMEOUT, std::format("Strategy {} timed out after {} ms", ToString(attempt.Strategy),
			                                   Options.PerDeviceTimeout.count()));
			result.TimedOut = true;
			result.LastError = ERROR_TIMEOUT;
			break;
		}

		if (outcome->Result.has_value())
		{
			lastMechanismSucceeded = attempt.Strategy;

			//
			// Only trust this strategy's RebootRequired signal now that its mechanism actually
			// succeeded: install-params flags read after a failed attempt can be stale/incidental
			// and would otherwise let a "device could not be restarted" warning outrank a driver
			// operation (e.g. service removal) that itself completed cleanly.
			// 
			result.RebootRequired = result.RebootRequired || outcome->RebootRequired;

			//
			// Don't just trust the strategy's own success signal: confirm the device is
			// actually back online (present, started, no problem code) before declaring
			// victory. If it isn't (yet), fall through to try any remaining, more invasive
			// strategy instead of reporting a false positive.
			// 
			if (::WaitForDeviceOnline(InstanceId, Options.PostRestartVerifyTimeout))
			{
				result.Strategy = attempt.Strategy;
				result.Succeeded = true;
				result.LastError = ERROR_SUCCESS;
				EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::End, "RestartDeviceInstance", InstanceId,
				        std::nullopt, std::format("Strategy {} succeeded and was verified online",
				                                  ToString(attempt.Strategy)));
				break;
			}

			EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Progress, "RestartDeviceInstance", InstanceId,
			        std::nullopt, std::format(
				        "Strategy {} reported success but the device did not verify online within {} ms; trying next strategy",
				        ToString(attempt.Strategy), Options.PostRestartVerifyTimeout.count()));
			result.LastError = ERROR_DEVICE_NOT_CONNECTED;
			continue;
		}

		result.LastError = outcome->Result.error().getErrorCode();
		result.VetoName = outcome->VetoName;
		result.VetoType = outcome->VetoType;

		EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Failure, "RestartDeviceInstance", InstanceId,
		        result.LastError,
		        outcome->VetoName.empty()
			        ? std::format("Strategy {} failed: {}", ToString(attempt.Strategy),
			                     outcome->Result.error().getErrorMessageA())
			        : std::format("Strategy {} vetoed by \"{}\"", ToString(attempt.Strategy),
			                     ConvertWideToANSI(outcome->VetoName)));
	}

	//
	// Every strategy has been exhausted (or none were enabled) without a verified success. Before
	// reporting failure, take one final authoritative look at the devnode instead of trusting the
	// last strategy's own (possibly premature) verify window: this is a plain status query, safe
	// to run even if the last attempt above hit PerDeviceTimeout and its worker is still running
	// in the background, since it does not touch anything that worker owns. A device that settles
	// into DN_STARTED with no problem code just a little later than a single strategy's verify
	// window is reported as Succeeded here rather than as a false failure; a device that is no
	// longer present at all, or is present but genuinely stuck with a problem code, is reported as
	// such via DevicePresent/FinalStarted/FinalHasProblem/FinalProblemCode either way.
	// 
	const auto finalObservation = ::PollDevNodeStatus(InstanceId, Options.PostRestartVerifyTimeout);

	result.DevicePresent = finalObservation.Located;
	result.FinalStatusValid = finalObservation.StatusValid;
	result.FinalStatusError = finalObservation.StatusError;

	if (finalObservation.StatusValid)
	{
		result.FinalStarted = finalObservation.Started;
		result.FinalHasProblem = finalObservation.HasProblem;
		result.FinalProblemCode = finalObservation.ProblemCode;
	}

	if (!result.Succeeded && finalObservation.Located && finalObservation.StatusValid &&
		finalObservation.Started && !finalObservation.HasProblem)
	{
		result.Succeeded = true;
		result.LastError = ERROR_SUCCESS;

		if (result.Strategy == RestartStrategy::None)
		{
			result.Strategy = lastMechanismSucceeded;
		}
	}

	//
	// Deliberately no final End/Failure summary event here: RestartDeviceInstance's return value
	// already carries everything a caller needs to log a single, authoritative outcome line (see
	// DescribeDeviceRestartResult), and emitting an equivalent event here as well would either be
	// invisible (if Verbose) or double-log the same outcome next to the caller's own summary (if
	// not). The Begin/Progress events above exist purely to add intermediate detail a caller could
	// not otherwise reconstruct from the return value alone.
	//
	return result;
}
