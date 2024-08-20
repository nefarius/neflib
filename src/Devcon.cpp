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
		(void)SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
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
			return std::unexpected(Win32Error(ERROR_NOT_FOUND, "SetupDiGetDeviceRegistryProperty"));
		}

		//
		// Unexpected status other than required size
		// 
		if (win32Error != ERROR_INSUFFICIENT_BUFFER)
		{
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
		}

		auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(sizeRequired);

		//
		// Query property value
		// 
		if (!SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
		                                      DeviceInfoData,
		                                      Property,
		                                      &propertyRegDataType,
		                                      buffer.get(),
		                                      sizeRequired,
		                                      &sizeRequired))
		{
			win32Error = GetLastError();
			buffer.release();
			return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
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

	std::expected<void, Win32Error> uninstall_device_and_driver(
		HDEVINFO hDevInfo, PSP_DEVINFO_DATA spDevInfoData, bool* rebootRequired)
	{
		BOOL drvNeedsReboot = FALSE, devNeedsReboot = FALSE;
		DWORD requiredBufferSize = 0;
		Newdev newdev;

		if (!newdev.fpDiUninstallDevice || !newdev.fpDiUninstallDriverW)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
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

		if (!SetupDiEnumDriverInfo(
			hDevInfo,
			spDevInfoData,
			SPDIT_COMPATDRIVER,
			0, // One result expected
			&drvInfoData
		))
		{
			return std::unexpected(Win32Error("SetupDiEnumDriverInfo"));
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

		const auto driverGuard = sg::make_scope_guard([hDevInfo, spDevInfoData]() noexcept
		{
			//
			// SetupDiGetDriverInfoDetail allocated memory we need to explicitly free again
			// 
			SetupDiDestroyDriverInfoList(
				hDevInfo,
				spDevInfoData,
				SPDIT_COMPATDRIVER
			);
		});

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
		UNREFERENCED_PARAMETER(lpText);
		UNREFERENCED_PARAMETER(lpCaption);
		UNREFERENCED_PARAMETER(uType);

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
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
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
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
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
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error(GetLastError()));
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
		for (LPWSTR p = buffer; p && *p && (p < &buffer[hwIdBuffer.value().Length]); p += lstrlenW(p) + sizeof(TCHAR))
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

		LocalFree(buffer);
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

		InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach((void**)&real_MessageBoxW, DetourMessageBoxW); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		//
		// If a message box call was intercepted, we encountered an error
		// 
		if (g_MbCalled)
		{
			g_MbCalled = FALSE;
			return std::unexpected(Win32Error(ERROR_PNP_REBOOT_REQUIRED, "InstallHinfSectionW"));
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
		return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
	case FunctionCallResult::Failure:
		return std::unexpected(Win32Error());
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

		//
		// Some implementations are bugged and do not respect the non-interactive flags,
		// so we catch the use of common dialog APIs and nullify their impact :)
		// 

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx); // NOLINT(clang-diagnostic-microsoft-cast)
		DetourTransactionCommit();

		if (RebootRequired)
		{
			*RebootRequired = g_RestartDialogExCalled;
		}

		return {};
	}

	return std::unexpected(Win32Error(ERROR_SECTION_NOT_FOUND));
}

template <nefarius::utilities::string_type StringType>
std::expected<std::vector<nefarius::devcon::FindByHwIdResult>, Win32Error> nefarius::devcon::FindByHwId(
	const StringType& Matchstring)
{
	const std::wstring matchstring = ConvertToWide(Matchstring);

	DWORD total = 0;
	SP_DEVINFO_DATA spDevInfoData;

	std::vector<FindByHwIdResult> results;

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

		LPTSTR hwIdsBuffer = (LPTSTR)hwIdProperty.value().Data.get();

		std::vector<std::wstring> entries;
		const TCHAR* p = hwIdsBuffer;

		while (*p)
		{
			entries.emplace_back(p);
			p += _tcslen(p) + 1;
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

			FindByHwIdResult result{entries};

			const auto descProperty = GetDeviceRegistryProperty(
				hDevInfo.get(),
				&spDevInfoData,
				SPDRP_DEVICEDESC
			);

			LPTSTR nameBuffer = NULL;

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
					continue;
				}

				nameBuffer = (LPTSTR)nameProperty.value().Data.get();
			}
			else
			{
				nameBuffer = (LPTSTR)descProperty.value().Data.get();
			}

			result.Name = std::wstring(nameBuffer);

			// Build a list of driver info items that we will retrieve below
			if (!SetupDiBuildDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER))
			{
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
		std::unexpected(Win32Error(GetLastError(), "SetupDiEnumDeviceInfo"));
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
			std::unexpected(Win32Error(GetLastError(), "SetupDiRestartDevices"));
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
