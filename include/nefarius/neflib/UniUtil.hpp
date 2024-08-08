#pragma once

namespace nefarius::utilities
{
	std::string ConvertWideToANSI(const std::wstring& wstr);

	std::wstring ConvertAnsiToWide(const std::string& str);
}
