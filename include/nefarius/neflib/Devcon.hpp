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

	namespace bluetooth
	{
		std::expected<void, nefarius::utilities::Win32Error> RestartBthUsbDevice(int instance = 0);

		std::expected<void, nefarius::utilities::Win32Error> EnableDisableBthUsbDevice(bool state, int instance = 0);
	}
}
