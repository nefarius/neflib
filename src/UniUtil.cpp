#include "pch.h"

#include <nefarius/neflib/UniUtil.hpp>


std::string nefarius::utilities::ConvertWideToANSI(const std::wstring& wide)
{
	int count = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.length(), NULL, 0, NULL, NULL);
	std::string str(count, 0);
	WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &str[0], count, NULL, NULL);
	return str;
}

std::wstring nefarius::utilities::ConvertAnsiToWide(const std::string& narrow)
{
	int count = MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), (int)narrow.length(), NULL, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), (int)narrow.length(), &wstr[0], count);
	return wstr;
}

template <>
std::string nefarius::utilities::ConvertToNarrow<std::string>(const std::string& str)
{
	return str;
}

template <>
std::string nefarius::utilities::ConvertToNarrow<std::wstring>(const std::wstring& wstr)
{
	return ConvertWideToANSI(wstr);
}

template <>
std::wstring nefarius::utilities::ConvertToWide<std::string>(const std::string& str)
{
	return ConvertAnsiToWide(str);
}

template <>
std::wstring nefarius::utilities::ConvertToWide<std::wstring>(const std::wstring& wstr)
{
	return wstr;
}
