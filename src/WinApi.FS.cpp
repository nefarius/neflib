// ReSharper disable CppCStyleCast
#include "pch.h"

#include <nefarius/neflib/MiscWinApi.hpp>

using namespace nefarius::utilities;


namespace
{
	std::expected<const VS_FIXEDFILEINFO*, Win32Error> GetFileVersionResource(const std::string& filePath)
	{
		DWORD verHandle = 0;
		UINT size = 0;
		LPBYTE lpBuffer = nullptr;
		const DWORD verSize = GetFileVersionInfoSizeA(filePath.c_str(), &verHandle);

		if (!verSize)
		{
			return std::unexpected(Win32Error("GetFileVersionInfoSizeA"));
		}

		const auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(verSize);

		if (!GetFileVersionInfoA(filePath.c_str(), verHandle, verSize, buffer.get()))
		{
			return std::unexpected(Win32Error("GetFileVersionInfoA"));
		}

		if (!VerQueryValueA(buffer.get(), "\\", (VOID FAR * FAR*)&lpBuffer, &size))
		{
			return std::unexpected(Win32Error("VerQueryValueA"));
		}

		if (size == 0)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_USER_BUFFER, "VerQueryValueA"));
		}

		const VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;

		if (verInfo->dwSignature != 0xfeef04bd)
		{
			return std::unexpected(Win32Error(ERROR_INVALID_EXE_SIGNATURE, "VS_FIXEDFILEINFO"));
		}

		return verInfo;
	}
}

std::expected<void, Win32Error> nefarius::winapi::fs::TakeFileOwnership(LPCWSTR file)
{
	HANDLE token;
	DWORD len;
	PSECURITY_DESCRIPTOR security = nullptr;

	// Get the privileges you need
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
	{
		return std::unexpected(Win32Error("OpenProcessToken"));
	}

	if (auto ret = security::SetPrivilege(L"SeTakeOwnershipPrivilege", TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(L"SeSecurityPrivilege", TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(L"SeBackupPrivilege", TRUE); !ret)
	{
		return ret;
	}
	if (auto ret = security::SetPrivilege(L"SeRestorePrivilege", TRUE); !ret)
	{
		return ret;
	}

	// Create the security descriptor
	if (!GetFileSecurity(file, OWNER_SECURITY_INFORMATION, security, 0, &len))
	{
		return std::unexpected(Win32Error("GetFileSecurity"));
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
	if (!SetFileSecurity(file, OWNER_SECURITY_INFORMATION, security))
	{
		return std::unexpected(Win32Error("SetFileSecurity"));
	}

	return {};
}

std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::GetProductVersionFromFile(
	const std::string& filePath)
{
	const auto ret = ::GetFileVersionResource(filePath);

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

std::expected<nefarius::winapi::fs::Version, Win32Error> nefarius::winapi::fs::
GetFileVersionFromFile(const std::string& filePath)
{
	const auto ret = ::GetFileVersionResource(filePath);

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
