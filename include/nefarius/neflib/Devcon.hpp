// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/AnyString.hpp>
#include <nefarius/neflib/Win32Error.hpp>
#include <nefarius/neflib/MultiStringArray.hpp>

namespace nefarius::devcon
{
	template <nefarius::utilities::string_type StringType>
	struct FindByHwIdResult
	{
		std::vector<StringType> HardwareIds;

		StringType Name;

		union
		{
			struct
			{
				uint16_t Major;
				uint16_t Minor;
				uint16_t Build;
				uint16_t Private;
			};

			uint64_t Value;
		} Version;
	};

	template <nefarius::utilities::string_type StringType>
	struct INFClassResult
	{
		GUID ClassGUID;

		StringType ClassName;
	};

	/**
	 * Creates a new root-enumerated device node for a driver to load on to.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	06.08.2024
	 *
	 * @param 	ClassName 	Name of the device class (System, HIDClass, USB, etc.).
	 * @param 	ClassGuid 	Unique identifier for the device class.
	 * @param 	HardwareId	The Hardware ID to set.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> Create(const StringType& ClassName, const GUID* ClassGuid,
	                                                            const nefarius::utilities::WideMultiStringArray&
	                                                            HardwareId);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::Create(
		const std::wstring& ClassName, const GUID* ClassGuid,
		const nefarius::utilities::WideMultiStringArray& HardwareId);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::Create(
		const std::string& ClassName, const GUID* ClassGuid,
		const nefarius::utilities::WideMultiStringArray& HardwareId);

	/**
	 * Triggers a driver update on all devices matching a given hardware ID with using the provided INF.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	07.08.2024
	 *
	 * @param 		  	HardwareId	  	The Hardware ID of the devices to affect.
	 * @param 		  	FullInfPath   	Full pathname to the INF file.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required.
	 * @param 		  	Force		  	(Optional) True to force.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> Update(const StringType& HardwareId,
	                                                            const StringType& FullInfPath, bool* RebootRequired,
	                                                            bool Force = false);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::Update(const std::wstring& HardwareId,
		const std::wstring& FullInfPath,
		bool* RebootRequired, bool Force);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::Update(const std::string& HardwareId,
		const std::string& FullInfPath,
		bool* RebootRequired, bool Force);

	/**
     * Installs a given driver into the driver store.
     *
     * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
     * @date	07.08.2024
     *
     * @param 		  	FullInfPath   	Full pathname of the INF file.
     * @param [in,out]	RebootRequired	If non-null, true if reboot required.
     *
     * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
     */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> InstallDriver(const StringType& FullInfPath,
	                                                                   bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InstallDriver(
		const std::wstring& FullInfPath,
		bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InstallDriver(const std::string& FullInfPath,
		bool* RebootRequired);

	/**
	 * Uninstalls a given driver.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	07.08.2024
	 *
	 * @param 		  	FullInfPath   	Full pathname of the INF file.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> UninstallDriver(const StringType& FullInfPath,
	                                                                     bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::UninstallDriver(
		const std::wstring& FullInfPath,
		bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::UninstallDriver(
		const std::string& FullInfPath,
		bool* RebootRequired);

	/**
	 * Uninstalls all devices and active function driver matched by provided device class and
	 * Hardware ID.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 *
	 * @param 		  	ClassGuid	  	Device class GUID.
	 * @param 		  	HardwareId	  	Identifier for the hardware.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required.
	 *
	 * @returns	A std::vector&lt;std::expected&lt;void,nefarius::utilities::Win32Error&gt;&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::vector<std::expected<void, nefarius::utilities::Win32Error>> UninstallDeviceAndDriver(
		const GUID* ClassGuid, const StringType& HardwareId, bool* RebootRequired);

	template
	std::vector<std::expected<void, nefarius::utilities::Win32Error>> nefarius::devcon::UninstallDeviceAndDriver(
		const GUID* ClassGuid, const std::wstring& HardwareId, bool* RebootRequired);

	template
	std::vector<std::expected<void, nefarius::utilities::Win32Error>> nefarius::devcon::UninstallDeviceAndDriver(
		const GUID* ClassGuid, const std::string& HardwareId, bool* RebootRequired);

	/**
	 * Installs a primitive driver.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 *
	 * @param 		  	FullInfPath   	Full pathname of the INF file.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required.
	 *
	 * @returns	A std::expected&lt;void,nefarius::utilities::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> InfDefaultInstall(const StringType& FullInfPath,
	                                                                       bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InfDefaultInstall(
		const std::wstring& FullInfPath, bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InfDefaultInstall(
		const std::string& FullInfPath, bool* RebootRequired);

	/**
	 * Uninstalls a primitive driver.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 *
	 * @param 		  	FullInfPath   	Full pathname of the INF file.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required.
	 *
	 * @returns	A std::expected&lt;void,nefarius::utilities::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> InfDefaultUninstall(
		const StringType& FullInfPath, bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InfDefaultUninstall(
		const std::wstring& FullInfPath,
		bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::InfDefaultUninstall(
		const std::string& FullInfPath,
		bool* RebootRequired);

	/**
	 * Searches for devices matched by Hardware ID and returns a list of Hardware IDs, friendly
	 * names and driver version information.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 *
	 * @param 	Matchstring	The partial string to search for.
	 *
	 * @returns	True if at least one match was found, false otherwise.
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<std::vector<nefarius::devcon::FindByHwIdResult<StringType>>, nefarius::utilities::Win32Error>
	FindByHwId(
		const StringType& Matchstring);

	template
	std::expected<std::vector<nefarius::devcon::FindByHwIdResult<std::wstring>>, nefarius::utilities::Win32Error>
	nefarius::devcon::FindByHwId(
		const std::wstring& Matchstring);

	template
	std::expected<std::vector<nefarius::devcon::FindByHwIdResult<std::string>>, nefarius::utilities::Win32Error>
	nefarius::devcon::FindByHwId(
		const std::string& Matchstring);

	template <nefarius::utilities::string_type StringType>
	std::expected<nefarius::devcon::INFClassResult<StringType>, nefarius::utilities::Win32Error>
	GetINFClass(const StringType& InfPath);

	template
	std::expected<nefarius::devcon::INFClassResult<std::wstring>, nefarius::utilities::Win32Error>
	GetINFClass(const std::wstring& InfPath);

	template
	std::expected<nefarius::devcon::INFClassResult<std::string>, nefarius::utilities::Win32Error>
	GetINFClass(const std::string& InfPath);

	namespace bluetooth
	{
		std::expected<void, nefarius::utilities::Win32Error> RestartBthUsbDevice(int instance = 0);

		std::expected<void, nefarius::utilities::Win32Error> EnableDisableBthUsbDevice(bool state, int instance = 0);
	}

	/**
	 * A single driver package published into the Windows driver store.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	16.08.2026
	 */
	struct DriverStorePackage
	{
		/// Absolute path of this package's INF copy inside the driver store
		std::wstring DriverPackageInfPath;
		/// Published name in %WINDIR%\INF, e.g. "oem12.inf"
		std::wstring PublishedInfName;
		/// True for driver packages that ship inbox with Windows itself
		bool IsInbox = false;
		/// Processor architecture the package was published for
		unsigned short ProcessorArchitecture = 0;
		/// Locale the package was published for
		std::wstring LocaleName;
	};

	/**
	 * Enumerates every non-inbox driver package currently published in the local driver store.
	 * Uses the undocumented drvstore.dll offline enumeration API; fails with
	 * ERROR_INVALID_FUNCTION if drvstore.dll or the export it needs isn't available.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	16.08.2026
	 *
	 * @returns	A std::expected&lt;std::vector&lt;DriverStorePackage&gt;,nefarius::utilities::Win32Error&gt;
	 */
	std::expected<std::vector<DriverStorePackage>, nefarius::utilities::Win32Error> EnumerateDriverStorePackages();

	/**
	 * Derives the base name of the original INF file a driver store package was staged from, out
	 * of the package's FileRepository directory name; e.g.
	 * "...\FileRepository\example1.inf_amd64_5ca6d479976bcd98\example1.inf" yields
	 * "example1.inf". Distinguishes packages whose [Version] identity collides (same Provider +
	 * DriverVer) but which originate from different INF files, which the published oemNN.inf
	 * naming scheme alone cannot. Returns std::nullopt if the directory name doesn't follow the
	 * "&lt;originalInfName&gt;_&lt;arch&gt;_&lt;hash&gt;" layout, so callers can fail closed.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 	DriverPackageInfPath	Absolute path of a package's INF copy inside the driver
	 * 									store, e.g. a DriverStorePackage::DriverPackageInfPath.
	 *
	 * @returns	The original INF base name, or std::nullopt if it couldn't be determined.
	 */
	std::optional<std::wstring> GetOriginalInfNameOfStorePackage(PCWSTR DriverPackageInfPath);

	/**
	 * Lightweight [Version] section identity of an INF file, used to match an original INF
	 * against its published copy inside the driver store without relying on the published
	 * oemNN.inf naming scheme.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	struct DriverStoreIdentity
	{
		std::wstring Provider;
		std::wstring DriverVer;
	};

	/**
	 * Reads the Provider and DriverVer fields out of an INF file's [Version] section. Works on
	 * both original INF files and driver store copies, since both are ordinary INF files as far
	 * as SetupAPI is concerned. Returns std::nullopt if the INF can't be opened, or either field
	 * is missing/empty (not every INF sets both).
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 	InfPath	Full pathname of the INF file to read.
	 *
	 * @returns	The INF's [Version] identity, or std::nullopt if it couldn't be determined.
	 */
	std::optional<DriverStoreIdentity> ReadDriverStoreIdentityFromInf(PCWSTR InfPath);

	/**
	 * Criteria used to select one or more driver store packages via FindDriverStorePackages /
	 * RemoveDriverStorePackages. Every populated field must match a candidate package (logical
	 * AND); a package failing to yield a value for a populated field (e.g. its class can't be
	 * determined) is treated as a non-match rather than skipping that criterion. At least one
	 * field must be populated - a filter matching every criterion vacuously is rejected with
	 * ERROR_INVALID_PARAMETER so this can never be used to sweep the entire driver store.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	struct DriverStorePackageFilter
	{
		/// Device setup class the package's INF must declare (via SetupDiGetINFClassW)
		std::optional<GUID> ClassGuid;
		/// Filter driver service name the package's INF must register as an UpperFilters/
		/// LowerFilters class filter (via GetInfClassFilterTargets); does NOT match a function
		/// driver's plain [*.Services] AddService entry
		std::optional<std::wstring> ServiceName;
		/// Base name(s) of the original INF the package must have been staged from (case-
		/// insensitive); a package matches if its name is any one of these. Left empty, this
		/// criterion imposes no constraint - but then at least one of the other fields must be
		/// set instead
		std::vector<std::wstring> OriginalInfNames;
		/// [Version] Provider the package's INF must declare (case-insensitive)
		std::optional<std::wstring> Provider;
		/// [Version] DriverVer the package's INF must declare (case-insensitive)
		std::optional<std::wstring> DriverVer;
	};

	/**
	 * A driver store package matched by FindDriverStorePackages / RemoveDriverStorePackages,
	 * together with whatever identity information could be determined about it.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	struct DriverStorePackageDetails
	{
		/// Absolute path of this package's INF copy inside the driver store
		std::wstring DriverPackageInfPath;
		/// Published name in %WINDIR%\INF, e.g. "oem12.inf"
		std::wstring PublishedInfName;
		/// Base name of the original INF this package was staged from, if determinable
		std::optional<std::wstring> OriginalInfName;
		/// [Version] Provider, if determinable
		std::optional<std::wstring> Provider;
		/// [Version] DriverVer, if determinable
		std::optional<std::wstring> DriverVer;
		/// Device setup class, if determinable
		std::optional<GUID> ClassGuid;
		/// Filter driver service name(s) this package's INF registers, if any
		std::vector<std::wstring> ServiceNames;
	};

	/**
	 * Pure predicate: true if an already-resolved DriverStorePackageDetails satisfies every
	 * criterion populated in a DriverStorePackageFilter (logical AND). A criterion that is
	 * populated in Filter but whose corresponding field is unset on Details (i.e. it couldn't be
	 * determined for that package) counts as a non-match, never a pass-through. Performs no I/O
	 * and touches no global state - the single source of truth for match semantics, callable with
	 * synthetic data for testing without needing a real driver store.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 	Details	The (possibly partially resolved) package details to test.
	 * @param 	Filter 	The selection criteria; see DriverStorePackageFilter.
	 *
	 * @returns	True if every criterion Filter populates is satisfied by Details.
	 */
	bool DriverStorePackageMatchesFilter(const DriverStorePackageDetails& Details,
	                                     const DriverStorePackageFilter& Filter);

	/**
	 * Finds every driver store package matching the given filter, without deleting anything.
	 * Intended both as the basis for RemoveDriverStorePackages and as a read-only way for a
	 * caller to inspect (e.g. under a "--verbose" flag) exactly what a subsequent removal would
	 * affect before committing to it.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 	Filter	The selection criteria; see DriverStorePackageFilter.
	 *
	 * @returns	A std::expected&lt;std::vector&lt;DriverStorePackageDetails&gt;,nefarius::utilities::Win32Error&gt;
	 */
	std::expected<std::vector<DriverStorePackageDetails>, nefarius::utilities::Win32Error> FindDriverStorePackages(
		const DriverStorePackageFilter& Filter);

	/**
	 * Outcome of attempting to remove a single driver store package as part of a
	 * RemoveDriverStorePackages call.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	struct DriverStorePackageRemoval
	{
		/// The package this removal attempt targeted
		DriverStorePackageDetails Package;
		/// True if the package was successfully removed
		bool Removed = false;
		/// True if a reboot is required to fully complete this specific removal (only set by the
		/// DiUninstallDriverW fallback)
		bool RebootRequired = false;
		/// Set if the removal attempt failed; empty (not Removed and no Error) never occurs
		std::optional<nefarius::utilities::Win32Error> Error;
	};

	/**
	 * Surgically removes every driver store package matching the given filter, without touching
	 * any device node (unlike UninstallDriver/DiUninstallDriverW, which also uninstalls devices
	 * still using the driver). Every matched package is attempted independently via the same
	 * cascade RemoveDriverStorePackage uses (the undocumented drvstore.dll offline delete API,
	 * falling back to SetupUninstallOEMInfW and finally to DiUninstallDriverW); a failure on one
	 * package does not prevent the others from being attempted. Zero matches is not an error -
	 * it simply yields an empty vector, letting the caller decide how to report "nothing to do".
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 		  	Filter		  	The selection criteria; see DriverStorePackageFilter.
	 * @param [in,out]	RebootRequired	If non-null, OR'd with every individual removal's
	 * 									RebootRequired.
	 *
	 * @returns	A std::expected&lt;std::vector&lt;DriverStorePackageRemoval&gt;,nefarius::utilities::Win32Error&gt;;
	 * 			the outer std::unexpected is only used for filter validation and enumeration
	 * 			failures, never for an individual package's removal failure.
	 */
	std::expected<std::vector<DriverStorePackageRemoval>, nefarius::utilities::Win32Error> RemoveDriverStorePackages(
		const DriverStorePackageFilter& Filter, bool* RebootRequired = nullptr);

	/**
	 * Surgically removes the driver store package matching a given original INF file, without
	 * touching any device node (unlike UninstallDriver/DiUninstallDriverW, which also uninstalls
	 * devices still using the driver). Matches the target package by its [Version] identity
	 * (Provider + DriverVer) as well as the base name of the original INF file the package was
	 * staged from, and deletes it via the undocumented drvstore.dll offline delete API,
	 * falling back to SetupUninstallOEMInfW and finally to DiUninstallDriverW if the surgical path
	 * is unavailable or fails. A package that is already absent is treated as success.
	 *
	 * Thin convenience wrapper around RemoveDriverStorePackages for the common single-file case;
	 * see that function to select and remove multiple packages at once (e.g. every version of a
	 * driver sharing a class + filter service name + original INF name).
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	16.08.2026
	 *
	 * @param 		  	FullInfPath   	Full pathname of the original INF file.
	 * @param [in,out]	RebootRequired	If non-null, true if reboot required (only set by the
	 * 									DiUninstallDriverW fallback).
	 *
	 * @returns	A std::expected&lt;void,nefarius::utilities::Win32Error&gt;
	 */
	template <nefarius::utilities::string_type StringType>
	std::expected<void, nefarius::utilities::Win32Error> RemoveDriverStorePackage(
		const StringType& FullInfPath, bool* RebootRequired = nullptr);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::RemoveDriverStorePackage(
		const std::wstring& FullInfPath, bool* RebootRequired);

	template
	std::expected<void, nefarius::utilities::Win32Error> nefarius::devcon::RemoveDriverStorePackage(
		const std::string& FullInfPath, bool* RebootRequired);
}
