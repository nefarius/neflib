// ReSharper disable CppClangTidyModernizeUseEmplace
// ReSharper disable CppCStyleCast
// ReSharper disable CppRedundantQualifier
#include "pch.h"

#include <nefarius/neflib/Devcon.hpp>
#include <nefarius/neflib/GenHandleGuard.hpp>


using namespace nefarius::utilities;

namespace
{
	struct DeviceRegistryPropertyResult
	{
		wil::unique_hlocal_ptr<uint8_t[]> Data;

		size_t Length;

		DWORD DataType;
	};

	std::expected<DeviceRegistryPropertyResult, Win32Error> GetDeviceRegistryProperty(
		_In_ HDEVINFO DeviceInfoSet,
		_In_ PSP_DEVINFO_DATA DeviceInfoData,
		_In_ DWORD Property
	)
	{
		DWORD sizeRequired = 0;
		DWORD propertyRegDataType;

		//
		// Query required size
		// 
		(void)SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet,
		                                        DeviceInfoData,
		                                        Property,
		                                        &propertyRegDataType,
		                                        NULL,
		                                        0,
		                                        &sizeRequired);

		DWORD win32Error = GetLastError();

		//
		// Property doesn't exist
		// 
		if (win32Error == ERROR_INVALID_DATA)
		{
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "SetupDiGetDeviceRegistryPropertyW"));
		}

		//
		// Unexpected status other than required size
		// 
		if (win32Error != ERROR_INSUFFICIENT_BUFFER)
		{
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryPropertyW"));
		}

		auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(sizeRequired);

		//
		// Query property value
		// 
		if (!SetupDiGetDeviceRegistryPropertyW(DeviceInfoSet,
		                                       DeviceInfoData,
		                                       Property,
		                                       &propertyRegDataType,
		                                       buffer.get(),
		                                       sizeRequired,
		                                       &sizeRequired))
		{
			win32Error = GetLastError();
			buffer.release();
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryPropertyW"));
		}

		return DeviceRegistryPropertyResult{std::move(buffer), sizeRequired, propertyRegDataType};
	}

	DWORD Win32FromHResult(HRESULT hr)
	{
		if ((hr & 0xFFFF0000) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, 0))
		// NOLINT(clang-diagnostic-sign-compare)
		{
			return HRESULT_CODE(hr);
		}

		if (hr == S_OK)
		{
			return ERROR_SUCCESS;
		}

		// Not a Win32 HRESULT so return a generic error code.
		return ERROR_CAN_NOT_COMPLETE;
	}

	PWSTR wstristr(PCWSTR haystack, PCWSTR needle)
	{
		do
		{
			PCWSTR h = haystack;
			PCWSTR n = needle;
			while (towlower(*h) == towlower(*n) && *n)
			{
				h++;
				n++;
			}
			if (*n == 0)
			{
				return (PWSTR)haystack;
			}
		}
		while (*haystack++);
		return nullptr;
	}

	//
	// Reads a single string field from an INF's [Version] section (e.g. "Provider", "DriverVer"),
	// used to build a lightweight identity for matching an original INF against its published
	// copy inside the driver store. Returns std::nullopt if the key isn't present rather than an
	// error, since not every INF sets every field.
	//
	std::optional<std::wstring> ReadInfVersionField(HINF hInf, PCWSTR key)
	{
		INFCONTEXT ctx;

		if (!SetupFindFirstLineW(hInf, L"Version", key, &ctx))
		{
			return std::nullopt;
		}

		DWORD required = 0;
		SetupGetStringFieldW(&ctx, 1, nullptr, 0, &required);

		if (required == 0)
		{
			return std::nullopt;
		}

		std::wstring value(required, L'\0');

		if (!SetupGetStringFieldW(&ctx, 1, value.data(), required, nullptr))
		{
			return std::nullopt;
		}

		//
		// SetupGetStringFieldW's required size includes the NUL terminator.
		// 
		if (!value.empty() && value.back() == L'\0')
		{
			value.pop_back();
		}

		return value;
	}

	//
	// Collects every non-inbox package while DriverStoreOfflineEnumDriverPackageW enumerates the
	// driver store; always returns 1 (continue) since a full inventory is wanted regardless of
	// what has been found so far.
	// 
	struct EnumCollectContext
	{
		std::vector<nefarius::devcon::DriverStorePackage> Packages;
	};

	int WINAPI EnumCollectCallback(PCWSTR driverPackageInfPath, PVOID enumInfoPtr, PVOID context)
	{
		if (!enumInfoPtr || !driverPackageInfPath)
		{
			return 0;
		}

		auto* ctx = static_cast<EnumCollectContext*>(context);
		const auto* info = static_cast<const nefarius::utilities::DriverStoreOfflineEnumDriverPackageInfoW*>(
			enumInfoPtr);

		if (info->InboxInf == 0)
		{
			try
			{
				nefarius::devcon::DriverStorePackage package;
				package.DriverPackageInfPath = driverPackageInfPath;
				package.PublishedInfName = std::wstring(info->PublishedInfName,
					wcsnlen(info->PublishedInfName, std::size(info->PublishedInfName)));
				package.IsInbox = false;
				package.ProcessorArchitecture = info->ProcessorArchitecture;
				package.LocaleName = std::wstring(info->LocaleName,
					wcsnlen(info->LocaleName, std::size(info->LocaleName)));
				ctx->Packages.push_back(std::move(package));
			}
			catch (const std::bad_alloc&)
			{
				//
				// This callback is invoked directly by drvstore.dll across a C ABI boundary; a
				// C++ exception must never be allowed to propagate through it.
				// 
				return 0;
			}
		}

		return 1;
	}

	Win32Error NtStatusToWin32Error(const nefarius::utilities::DrvStore& drvStore, LONG status, const char* context)
	{
		if (drvStore.fpRtlNtStatusToDosError)
		{
			return Win32Error(drvStore.fpRtlNtStatusToDosError(status), context);
		}

		return Win32Error(ERROR_CAN_NOT_COMPLETE, context);
	}

	//
	// Thin wrapper so driver-store call sites read as a single statement instead of constructing
	// a DiagnosticEvent inline every time; mirrors DeviceRestart.cpp's EmitDiag. Level is almost
	// always Verbose: these events exist to make matching decisions (which candidates were
	// accepted/rejected and why) visible under a consumer's "--verbose"-equivalent flag, not to
	// duplicate the final outcome a caller already gets back via the return value.
	// 
	void EmitDiag(DiagnosticLevel level, DiagnosticPhase phase, std::string_view operation,
	             const std::wstring& subject, std::optional<DWORD> win32Code, std::string message)
	{
		detail::EmitDiagnostic({level, phase, operation, subject, win32Code, std::move(message)});
	}

	std::expected<void, Win32Error> uninstall_device_and_driver(
		HDEVINFO hDevInfo, PSP_DEVINFO_DATA spDevInfoData, bool* rebootRequired)
	{
		BOOL drvNeedsReboot = FALSE, devNeedsReboot = FALSE;
		DWORD requiredBufferSize = 0;
		Newdev newdev;

		if (!newdev.fpDiUninstallDevice || !newdev.fpDiUninstallDriverW)
		{
			return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
				                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
				                       : Win32Error(ERROR_PROC_NOT_FOUND,
				                                   "DiUninstallDevice/DiUninstallDriverW export not found"));
		}

		SP_DRVINFO_DATA_W drvInfoData;
		drvInfoData.cbSize = sizeof(drvInfoData);

		//
		// Start building driver info
		// 
		if (!SetupDiBuildDriverInfoList(
			hDevInfo,
			spDevInfoData,
			SPDIT_COMPATDRIVER
		))
		{
			return std::unexpected(Win32Error("SetupDiBuildDriverInfoList"));
		}

		const auto driverGuard = sg::make_scope_guard([hDevInfo, spDevInfoData]() noexcept
		{
			//
			// SetupDiBuildDriverInfoList allocated memory we need to explicitly free again
			// 
			SetupDiDestroyDriverInfoList(
				hDevInfo,
				spDevInfoData,
				SPDIT_COMPATDRIVER
			);
		});

		DWORD drvEnumLastError = ERROR_SUCCESS;

		if (!SetupDiEnumDriverInfo(
			hDevInfo,
			spDevInfoData,
			SPDIT_COMPATDRIVER,
			0, // One result expected
			&drvInfoData
		) && (drvEnumLastError = GetLastError()) != ERROR_NO_MORE_ITEMS /* driver-less device */)
		{
			return std::unexpected(Win32Error("SetupDiEnumDriverInfo"));
		}

		//
		// Device is missing driver, removal can be short-circuited
		//
		if (drvEnumLastError == ERROR_NO_MORE_ITEMS)
		{
			SP_REMOVEDEVICE_PARAMS removeParams;
			removeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
		    removeParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
		    removeParams.Scope = DI_REMOVEDEVICE_GLOBAL;
		    removeParams.HwProfile = 0;

			if(!SetupDiSetClassInstallParams(
				hDevInfo,
				spDevInfoData,
				&removeParams.ClassInstallHeader,
				sizeof(removeParams))
				)
			{
				return std::unexpected(Win32Error("SetupDiSetClassInstallParams"));
			}

			if (!SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, spDevInfoData))
			{
				return std::unexpected(Win32Error("SetupDiCallClassInstaller"));
			}

			SP_DEVINSTALL_PARAMS devParams;
			devParams.cbSize = sizeof(devParams);
			if (SetupDiGetDeviceInstallParams(
					hDevInfo,
					spDevInfoData,
					&devParams
				) && (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))
			)
			{
				if (rebootRequired)
					*rebootRequired = TRUE;
			}

			// nothing more to do with this instance
			return {};
		}

		//
		// Details will contain the INF path to driver store copy
		// 
		SP_DRVINFO_DETAIL_DATA_W drvInfoDetailData;
		drvInfoDetailData.cbSize = sizeof(drvInfoDetailData);

		//
		// Request required buffer size
		// 
		(void)SetupDiGetDriverInfoDetail(
			hDevInfo,
			spDevInfoData,
			&drvInfoData,
			&drvInfoDetailData,
			drvInfoDetailData.cbSize,
			&requiredBufferSize
		);

		if (requiredBufferSize == 0)
		{
			return std::unexpected(Win32Error("SetupDiGetDriverInfoDetail"));
		}

		//
		// Allocate required amount
		// 
		PSP_DRVINFO_DETAIL_DATA_W pDrvInfoDetailData = static_cast<PSP_DRVINFO_DETAIL_DATA_W>(
			malloc(requiredBufferSize));

		const auto dataGuard = sg::make_scope_guard([pDrvInfoDetailData]() noexcept
		{
			if (pDrvInfoDetailData != nullptr)
			{
				free(pDrvInfoDetailData);
			}
		});

		if (pDrvInfoDetailData == nullptr)
		{
			return std::unexpected(Win32Error(ERROR_INSUFFICIENT_BUFFER));
		}

		pDrvInfoDetailData->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

		//
		// Query full driver details
		// 
		if (!SetupDiGetDriverInfoDetail(
			hDevInfo,
			spDevInfoData,
			&drvInfoData,
			pDrvInfoDetailData,
			requiredBufferSize,
			nullptr
		))
		{
			return std::unexpected(Win32Error("SetupDiGetDriverInfoDetail"));
		}

		//
		// Remove device
		// 
		if (!newdev.fpDiUninstallDevice(
			nullptr,
			hDevInfo,
			spDevInfoData,
			0,
			&devNeedsReboot
		))
		{
			return std::unexpected(Win32Error("DiUninstallDevice"));
		}

		//
		// Uninstall from driver store
		// 
		if (!newdev.fpDiUninstallDriverW(
			nullptr,
			pDrvInfoDetailData->InfFileName,
			0,
			&drvNeedsReboot
		))
		{
			return std::unexpected(Win32Error("DiUninstallDriverW"));
		}

		if (rebootRequired)
			*rebootRequired = (drvNeedsReboot > 0) || (devNeedsReboot > 0);

		return {};
	}

	decltype(MessageBoxW)* real_MessageBoxW = MessageBoxW;

	int DetourMessageBoxW(
		HWND hWnd,
		LPCWSTR lpText,
		LPCWSTR lpCaption,
		UINT uType
	);

	BOOL g_MbCalled = FALSE;

	//
	// Captured from the intercepted MessageBoxW call below so a caller doesn't just see an opaque
	// "InstallHinfSectionW failed" - the dialog text/caption SetupAPI would otherwise have shown
	// the (non-interactive) user is often the only place the actual failure reason is surfaced.
	// Protected by the same named mutex InfDefaultInstall/InfDefaultUninstall already take before
	// attaching the detour (see the "SharedLock" CreateMutex calls below), since only one INF
	// (un)install can be in flight at a time.
	// 
	std::wstring g_LastMessageBoxCaption;
	std::wstring g_LastMessageBoxText;

	decltype(RestartDialogEx)* real_RestartDialogEx = RestartDialogEx;

	int DetourRestartDialogEx(
		HWND hwnd,
		PCWSTR pszPrompt,
		DWORD dwReturn,
		DWORD dwReasonCode
	);

	BOOL g_RestartDialogExCalled = FALSE;

	//
	// Hooks MessageBoxW which is called if an error occurred, even when instructed to suppress any UI interaction
	// 
	int DetourMessageBoxW(
		HWND hWnd,
		LPCWSTR lpText,
		LPCWSTR lpCaption,
		UINT uType
	)
	{
		UNREFERENCED_PARAMETER(hWnd);
		UNREFERENCED_PARAMETER(uType);

		g_LastMessageBoxText = lpText ? lpText : L"";
		g_LastMessageBoxCaption = lpCaption ? lpCaption : L"";
		g_MbCalled = TRUE;

		return IDOK;
	}

	//
	// Hooks RestartDialogEx which is called if a reboot is required, even when instructed to suppress any UI interaction
	// 
	int DetourRestartDialogEx(
		HWND hwnd,
		PCWSTR pszPrompt,
		DWORD dwReturn,
		DWORD dwReasonCode
	)
	{
		UNREFERENCED_PARAMETER(hwnd);
		UNREFERENCED_PARAMETER(pszPrompt);
		UNREFERENCED_PARAMETER(dwReturn);
		UNREFERENCED_PARAMETER(dwReasonCode);

		g_RestartDialogExCalled = TRUE;

		return IDCANCEL; // equivalent to the user clicking "Restart Later"
	}
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::Create(const StringType& ClassName, const GUID* ClassGuid,
                                                         const WideMultiStringArray& HardwareId)
{
	const std::wstring className = ConvertToWide(ClassName);

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiCreateDeviceInfoList(ClassGuid, nullptr));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiCreateDeviceInfoList"));
	}

	SP_DEVINFO_DATA deviceInfoData{};
	deviceInfoData.cbSize = sizeof(deviceInfoData);

	//
	// Create new device node
	// 
	if (!SetupDiCreateDeviceInfoW(
		hDevInfo.get(),
		className.c_str(),
		ClassGuid,
		nullptr,
		nullptr,
		DICD_GENERATE_ID,
		&deviceInfoData
	))
	{
		return std::unexpected(Win32Error("SetupDiCreateDeviceInfoW"));
	}

	//
	// Add the HardwareID to the Device's HardwareID property.
	//
	if (!SetupDiSetDeviceRegistryPropertyW(
		hDevInfo.get(),
		&deviceInfoData,
		SPDRP_HARDWAREID,
		HardwareId.data(),
		static_cast<DWORD>(HardwareId.size())
	))
	{
		return std::unexpected(Win32Error("SetupDiSetDeviceRegistryPropertyW"));
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
		return std::unexpected(Win32Error("SetupDiCallClassInstaller"));
	}

	return {};
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::Update(const StringType& HardwareId,
                                                         const StringType& FullInfPath,
                                                         bool* RebootRequired, bool Force)
{
	const std::wstring hardwareId = ConvertToWide(HardwareId);
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

	Newdev newdev;
	DWORD flags = 0;
	BOOL reboot = FALSE;
	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	if (Force)
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
		return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
			                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
			                       : Win32Error(ERROR_PROC_NOT_FOUND,
			                                   "UpdateDriverForPlugAndPlayDevicesW export not found"));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error("UpdateDriverForPlugAndPlayDevicesW"));
	case FunctionCallResult::Success:
		if (RebootRequired)
			*RebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::InstallDriver(const StringType& FullInfPath,
                                                                bool* RebootRequired)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

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
		return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
			                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
			                       : Win32Error(ERROR_PROC_NOT_FOUND, "DiInstallDriverW export not found"));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error("DiInstallDriverW"));
	case FunctionCallResult::Success:
		if (RebootRequired)
			*RebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::UninstallDriver(const StringType& FullInfPath,
                                                                  bool* RebootRequired)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

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
		return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
			                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
			                       : Win32Error(ERROR_PROC_NOT_FOUND, "DiUninstallDriverW export not found"));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error("DiUninstallDriverW"));
	case FunctionCallResult::Success:
		if (RebootRequired)
			*RebootRequired = reboot > 0;
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::vector<std::expected<void, Win32Error>> nefarius::devcon::UninstallDeviceAndDriver(
	const GUID* ClassGuid, const StringType& HardwareId, bool* RebootRequired)
{
	const std::wstring hardwareId = ConvertToWide(HardwareId);

	std::vector<std::expected<void, Win32Error>> results;

	SP_DEVINFO_DATA spDevInfoData;

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
		ClassGuid,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	));

	if (hDevInfo.is_invalid())
	{
		results.push_back(std::unexpected(Win32Error("SetupDiGetClassDevs")));
		return results;
	}

	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), i, &spDevInfoData); i++)
	{
		const auto hwIdBuffer = GetDeviceRegistryProperty(
			hDevInfo.get(),
			&spDevInfoData,
			SPDRP_HARDWAREID
		);

		if (!hwIdBuffer)
		{
			results.push_back(std::unexpected(hwIdBuffer.error()));
			continue;
		}

		LPWSTR buffer = (LPWSTR)hwIdBuffer.value().Data.get();

		//
		// find device matching hardware ID
		// 
		for (LPWSTR p = buffer; p && *p && (p < &buffer[hwIdBuffer.value().Length]); p += lstrlenW(p) + 1)
		{
			if (wstristr(p, hardwareId.c_str()))
			{
				results.push_back(::uninstall_device_and_driver(
					hDevInfo.get(),
					&spDevInfoData,
					RebootRequired
				));
				break;
			}
		}
	}

	return results;
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::InfDefaultInstall(
	const StringType& FullInfPath, bool* RebootRequired)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

	SYSTEM_INFO sysInfo;
	WCHAR InfSectionWithExt[LINE_LEN] = {};
	constexpr int maxCmdLine = 280;
	WCHAR pszDest[maxCmdLine] = {};
	BOOLEAN hasDefaultSection = FALSE;

	GetNativeSystemInfo(&sysInfo);

	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	guards::INFHandleGuard hInf(SetupOpenInfFileW(normalisedInfPath, nullptr, INF_STYLE_WIN4, nullptr));

	if (hInf.is_invalid())
	{
		return std::unexpected(Win32Error());
	}

	//
	// Try default section first, which is common to class filter driver, filesystem drivers and alike
	// 
	if (SetupDiGetActualSectionToInstallW(
			hInf.get(),
			L"DefaultInstall",
			InfSectionWithExt,
			LINE_LEN,
			reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
			nullptr)
		&& SetupFindFirstLineW(
			hInf.get(),
			InfSectionWithExt,
			nullptr,
			reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
		))
	{
		hasDefaultSection = TRUE;

		if (const HRESULT hr = StringCchPrintfW(pszDest, maxCmdLine, L"DefaultInstall 132 %ws", normalisedInfPath);
			FAILED(hr))
		{
			return std::unexpected(Win32Error(::Win32FromHResult(hr), "StringCchPrintfW"));
		}

		//
		// Since we cheat with global resources to monitor state we must not run in parallel
		// 
		guards::NullHandleGuard lock(CreateMutex(NULL, TRUE, __FUNCTIONW__ "-SharedLock-274fc7"));

		if (lock.is_invalid())
		{
			return std::unexpected(Win32Error(ERROR_LOCK_VIOLATION, "CreateMutex"));
		}

		//
		// Some implementations are bugged and do not respect the non-interactive flags,
		// so we catch the use of common dialog APIs and nullify their impact :)
		// 

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach((void**)&real_MessageBoxW, DetourMessageBoxW); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		g_MbCalled = FALSE;
		g_RestartDialogExCalled = FALSE;
		g_LastMessageBoxCaption.clear();
		g_LastMessageBoxText.clear();

		InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

		DWORD win32Error = GetLastError();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach((void**)&real_MessageBoxW, DetourMessageBoxW); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		//
		// If a message box call was intercepted, we encountered an error. SetupAPI's dialog text
		// is often the *only* place the actual failure reason (e.g. a missing dependency, a
		// signature problem) is surfaced; without it, callers only ever see a generic
		// "InstallHinfSectionW failed" with whatever (possibly stale/unrelated) code GetLastError()
		// happened to report.
		// 
		if (g_MbCalled)
		{
			g_MbCalled = FALSE;

			std::string context = "InstallHinfSectionW";

			if (!g_LastMessageBoxText.empty())
			{
				context += std::format(" (dialog: \"{}\")",
				                      ConvertWideToANSI(g_LastMessageBoxCaption.empty()
					                                        ? g_LastMessageBoxText
					                                        : g_LastMessageBoxCaption + L": " + g_LastMessageBoxText));
			}

			//
			// InstallHinfSectionW has no return value, and the intercepted dialog is the only
			// reliable signal that this failed; GetLastError() may still read ERROR_SUCCESS from
			// an unrelated earlier call. Reporting that as-is here would return an "unexpected
			// success" error code to callers that propagate getErrorCode() as their own exit code.
			// 
			const DWORD effectiveError = (win32Error != ERROR_SUCCESS) ? win32Error : ERROR_FUNCTION_FAILED;

			return std::unexpected(Win32Error(effectiveError, context));
		}
	}

	//
	// If we have no Default, but a Manufacturer section we can attempt classic installation
	// 
	if (!SetupFindFirstLineW(
		hInf.get(),
		L"Manufacturer",
		nullptr,
		reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
	))
	{
		//
		// We need either one or the other, this INF appears to not be compatible with this install method
		// 
		if (!hasDefaultSection)
		{
			return std::unexpected(Win32Error(ERROR_SECTION_NOT_FOUND, "SetupFindFirstLineW"));
		}
	}

	Newdev newdev;
	BOOL reboot = FALSE;

	switch (newdev.CallFunction(
		newdev.fpDiInstallDriverW,
		nullptr,
		normalisedInfPath,
		0,
		&reboot
	))
	{
	case FunctionCallResult::NotAvailable:
		return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
			                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
			                       : Win32Error(ERROR_PROC_NOT_FOUND, "DiInstallDriverW export not found"));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error("DiInstallDriverW"));
	case FunctionCallResult::Success:
		if (RebootRequired)
		{
			*RebootRequired = reboot > FALSE || g_RestartDialogExCalled;
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::InfDefaultUninstall(const StringType& FullInfPath,
                                                                      bool* RebootRequired)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

	SYSTEM_INFO sysInfo;
	WCHAR InfSectionWithExt[LINE_LEN] = {};
	constexpr int maxCmdLine = 280;
	WCHAR pszDest[maxCmdLine] = {};

	GetNativeSystemInfo(&sysInfo);

	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	guards::INFHandleGuard hInf(SetupOpenInfFileW(normalisedInfPath, nullptr, INF_STYLE_WIN4, nullptr));

	if (hInf.is_invalid())
	{
		return std::unexpected(Win32Error());
	}

	if (SetupDiGetActualSectionToInstallW(
			hInf.get(),
			L"DefaultUninstall",
			InfSectionWithExt,
			LINE_LEN,
			reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
			nullptr)
		&& SetupFindFirstLineW(
			hInf.get(),
			InfSectionWithExt,
			nullptr,
			reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
		))
	{
		if (const HRESULT hr = StringCchPrintfW(pszDest, maxCmdLine, L"DefaultUninstall 132 %ws", normalisedInfPath);
			FAILED(hr))
		{
			return std::unexpected(Win32Error(::Win32FromHResult(hr), "StringCchPrintfW"));
		}

		//
		// Since we cheat with global resources to monitor state we must not run in parallel
		// 
		guards::NullHandleGuard lock(CreateMutex(NULL, TRUE, __FUNCTIONW__ "-SharedLock-274fc7"));

		if (lock.is_invalid())
		{
			return std::unexpected(Win32Error(ERROR_LOCK_VIOLATION, "CreateMutex"));
		}

		g_RestartDialogExCalled = FALSE;
		g_MbCalled = FALSE;
		g_LastMessageBoxCaption.clear();
		g_LastMessageBoxText.clear();

		//
		// Some implementations are bugged and do not respect the non-interactive flags,
		// so we catch the use of common dialog APIs and nullify their impact :). Unlike the
		// original implementation, MessageBoxW is now intercepted here too (matching
		// InfDefaultInstall): InstallHinfSectionW itself has no return value and never sets
		// GetLastError() to anything meaningful for [DefaultUninstall], so a suppressed error
		// dialog is the *only* signal available that the uninstall actually failed. Without this,
		// a broken [DefaultUninstall] section was previously always reported as success.
		// 

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach((void**)&real_MessageBoxW, DetourMessageBoxW); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

		const DWORD win32Error = GetLastError();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach((void**)&real_MessageBoxW, DetourMessageBoxW); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		if (g_MbCalled)
		{
			g_MbCalled = FALSE;

			std::string context = "InstallHinfSectionW";

			if (!g_LastMessageBoxText.empty())
			{
				context += std::format(" (dialog: \"{}\")",
				                      ConvertWideToANSI(g_LastMessageBoxCaption.empty()
					                                        ? g_LastMessageBoxText
					                                        : g_LastMessageBoxCaption + L": " + g_LastMessageBoxText));
			}

			//
			// See InfDefaultInstall's identical check: GetLastError() may still read
			// ERROR_SUCCESS here even though the intercepted dialog proves this failed, and
			// returning that as-is would report an "unexpected success" error code to callers
			// that propagate getErrorCode() as their own exit code.
			// 
			const DWORD effectiveError = (win32Error != ERROR_SUCCESS) ? win32Error : ERROR_FUNCTION_FAILED;

			return std::unexpected(Win32Error(effectiveError, context));
		}

		if (RebootRequired)
		{
			*RebootRequired = g_RestartDialogExCalled;
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_SECTION_NOT_FOUND));
}

template <nefarius::utilities::string_type StringType>
std::expected<std::vector<nefarius::devcon::FindByHwIdResult<StringType>>, Win32Error> nefarius::devcon::FindByHwId(
	const StringType& Matchstring)
{
	const std::wstring matchstring = ConvertToWide(Matchstring);

	DWORD total = 0;
	SP_DEVINFO_DATA spDevInfoData;

	std::vector<FindByHwIdResult<StringType>> results;

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
		nullptr,
		nullptr,
		nullptr,
		DIGCF_ALLCLASSES | DIGCF_PRESENT
	));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error("SetupDiGetClassDevs"));
	}

	spDevInfoData.cbSize = sizeof(spDevInfoData);

	for (DWORD devIndex = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), devIndex, &spDevInfoData); devIndex++)
	{
		const auto hwIdProperty = GetDeviceRegistryProperty(
			hDevInfo.get(),
			&spDevInfoData,
			SPDRP_HARDWAREID
		);

		if (!hwIdProperty)
		{
			continue;
		}

		LPWSTR hwIdsBuffer = (LPWSTR)hwIdProperty.value().Data.get();

		std::vector<std::wstring> entries;
		const WCHAR* p = hwIdsBuffer;

		while (*p)
		{
			entries.emplace_back(p);
			p += wcslen(p) + 1;
		}

		bool foundMatch = FALSE;

		for (auto& i : entries)
		{
			if (i.find(matchstring) != std::wstring::npos)
			{
				foundMatch = TRUE;
				break;
			}
		}

		// If we have a match, print out the whole array
		if (foundMatch)
		{
			total++;

			FindByHwIdResult<StringType> result;

			const auto descProperty = GetDeviceRegistryProperty(
				hDevInfo.get(),
				&spDevInfoData,
				SPDRP_DEVICEDESC
			);

			LPWSTR nameBuffer = NULL;
			LPCWSTR fallbackName = L"Unknown device";

			//
			// Try Device Description...
			// 
			if (!descProperty)
			{
				//
				// ...then Friendly Name
				// 
				const auto nameProperty = GetDeviceRegistryProperty(
					hDevInfo.get(),
					&spDevInfoData,
					SPDRP_FRIENDLYNAME
				);

				if (!nameProperty)
				{
					nameBuffer = (LPWSTR)fallbackName;
				}
				else
				{
					nameBuffer = (LPWSTR)nameProperty.value().Data.get();
				}				
			}
			else
			{
				nameBuffer = (LPWSTR)descProperty.value().Data.get();
			}

			const auto resultName = std::wstring(nameBuffer);

			if constexpr (std::is_same_v<StringType, std::wstring>)
			{
				result.HardwareIds = entries;
				result.Name = resultName;
			}
			else if constexpr (std::is_same_v<StringType, std::string>)
			{
				result.HardwareIds.reserve(entries.size());
				std::transform(entries.begin(), entries.end(), result.HardwareIds.begin(), ConvertWideToANSI);
				result.Name = ConvertToNarrow(resultName);
			}

			// Build a list of driver info items that we will retrieve below
			if (!SetupDiBuildDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER))
			{
				results.push_back(result);
				continue;
			}

			const auto driverGuard = sg::make_scope_guard([&hDevInfo, &spDevInfoData]() noexcept
			{
				SetupDiDestroyDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER);
			});

			SCOPE_GUARD_CAPTURE({
			                    SetupDiDestroyDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER);
			                    }, &hDevInfo, &spDevInfoData);

			// Get the first info item for this driver
			SP_DRVINFO_DATA drvInfo = {};
			drvInfo.cbSize = sizeof(SP_DRVINFO_DATA);

			if (!SetupDiEnumDriverInfo(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER, 0, &drvInfo))
			{
				results.push_back(result);
				continue;
			}			

			result.Version.Major = (drvInfo.DriverVersion >> 48) & 0xFFFF;
			result.Version.Minor = (drvInfo.DriverVersion >> 32) & 0xFFFF;
			result.Version.Build = (drvInfo.DriverVersion >> 16) & 0xFFFF;
			result.Version.Private = drvInfo.DriverVersion & 0x0000FFFF;

			results.push_back(result);
		}
	}

	return results;
}

template <nefarius::utilities::string_type StringType>
std::expected<nefarius::devcon::INFClassResult<StringType>, nefarius::utilities::Win32Error> nefarius::
devcon::GetINFClass(const StringType& InfPath)
{
	const std::wstring fullInfPath = ConvertToWide(InfPath);

	WCHAR normalisedInfPath[MAX_PATH] = {};

	const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

	if ((ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	GUID classGuid = {};
	std::wstring className(MAX_CLASS_NAME_LEN, L'\0');

	if (!SetupDiGetINFClassW(normalisedInfPath, &classGuid, className.data(), (DWORD)className.size(), NULL))
	{
		return std::unexpected(Win32Error("SetupDiGetINFClassW"));
	}

	StripNullCharacters(className);

	if constexpr (std::is_same_v<StringType, std::wstring>)
	{
		return INFClassResult<StringType>{classGuid, className};
	}
	else if constexpr (std::is_same_v<StringType, std::string>)
	{
		return INFClassResult<StringType>{classGuid, ConvertToNarrow(className)};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> nefarius::devcon::bluetooth::RestartBthUsbDevice(int instance)
{
	bool found = false;
	SP_DEVINFO_DATA spDevInfoData;

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
		&GUID_DEVCLASS_BLUETOOTH,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiGetClassDevs"));
	}

	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	if (!SetupDiEnumDeviceInfo(hDevInfo.get(), instance, &spDevInfoData))
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiEnumDeviceInfo"));
	}

	const auto enumeratorProperty = GetDeviceRegistryProperty(
		hDevInfo.get(),
		&spDevInfoData,
		SPDRP_ENUMERATOR_NAME
	);

	if (!enumeratorProperty)
	{
		return std::unexpected(enumeratorProperty.error());
	}

	const LPTSTR buffer = (LPTSTR)enumeratorProperty.value().Data.get();
	const size_t bufferLength = enumeratorProperty.value().Length;

	WideMultiStringArray enumerator(buffer, bufferLength);

	// if device found restart
	if (enumerator.contains(L"USB"))
	{
		if (!SetupDiRestartDevices(hDevInfo.get(), &spDevInfoData))
		{
			return std::unexpected(Win32Error(GetLastError(), "SetupDiRestartDevices"));
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_NOT_FOUND));
}

std::expected<void, Win32Error> nefarius::devcon::bluetooth::EnableDisableBthUsbDevice(bool state, int instance)
{
	SP_DEVINFO_DATA spDevInfoData = {};

	guards::HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
		&GUID_DEVCLASS_BLUETOOTH,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	));

	if (hDevInfo.is_invalid())
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiGetClassDevs"));
	}

	if (!SetupDiEnumDeviceInfo(hDevInfo.get(), instance, &spDevInfoData))
	{
		return std::unexpected(Win32Error(GetLastError(), "SetupDiEnumDeviceInfo"));
	}

	const auto enumeratorProperty = GetDeviceRegistryProperty(
		hDevInfo.get(),
		&spDevInfoData,
		SPDRP_ENUMERATOR_NAME
	);

	if (!enumeratorProperty)
	{
		return std::unexpected(enumeratorProperty.error());
	}

	const LPTSTR buffer = (LPTSTR)enumeratorProperty.value().Data.get();
	const size_t bufferLength = enumeratorProperty.value().Length;

	WideMultiStringArray enumerator(buffer, bufferLength);

	// if device found change it's state
	if (enumerator.contains(L"USB"))
	{
		SP_PROPCHANGE_PARAMS params;

		params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
		params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
		// ReSharper disable once CppAssignedValueIsNeverUsed
		params.Scope = DICS_FLAG_GLOBAL;
		// ReSharper disable once CppAssignedValueIsNeverUsed
		params.StateChange = (state) ? DICS_ENABLE : DICS_DISABLE;

		// setup proper parameters            
		if (!SetupDiSetClassInstallParams(hDevInfo.get(), &spDevInfoData, &params.ClassInstallHeader, sizeof(params)))
		{
			return std::unexpected(Win32Error(GetLastError(), "SetupDiSetClassInstallParams"));
		}

		// use parameters
		if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo.get(), &spDevInfoData))
		{
			return std::unexpected(Win32Error(GetLastError(), "SetupDiCallClassInstaller"));
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_NOT_FOUND));
}

std::expected<std::vector<nefarius::devcon::DriverStorePackage>, Win32Error>
nefarius::devcon::EnumerateDriverStorePackages()
{
	nefarius::utilities::DrvStore drvStore;

	if (!drvStore.fpDriverStoreOfflineEnumDriverPackageW)
	{
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION, "DriverStoreOfflineEnumDriverPackageW"));
	}

	WCHAR windowsDirectory[MAX_PATH] = {};

	if (GetWindowsDirectoryW(windowsDirectory, MAX_PATH) == 0)
	{
		return std::unexpected(Win32Error("GetWindowsDirectoryW"));
	}

	::EnumCollectContext ctx;

	const LONG status = drvStore.fpDriverStoreOfflineEnumDriverPackageW(
		&::EnumCollectCallback, &ctx, windowsDirectory);

	if (status < 0)
	{
		return std::unexpected(::NtStatusToWin32Error(drvStore, status, "DriverStoreOfflineEnumDriverPackageW"));
	}

	return std::move(ctx.Packages);
}

std::optional<std::wstring> nefarius::devcon::GetOriginalInfNameOfStorePackage(PCWSTR DriverPackageInfPath)
{
	std::wstring packageDirName(DriverPackageInfPath);
	const auto lastSeparator = packageDirName.find_last_of(L'\\');

	if (lastSeparator == std::wstring::npos)
	{
		return std::nullopt;
	}

	//
	// The staged copy of the INF lives inside its package directory, which is the component
	// right before the final one when the path ends in the INF file itself.
	// 
	const std::wstring lastComponent(packageDirName, lastSeparator + 1);

	if (lastComponent.size() >= 4 &&
		_wcsicmp(lastComponent.c_str() + lastComponent.size() - 4, L".inf") == 0)
	{
		packageDirName.erase(lastSeparator);

		const auto dirSeparator = packageDirName.find_last_of(L'\\');

		if (dirSeparator == std::wstring::npos)
		{
			return std::nullopt;
		}

		packageDirName.erase(0, dirSeparator + 1);
	}
	else
	{
		packageDirName.erase(0, lastSeparator + 1);
	}

	//
	// The original name is the prefix up to the last ".inf_" separator (mirrors the
	// greedy "(.+\.inf)_.+$" extraction used by DriverStoreExplorer).
	// 
	PCWSTR lastInfoSeparator = nullptr;
	PCWSTR cursor = packageDirName.c_str();

	while (const auto* occurrence = ::wstristr(cursor, L".inf_"))
	{
		lastInfoSeparator = occurrence;
		cursor = occurrence + 1;
	}

	//
	// Fail closed: the separator needs a non-empty prefix, and there must be at least one
	// character (architecture/hash) after it.
	// 
	if (lastInfoSeparator == nullptr || lastInfoSeparator == packageDirName.c_str() ||
		lastInfoSeparator + 5 >= packageDirName.c_str() + packageDirName.size())
	{
		return std::nullopt;
	}

	return packageDirName.substr(0,
		static_cast<size_t>(lastInfoSeparator - packageDirName.c_str()) + 4);
}

std::optional<nefarius::devcon::DriverStoreIdentity> nefarius::devcon::ReadDriverStoreIdentityFromInf(
	PCWSTR InfPath)
{
	guards::INFHandleGuard hInf(SetupOpenInfFileW(InfPath, nullptr, INF_STYLE_WIN4, nullptr));

	if (hInf.is_invalid())
	{
		return std::nullopt;
	}

	auto provider = ::ReadInfVersionField(hInf.get(), L"Provider");
	auto driverVer = ::ReadInfVersionField(hInf.get(), L"DriverVer");

	if (!provider || !driverVer || provider->empty() || driverVer->empty())
	{
		return std::nullopt;
	}

	return DriverStoreIdentity{std::move(*provider), std::move(*driverVer)};
}

bool nefarius::devcon::DriverStorePackageMatchesFilter(const DriverStorePackageDetails& Details,
                                                        const DriverStorePackageFilter& Filter)
{
	if (!Filter.OriginalInfNames.empty())
	{
		const bool nameMatches = Details.OriginalInfName && std::ranges::any_of(Filter.OriginalInfNames,
			[&](const std::wstring& name)
			{
				return _wcsicmp(Details.OriginalInfName->c_str(), name.c_str()) == 0;
			});

		if (!nameMatches)
		{
			return false;
		}
	}

	if (Filter.Provider && (!Details.Provider || _wcsicmp(Details.Provider->c_str(), Filter.Provider->c_str()) != 0))
	{
		return false;
	}

	if (Filter.DriverVer &&
		(!Details.DriverVer || _wcsicmp(Details.DriverVer->c_str(), Filter.DriverVer->c_str()) != 0))
	{
		return false;
	}

	if (Filter.ClassGuid && (!Details.ClassGuid || !IsEqualGUID(*Details.ClassGuid, *Filter.ClassGuid)))
	{
		return false;
	}

	if (Filter.ServiceName)
	{
		const bool serviceMatches = std::ranges::any_of(Details.ServiceNames,
			[&](const std::wstring& name)
			{
				return _wcsicmp(name.c_str(), Filter.ServiceName->c_str()) == 0;
			});

		if (!serviceMatches)
		{
			return false;
		}
	}

	return true;
}

namespace
{
	using nefarius::devcon::DriverStorePackage;
	using nefarius::devcon::DriverStorePackageDetails;
	using nefarius::devcon::DriverStorePackageFilter;

	bool FilterIsEmpty(const DriverStorePackageFilter& filter)
	{
		return !filter.ClassGuid && !filter.ServiceName && !filter.Provider && !filter.DriverVer &&
			filter.OriginalInfNames.empty();
	}

	//
	// Checks a single criterion of Details against Filter via the shared, pure
	// DriverStorePackageMatchesFilter (constructing a filter containing just that one field), so
	// match semantics have exactly one implementation. On mismatch, records Reason for
	// diagnostics and returns false; on a vacuous criterion (unset in Filter), returns true
	// without touching Reason.
	// 
	template <typename FieldSetter>
	bool CheckCriterion(const DriverStorePackageDetails& details, FieldSetter setField, const char* reason,
	                    std::string& rejectReason)
	{
		nefarius::devcon::DriverStorePackageFilter singleFieldFilter;
		setField(singleFieldFilter);

		if (nefarius::devcon::DriverStorePackageMatchesFilter(details, singleFieldFilter))
		{
			return true;
		}

		rejectReason = reason;
		return false;
	}

	//
	// Evaluates a candidate package against a filter, from cheapest to most expensive criterion,
	// so a name mismatch (pure string comparison) never pays for an INF open, and a
	// Provider/DriverVer mismatch never pays for the class/service-target parse. Populates as
	// much of Details as was actually read along the way, even on a non-match, so a caller (e.g.
	// diagnostics) can report exactly what was found. Delegates the actual match decision for
	// each criterion to DriverStorePackageMatchesFilter (via CheckCriterion above) so this
	// function's only responsibility is resolving I/O-dependent fields in cost order; a field
	// that is populated in filter but unreadable for this candidate is left unset on Details,
	// which DriverStorePackageMatchesFilter itself treats as a rejection.
	// 
	bool MatchesFilter(const DriverStorePackage& candidate, const DriverStorePackageFilter& filter,
	                   DriverStorePackageDetails& details, std::string& rejectReason)
	{
		details.DriverPackageInfPath = candidate.DriverPackageInfPath;
		details.PublishedInfName = candidate.PublishedInfName;
		details.OriginalInfName = nefarius::devcon::GetOriginalInfNameOfStorePackage(
			candidate.DriverPackageInfPath.c_str());

		if (!CheckCriterion(details, [&](DriverStorePackageFilter& f) { f.OriginalInfNames = filter.OriginalInfNames; },
		                   "original INF name mismatch", rejectReason))
		{
			return false;
		}

		if (filter.Provider || filter.DriverVer)
		{
			if (const auto identity = nefarius::devcon::ReadDriverStoreIdentityFromInf(
				candidate.DriverPackageInfPath.c_str()))
			{
				details.Provider = identity->Provider;
				details.DriverVer = identity->DriverVer;
			}

			if (!CheckCriterion(details, [&](DriverStorePackageFilter& f) { f.Provider = filter.Provider; },
			                   "Provider mismatch (or unreadable)", rejectReason))
			{
				return false;
			}

			if (!CheckCriterion(details, [&](DriverStorePackageFilter& f) { f.DriverVer = filter.DriverVer; },
			                   "DriverVer mismatch (or unreadable)", rejectReason))
			{
				return false;
			}
		}

		if (filter.ClassGuid)
		{
			if (const auto classResult = nefarius::devcon::GetINFClass(candidate.DriverPackageInfPath))
			{
				details.ClassGuid = classResult->ClassGUID;
			}

			if (!CheckCriterion(details, [&](DriverStorePackageFilter& f) { f.ClassGuid = filter.ClassGuid; },
			                   "device setup class mismatch (or unreadable)", rejectReason))
			{
				return false;
			}
		}

		if (filter.ServiceName)
		{
			if (const auto targets = nefarius::devcon::GetInfClassFilterTargets(candidate.DriverPackageInfPath))
			{
				for (const auto& target : targets.value())
				{
					details.ServiceNames.push_back(target.ServiceName);
				}
			}

			if (!CheckCriterion(details, [&](DriverStorePackageFilter& f) { f.ServiceName = filter.ServiceName; },
			                   "filter service name mismatch (or unreadable)", rejectReason))
			{
				return false;
			}
		}

		return true;
	}

	//
	// Shared delete cascade for an already-matched package: DriverStoreOfflineDeleteDriverPackageW
	// -> SetupUninstallOEMInfW -> DiUninstallDriverW, in that order of preference. Unlike the
	// bare-fallback path taken when no package could be identified up front, the final
	// DiUninstallDriverW call here is handed the matched package's own store path rather than the
	// caller's original INF path - the two can differ (e.g. differing case, or the original file
	// having since moved/changed), and only the store path is guaranteed to still refer to the
	// package that was actually matched and is being removed.
	// 
	std::expected<void, Win32Error> RemoveMatchedPackage(const DriverStorePackageDetails& package,
	                                                      bool* rebootRequired)
	{
		nefarius::utilities::DrvStore drvStore;
		std::optional<Win32Error> driverStoreDeleteError;

		if (drvStore.fpDriverStoreOfflineDeleteDriverPackageW)
		{
			WCHAR windowsDirectory[MAX_PATH] = {};

			if (GetWindowsDirectoryW(windowsDirectory, MAX_PATH) != 0)
			{
				std::wstring driveRoot(windowsDirectory);

				if (driveRoot.size() > 3)
				{
					driveRoot.resize(3);
				}

				const LONG status = drvStore.fpDriverStoreOfflineDeleteDriverPackageW(
					package.DriverPackageInfPath.c_str(), 0, nullptr, windowsDirectory, driveRoot.c_str());

				if (status >= 0)
				{
					return {};
				}

				//
				// Fall through to SetupUninstallOEMInfW below, reusing the published name already
				// resolved by the enumeration above. Keep the original failure around in case
				// every fallback also fails, so it isn't lost behind a possibly-unrelated
				// GetLastError() from a later API.
				// 
				driverStoreDeleteError = ::NtStatusToWin32Error(
					drvStore, status, "DriverStoreOfflineDeleteDriverPackageW");
			}
		}

		if (!package.PublishedInfName.empty() &&
			SetupUninstallOEMInfW(package.PublishedInfName.c_str(), SUOI_FORCEDELETE, nullptr))
		{
			return {};
		}

		//
		// Last resort: still identifies the package by its matched store path (see function
		// comment), but will also uninstall any device still actively using this driver.
		// 
		Newdev newdev;
		BOOL reboot = FALSE;

		switch (newdev.CallFunction(newdev.fpDiUninstallDriverW, nullptr,
			package.DriverPackageInfPath.c_str(), 0, &reboot))
		{
		case FunctionCallResult::NotAvailable:
			return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
				                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
				                       : Win32Error(ERROR_PROC_NOT_FOUND, "DiUninstallDriverW export not found"));
		case FunctionCallResult::Failure:
			if (driverStoreDeleteError)
			{
				return std::unexpected(Win32Error(driverStoreDeleteError->getErrorCode(),
					std::format("DiUninstallDriverW (after {})", driverStoreDeleteError->getErrorMessageA())));
			}
			return std::unexpected(Win32Error("DiUninstallDriverW"));
		case FunctionCallResult::Success:
			if (rebootRequired)
			{
				*rebootRequired = *rebootRequired || (reboot > 0);
			}
			return {};
		}

		return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
	}
}

std::expected<std::vector<nefarius::devcon::DriverStorePackageDetails>, Win32Error>
nefarius::devcon::FindDriverStorePackages(const DriverStorePackageFilter& Filter)
{
	if (::FilterIsEmpty(Filter))
	{
		return std::unexpected(Win32Error(ERROR_INVALID_PARAMETER,
			"FindDriverStorePackages requires at least one filter criterion"));
	}

	const auto packages = EnumerateDriverStorePackages();

	if (!packages)
	{
		return std::unexpected(packages.error());
	}

	std::vector<DriverStorePackageDetails> matches;

	for (const auto& candidate : packages.value())
	{
		DriverStorePackageDetails details;
		std::string rejectReason;

		if (::MatchesFilter(candidate, Filter, details, rejectReason))
		{
			::EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Progress, "FindDriverStorePackages",
			          details.DriverPackageInfPath, std::nullopt, "candidate matched filter");
			matches.push_back(std::move(details));
		}
		else if (DiagnosticsEnabled())
		{
			::EmitDiag(DiagnosticLevel::Verbose, DiagnosticPhase::Progress, "FindDriverStorePackages",
			          details.DriverPackageInfPath, std::nullopt,
			          std::format("candidate rejected: {}", rejectReason));
		}
	}

	return matches;
}

std::expected<std::vector<nefarius::devcon::DriverStorePackageRemoval>, Win32Error>
nefarius::devcon::RemoveDriverStorePackages(const DriverStorePackageFilter& Filter, bool* RebootRequired)
{
	const auto packages = FindDriverStorePackages(Filter);

	if (!packages)
	{
		return std::unexpected(packages.error());
	}

	std::vector<DriverStorePackageRemoval> results;
	results.reserve(packages->size());
	bool anyRebootRequired = false;

	for (auto& package : packages.value())
	{
		DriverStorePackageRemoval removal;
		removal.Package = package;

		if (const auto removeResult = ::RemoveMatchedPackage(package, &removal.RebootRequired); removeResult)
		{
			removal.Removed = true;
			::EmitDiag(DiagnosticLevel::Info, DiagnosticPhase::End, "RemoveDriverStorePackages",
			          package.DriverPackageInfPath, std::nullopt, "package removed successfully");
		}
		else
		{
			removal.Error = removeResult.error();
			::EmitDiag(DiagnosticLevel::Warning, DiagnosticPhase::Failure, "RemoveDriverStorePackages",
			          package.DriverPackageInfPath, removeResult.error().getErrorCode(),
			          std::format("failed to remove package: {}", removeResult.error().getErrorMessageA()));
		}

		anyRebootRequired = anyRebootRequired || removal.RebootRequired;

		results.push_back(std::move(removal));
	}

	//
	// Accumulated locally rather than read-modify-written directly on *RebootRequired, since the
	// out parameter's initial contents are the caller's responsibility and must never be read.
	// 
	if (RebootRequired)
	{
		*RebootRequired = anyRebootRequired;
	}

	return results;
}

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::devcon::RemoveDriverStorePackage(
	const StringType& FullInfPath, bool* RebootRequired)
{
	const std::wstring fullInfPath = ConvertToWide(FullInfPath);

	WCHAR normalisedInfPath[MAX_PATH] = {};

	if (const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, nullptr);
		(ret >= MAX_PATH) || (ret == FALSE))
	{
		return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
	}

	//
	// Surgical path: identify the target package by its [Version] identity plus the base name of
	// the original INF file it was staged from, then delegate to RemoveDriverStorePackages with
	// an exact filter built from that identity, which finds and deletes exactly that package
	// without touching any device node.
	// 
	if (const auto targetIdentity = ReadDriverStoreIdentityFromInf(normalisedInfPath))
	{
		//
		// Base name of the original INF file the caller wants purged; packages may share its
		// [Version] identity while originating from different INF files.
		// 
		const std::wstring normalisedPath(normalisedInfPath);
		const auto separator = normalisedPath.find_last_of(L'\\');
		const std::wstring targetOriginalName = separator != std::wstring::npos
			? normalisedPath.substr(separator + 1)
			: normalisedPath;

		DriverStorePackageFilter filter;
		filter.OriginalInfNames = {targetOriginalName};
		filter.Provider = targetIdentity->Provider;
		filter.DriverVer = targetIdentity->DriverVer;

		if (const auto results = RemoveDriverStorePackages(filter, RebootRequired))
		{
			if (results->empty())
			{
				//
				// No matching package in the store; either it was never published this way, or
				// has already been purged. Either way, there is nothing left to remove.
				// 
				return {};
			}

			//
			// The filter above (identity + original name) identifies at most one real-world
			// package; take its outcome directly to preserve this function's single-package
			// contract.
			// 
			const auto& only = results->front();

			if (only.Removed)
			{
				return {};
			}

			return std::unexpected(*only.Error);
		}

		//
		// The store couldn't even be enumerated; fall through to the bare DiUninstallDriverW
		// fallback below, same as when no identity could be read at all.
		// 
	}

	//
	// No identity could be read from the original INF at all, or the store couldn't be
	// enumerated; fall back straight to DiUninstallDriverW, which (unlike the surgical path
	// above) will also uninstall any device still actively using this driver.
	// 
	Newdev newdev;
	BOOL reboot = FALSE;

	switch (newdev.CallFunction(newdev.fpDiUninstallDriverW, nullptr, normalisedInfPath, 0, &reboot))
	{
	case FunctionCallResult::NotAvailable:
		return std::unexpected(newdev.GetLoadError() != ERROR_SUCCESS
			                       ? Win32Error(newdev.GetLoadError(), "Failed to load Newdev.dll")
			                       : Win32Error(ERROR_PROC_NOT_FOUND, "DiUninstallDriverW export not found"));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error("DiUninstallDriverW"));
	case FunctionCallResult::Success:
		if (RebootRequired)
		{
			*RebootRequired = reboot > 0;
		}
		return {};
	}

	return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

template
std::expected<void, Win32Error> nefarius::devcon::RemoveDriverStorePackage(
	const std::wstring& FullInfPath, bool* RebootRequired);

template
std::expected<void, Win32Error> nefarius::devcon::RemoveDriverStorePackage(
	const std::string& FullInfPath, bool* RebootRequired);
