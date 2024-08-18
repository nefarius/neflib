// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;


std::vector<const char*> nefarius::winapi::cli::CliArgsResult::AsArgv(int* argc)
{
	const auto numArgs = this->Arguments.size();

	std::vector<const char*> argv;
	argv.resize(numArgs);

	std::ranges::transform(this->Arguments, argv.begin(), [](const std::string& arg) { return arg.c_str(); });

	argv.push_back(nullptr);

	if (argc)
		*argc = (int)numArgs;

	return argv;
}

std::expected<nefarius::winapi::cli::CliArgsResult, Win32Error> nefarius::winapi::cli::GetCommandLineArgs()
{
	int nArgs;

	const LPWSTR* argList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (nullptr == argList)
	{
		return std::unexpected(Win32Error("CommandLineToArgvW"));
	}

	std::vector<std::string> narrow;
	narrow.reserve(nArgs);

	for (int i = 0; i < nArgs; i++)
	{
		narrow.push_back(ConvertWideToANSI(std::wstring(argList[i])));
	}

	return CliArgsResult{std::move(narrow)};
}
