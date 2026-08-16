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
		explicit DllHelper(LPCTSTR filename) : _module(LoadLibrary(filename))
		{
		}

		~DllHelper() { FreeLibrary(_module); }

		ProcPtr operator[](LPCSTR proc_name) const
		{
			return ProcPtr(GetProcAddress(_module, proc_name));
		}

		static HMODULE _parent_module;

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
		DllHelper _dll{L"drvstore.dll"};
		DllHelper _ntdll{L"ntdll.dll"};

	public:
		DriverStoreOfflineEnumDriverPackageW_t* fpDriverStoreOfflineEnumDriverPackageW = _dll[
			"DriverStoreOfflineEnumDriverPackageW"];
		DriverStoreOfflineDeleteDriverPackageW_t* fpDriverStoreOfflineDeleteDriverPackageW = _dll[
			"DriverStoreOfflineDeleteDriverPackageW"];
		RtlNtStatusToDosError_t* fpRtlNtStatusToDosError = _ntdll["RtlNtStatusToDosError"];
	};
}
