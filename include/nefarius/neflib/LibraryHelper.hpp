#pragma once


namespace nefarius::utilities
{
	// Enum to represent the result of the function call
	enum class FunctionCallResult
	{
		NotAvailable,
		Failure,
		Success
	};

	class ProcPtr
	{
	public:
		explicit ProcPtr(FARPROC ptr) : _ptr(ptr)
		{
		}

		template <typename T, typename = std::enable_if_t<std::is_function_v<T>>>
		operator T*() const
		{
			return reinterpret_cast<T*>(_ptr);
		}

	private:
		FARPROC _ptr;
	};

	class DllHelper
	{
	public:
		explicit DllHelper(LPCTSTR filename) : _module(LoadLibrary(filename)),
		                                       _loadError(_module ? ERROR_SUCCESS : GetLastError())
		{
		}

		~DllHelper() { FreeLibrary(_module); }

		ProcPtr operator[](LPCSTR proc_name) const
		{
			return ProcPtr(GetProcAddress(_module, proc_name));
		}

		//
		// GetLastError() captured immediately after LoadLibrary, so a caller whose function
		// pointer(s) came back null can tell "the DLL itself couldn't be loaded" (e.g.
		// ERROR_MOD_NOT_FOUND on an OS missing this optional component) apart from "the DLL
		// loaded fine but doesn't export this particular function" - both currently collapse into
		// the same generic ERROR_INVALID_FUNCTION at call sites otherwise.
		//
		[[nodiscard]] DWORD GetLoadError() const { return _loadError; }

		[[nodiscard]] bool IsLoaded() const { return _module != nullptr; }

		static HMODULE _parent_module;

	private:
		HMODULE _module;
		DWORD _loadError;
	};

	//
	// Like DllHelper, but restricts the search to %SystemRoot%\System32 (LOAD_LIBRARY_SEARCH_SYSTEM32),
	// so a module that isn't already loaded elsewhere in the process (unlike ntdll.dll) can't be
	// planted/hijacked from the application directory or another entry of the default DLL search order.
	//
	class DllHelperSystem32
	{
	public:
		explicit DllHelperSystem32(LPCWSTR filename) : _module(
			LoadLibraryExW(filename, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
		{
		}

		~DllHelperSystem32()
		{
			if (_module)
			{
				FreeLibrary(_module);
			}
		}

		DllHelperSystem32(const DllHelperSystem32&) = delete;
		DllHelperSystem32& operator=(const DllHelperSystem32&) = delete;

		ProcPtr operator[](LPCSTR proc_name) const
		{
			return ProcPtr(_module ? GetProcAddress(_module, proc_name) : nullptr);
		}

	private:
		HMODULE _module;
	};

	class Newdev
	{
	private:
		DllHelper _dll{L"Newdev.dll"};

	public:
		decltype(DiUninstallDriverW)* fpDiUninstallDriverW = _dll["DiUninstallDriverW"];
		decltype(DiInstallDriverW)* fpDiInstallDriverW = _dll["DiInstallDriverW"];
		decltype(DiUninstallDevice)* fpDiUninstallDevice = _dll["DiUninstallDevice"];
		decltype(UpdateDriverForPlugAndPlayDevicesW)* fpUpdateDriverForPlugAndPlayDevicesW = _dll[
			"UpdateDriverForPlugAndPlayDevicesW"];

		// Wrapper function to handle the function call and return the result
		template <typename Func, typename... Args>
		FunctionCallResult CallFunction(Func func, Args... args)
		{
			if (!func)
			{
				return FunctionCallResult::NotAvailable;
			}

			const auto ret = func(args...);
			return ret ? FunctionCallResult::Success : FunctionCallResult::Failure;
		}

		//
		// GetLastError() captured when Newdev.dll itself failed to load; ERROR_SUCCESS if it
		// loaded fine (in which case a NotAvailable CallFunction result means this particular
		// export is simply missing from the loaded Newdev.dll, e.g. on a very old OS). Lets a
		// caller building the resulting Win32Error distinguish the two instead of always
		// reporting the generic ERROR_INVALID_FUNCTION.
		//
		[[nodiscard]] DWORD GetLoadError() const { return _dll.GetLoadError(); }
	};

	//
	// drvstore.dll and ntdll.dll's RtlNtStatusToDosError are undocumented; unlike Newdev.dll
	// there is no SDK header to decltype() their exports from, so the function types are spelled
	// out explicitly. Signatures/behavior are derived from the working sample at
	// https://github.com/nefarius/Nefarius.Utilities.DeviceManagement (src/Drivers/DriverStore.cs).
	//
	using DriverStoreOfflineEnumDriverPackageCallback_t = int(WINAPI)(PCWSTR DriverPackageInfPath, PVOID EnumInfo,
	                                                                  PVOID Context);
	using DriverStoreOfflineEnumDriverPackageW_t = LONG(WINAPI)(
		DriverStoreOfflineEnumDriverPackageCallback_t* Callback, PVOID Context, PCWSTR TargetSystemRoot);
	using DriverStoreOfflineDeleteDriverPackageW_t = LONG(WINAPI)(PCWSTR DriverPackageInfPath, ULONG Flags,
	                                                              PVOID Reserved, PCWSTR TargetSystemRoot,
	                                                              PCWSTR TargetSystemDrive);
	using RtlNtStatusToDosError_t = ULONG(WINAPI)(LONG Status);

	//
	// Mirrors drvstore.dll's (undocumented) DriverStoreOfflineEnumDriverPackageInfoW. The exact,
	// unpadded byte layout matters since this is passed across the ABI boundary by the OS: 4
	// (InboxInf) + 2 (ProcessorArchitecture) + 85*2 (LocaleName) + 260*2 (PublishedInfName) = 696
	// = 0x2B8 bytes, matching the C# reference's `[StructLayout(..., Size = 0x2B8, Pack = 0x4)]`.
	//
#pragma pack(push, 4)
	struct DriverStoreOfflineEnumDriverPackageInfoW
	{
		LONG InboxInf;
		USHORT ProcessorArchitecture;
		WCHAR LocaleName[85];
		WCHAR PublishedInfName[260];
	};
#pragma pack(pop)

	static_assert(sizeof(DriverStoreOfflineEnumDriverPackageInfoW) == 0x2B8,
	             "DriverStoreOfflineEnumDriverPackageInfoW layout must match the drvstore.dll ABI");

	class DrvStore
	{
	private:
		DllHelperSystem32 _dll{L"drvstore.dll"};
		DllHelper _ntdll{L"ntdll.dll"};

	public:
		DriverStoreOfflineEnumDriverPackageW_t* fpDriverStoreOfflineEnumDriverPackageW = _dll[
			"DriverStoreOfflineEnumDriverPackageW"];
		DriverStoreOfflineDeleteDriverPackageW_t* fpDriverStoreOfflineDeleteDriverPackageW = _dll[
			"DriverStoreOfflineDeleteDriverPackageW"];
		RtlNtStatusToDosError_t* fpRtlNtStatusToDosError = _ntdll["RtlNtStatusToDosError"];
	};
}
