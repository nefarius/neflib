// ReSharper disable CppRedundantQualifier
#pragma once

#include <nefarius/neflib/AnyString.hpp>

namespace nefarius::utilities
{
	std::string ConvertWideToANSI(const std::wstring& wide);

	std::wstring ConvertAnsiToWide(const std::string& narrow);

	template <nefarius::utilities::string_type StringType>
	std::string ConvertToNarrow(const StringType& str)
	{
		if constexpr (std::is_same_v<StringType, std::wstring>)
		{
			return nefarius::utilities::ConvertWideToANSI(str);
		}
		else if constexpr (std::is_same_v<StringType, std::string>)
		{
			return str;
		}
		else
		{
			static_assert(false, "Not a string type");
		}

		return {};
	}

	template <nefarius::utilities::string_type StringType>
	std::wstring ConvertToWide(const StringType& str)
	{
		if constexpr (std::is_same_v<StringType, std::wstring>)
		{
			return str;
		}
		else if constexpr (std::is_same_v<StringType, std::string>)
		{
			return nefarius::utilities::ConvertAnsiToWide(str);
		}
		else
		{
			static_assert(false, "Not a string type");
		}

		return {};
	}
}
