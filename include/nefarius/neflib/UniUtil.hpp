#pragma once

namespace nefarius::utilities
{
	std::string ConvertWideToANSI(const std::wstring& wide);

	std::wstring ConvertAnsiToWide(const std::string& narrow);
}
