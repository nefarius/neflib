// ReSharper disable CppRedundantQualifier
#pragma once


#include <nefarius/neflib/Win32Error.hpp>

namespace nefarius::devcon
{
	struct FindByHwIdResult
	{
		std::vector<std::wstring> HardwareIds;

		std::wstring Name;

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
	 * @param 	className 	Name of the device class (System, HIDClass, USB, etc.).
	 * @param 	classGuid 	Unique identifier for the device class.
	 * @param 	hardwareId	The Hardware ID to set.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	std::expected<void, nefarius::utilities::Win32Error> Create(const std::wstring& className, const GUID* classGuid,
	                                                            const nefarius::utilities::WideMultiStringArray&
	                                                            hardwareId);

	/**
	 * Triggers a driver update on all devices matching a given hardware ID with using the provided INF.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	07.08.2024
	 *
	 * @param 		  	hardwareId	  	The Hardware ID of the devices to affect.
	 * @param 		  	fullInfPath   	Full pathname to the INF file.
	 * @param [in,out]	rebootRequired	If non-null, true if reboot required.
	 * @param 		  	force		  	(Optional) True to force.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	std::expected<void, nefarius::utilities::Win32Error> Update(const std::wstring& hardwareId,
	                                                            const std::wstring& fullInfPath, bool* rebootRequired,
	                                                            bool force = false);

	/**
     * Installs a given driver into the driver store.
     *
     * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
     * @date	07.08.2024
     *
     * @param 		  	fullInfPath   	Full pathname of the INF file.
     * @param [in,out]	rebootRequired	If non-null, true if reboot required.
     *
     * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
     */
	std::expected<void, nefarius::utilities::Win32Error> InstallDriver(const std::wstring& fullInfPath,
	                                                                   bool* rebootRequired);

	/**
	 * Uninstalls a given driver.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	07.08.2024
	 *
	 * @param 		  	fullInfPath   	Full pathname of the INF file.
	 * @param [in,out]	rebootRequired	If non-null, true if reboot required.
	 *
	 * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
	 */
	std::expected<void, nefarius::utilities::Win32Error> UninstallDriver(const std::wstring& fullInfPath,
	                                                                     bool* rebootRequired);
}
