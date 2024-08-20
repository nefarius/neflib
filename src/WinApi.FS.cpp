// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;


namespace
{
	template <typename StringType>
	std::expected<const VS_FIXEDFILEINFO*, Win32Error> GetFileVersionResource(const StringType& FilePath)
	{
		const std::wstring filePath = ConvertToWide(FilePath);

		DWORD verHandle = 0;
		UINT size = 0;
		LPBYTE lpBuffer = nullptr;
		const DWORD verSize = GetFileVersionInfoSizeW(filePath.c_str(), &verHandle);

		if (!verSize)
		{
			return std::unexpected(Win32Error("GetFileVersionInfoSizeA"));
		}

		const auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(verSize);

		if (!GetFileVersionInfoW(filePath.c_str(), verHandle, verSize, buffer.get()))
		{
			return std::unexpected(Win32Error("GetFileVersionInfoA"));
		}

		if (!VerQueryValueW(buffer.get(), L"\\", (VOID FAR * FAR*)&lpBuffer, &size))
		{
			return std::unexpected(Win32Error("VerQueryValueW"));
		}

		if (size == 0)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_USER_BUFFER, "VerQueryValueW"));
		}

		const VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;

		if (verInfo->dwSignature != 0xfeef04bd)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_EXE_SIGNATURE, "VS_FIXEDFILEINFO"));
		}

		return verInfo;
	}
}

template
std::expected<void, Win32Error> nefarius::winapi::fs::TakeFileOwnership(const std::wstring& FilePath);

template
std::expected<void, Win32Error> nefarius::winapi::fs::TakeFileOwnership(const std::string& FilePath);

template <nefarius::utilities::string_type StringType>
std::expected<void, Win32Error> nefarius::winapi::fs::TakeFileOwnership(const StringType& FilePath)
{
	const std::wstring filePath = ConvertToWide(FilePath);

	HANDLE token;
	DWORD len;
	PSECURITY_DESCRIPTOR security = nullptr;

	// Get the privileges you need
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
	{
		return std::unexpected(Win32Error("OpenProcessToken"));
	}

	if (auto ret = security::SetPrivilege(std::wstring(L"SeTakeOwnershipPrivilege"), TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(std::wstring(L"SeSecurityPrivilege"), TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(std::wstring(L"SeBackupPrivilege"), TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(std::wstring(L"SeRestorePrivilege"), TRUE); !ret)
	{
		return ret;
	}

	// Create the security descriptor
	if (!GetFileSecurityW(filePath.c_str(), OWNER_SECURITY_INFORMATION, security, 0, &len))
	{
		return std::unexpected(Win32Error("GetFileSecurityW"));
	}

	security = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);

	SCOPE_GUARD_CAPTURE({ HeapFree(GetProcessHeap(), 0, security); }, security);

	if (!InitializeSecurityDescriptor(security, SECURITY_DESCRIPTOR_REVISION))
	{
		return std::unexpected(Win32Error("InitializeSecurityDescriptor"));
	}

	const auto sid = security::GetLogonSID(token);

	if (!sid)
	{
		return std::unexpected(sid.error());
	}

	SCOPE_GUARD_CAPTURE({ HeapFree(GetProcessHeap(), 0, sid.value()); }, sid);

	// Set the sid to be the new owner
	if (!SetSecurityDescriptorOwner(security, sid.value(), 0))
	{
		return std::unexpected(Win32Error("SetSecurityDescriptorOwner"));
	}

	// Save the security descriptor
	if (!SetFileSecurityW(filePath.c_str(), OWNER_SECURITY_INFORMATION, security))
	{
		return std::unexpected(Win32Error("SetFileSecurityW"));
	}

	return {};
}

template
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::GetProductVersionFromFile(
	const std::wstring& filePath);

template
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::GetProductVersionFromFile(
	const std::string& filePath);

template <nefarius::utilities::string_type StringType>
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::GetProductVersionFromFile(
	const StringType& FilePath)
{
	const auto ret = ::GetFileVersionResource(FilePath);

	if (!ret)
	{
		return std::unexpected(ret.error());
	}

	const auto version = ret.value();

	return Version{
		HIWORD(version->dwProductVersionMS),
		LOWORD(version->dwProductVersionMS),
		HIWORD(version->dwProductVersionLS),
		LOWORD(version->dwProductVersionLS)
	};
}

template
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::
GetFileVersionFromFile(const std::wstring& FilePath);

template
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::
GetFileVersionFromFile(const std::string& FilePath);

template <nefarius::utilities::string_type StringType>
std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::
GetFileVersionFromFile(const StringType& FilePath)
{
	const auto ret = ::GetFileVersionResource(FilePath);

	if (!ret)
	{
		return std::unexpected(ret.error());
	}

	const auto version = ret.value();

	return Version{
		HIWORD(version->dwFileVersionMS),
		LOWORD(version->dwFileVersionMS),
		HIWORD(version->dwFileVersionLS),
		LOWORD(version->dwFileVersionLS)
	};
}

template
std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryExists(const std::wstring& Path);

template
std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryExists(const std::string& Path);

template <nefarius::utilities::string_type StringType>
std::expected<bool, nefarius::utilities::Win32Error> nefarius::winapi::fs::DirectoryExists(const StringType& Path)
{
	const std::wstring dirName = nefarius::utilities::ConvertToWide(Path);
	const DWORD type = GetFileAttributesW(dirName.c_str());

	if (type == INVALID_FILE_ATTRIBUTES)
	{
		return std::unexpected(Win32Error("GetFileAttributesW"));
	}

	if (type & FILE_ATTRIBUTE_DIRECTORY)
	{
		return true;
	}

	return false;
}
