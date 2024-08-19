#pragma once

#ifndef NEFLIB_MISCWINAPI_IMPL_INCLUDED
    #error "Do not include MiscWinApi.Impl.hpp directly. Include MiscWinApi.hpp instead."
#endif


template <nefarius::utilities::string_type StringType>
std::expected<std::variant<std::string, std::wstring>, nefarius::utilities::Win32Error> GetProcessFullPath(DWORD PID)
{
	const HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, PID);
	if (hProcess == nullptr)
	{
		return std::unexpected(nefarius::utilities::Win32Error("OpenProcess"));
	}

	SCOPE_GUARD_CAPTURE({ CloseHandle(hProcess); }, hProcess);

	DWORD numChars = MAX_PATH;
	StringType processPath(numChars, '\0');

	if constexpr (std::is_same_v<StringType, std::wstring>)
	{
		if (!QueryFullProcessImageNameW(hProcess, 0, processPath.data(), &numChars))
		{
			return std::unexpected(nefarius::utilities::Win32Error("QueryFullProcessImageNameW"));
		}
	}
	else if constexpr (std::is_same_v<StringType, std::string>)
	{
		if (!QueryFullProcessImageNameA(hProcess, 0, processPath.data(), &numChars))
		{
			return std::unexpected(nefarius::utilities::Win32Error("QueryFullProcessImageNameA"));
		}
	}
	else
	{
		static_assert(false, "Not a string type");
	}

	StripNullCharacters(processPath);

	return processPath;
}
