#pragma once

namespace nefarius::utilities
{
	std::string ConvertWideToANSI(const std::wstring& wide);

	std::wstring ConvertAnsiToWide(const std::string& narrow);

	template <typename StringType>
	// ReSharper disable once CppFunctionIsNotImplemented
	std::string ConvertToNarrow(const StringType& str);

	template <typename StringType>
	// ReSharper disable once CppFunctionIsNotImplemented
	std::wstring ConvertToWide(const StringType& str);
}
