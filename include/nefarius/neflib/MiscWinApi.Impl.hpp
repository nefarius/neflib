// ReSharper disable CppRedundantQualifier
#pragma once

#ifndef NEFLIB_MISCWINAPI_IMPL_INCLUDED
    #error "Do not include MiscWinApi.Impl.hpp directly. Include MiscWinApi.hpp instead."
#endif

namespace nefarius::winapi
{
	template <nefarius::utilities::string_type StringType>
	std::expected<std::variant<std::string, std::wstring>, nefarius::utilities::Win32Error>
	GetProcessFullPath(DWORD PID)
	{
		std::expected<std::wstring, nefarius::utilities::Win32Error> GetProcessFullPathImpl(DWORD ProcessID);

		if constexpr (std::is_same_v<StringType, std::wstring>)
		{
			return GetProcessFullPathImpl(PID);
		}
		else if constexpr (std::is_same_v<StringType, std::string>)
		{
			const auto result = GetProcessFullPathImpl(PID);

			if (!result)
			{
				return std::unexpected(result.error());
			}

			return nefarius::utilities::ConvertToNarrow(result.value());
		}
		else
		{
			static_assert(false, "Not a string type");
		}

		return std::unexpected(nefarius::utilities::Win32Error(ERROR_INTERNAL_ERROR));
	}
}
