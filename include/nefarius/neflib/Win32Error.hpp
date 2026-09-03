#pragma once

#include <nefarius/neflib/UniUtil.hpp>

namespace nefarius::utilities
{
	using namespace nefarius::utilities;

	class Win32Error
	{
	public:
		explicit Win32Error(std::string additionalMessage) : errorCode(GetLastError()),
		                                                     additionalMessage(std::move(additionalMessage))
		{
		}

		explicit Win32Error(DWORD errorCode = GetLastError(), std::string additionalMessage = "") :
			errorCode(errorCode),
			additionalMessage(std::move(additionalMessage))
		{
		}

		[[nodiscard]] DWORD getErrorCode() const
		{
			return errorCode;
		}

		[[nodiscard]] std::string getErrorMessageA() const
		{
			char* messageBuffer = nullptr;
			const size_t messageSize = FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				errorCode,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPSTR)&messageBuffer,
				0,
				NULL
			);

			// FormatMessageA can fail (e.g. an error code with no registered message table entry),
			// in which case messageBuffer is left null; fall back to a numeric-only message instead
			// of constructing a std::string from a null pointer.
			std::string message = messageBuffer
				? std::string(messageBuffer, messageSize)
				: std::format("Unknown error 0x{:08X}", errorCode);

			if (messageBuffer)
				LocalFree(messageBuffer);

			if (additionalMessage.empty())
				return message;

			return std::format(
				"{} failed with error: {} ({})",
				additionalMessage,
				message,
				errorCode
			);
		}

		[[nodiscard]] std::wstring getErrorMessageW() const
		{
			wchar_t* messageBuffer = nullptr;
			const size_t messageSize = FormatMessageW(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				errorCode,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPWSTR)&messageBuffer,
				0,
				NULL
			);

			// FormatMessageW can fail (e.g. an error code with no registered message table entry),
			// in which case messageBuffer is left null; fall back to a numeric-only message instead
			// of constructing a std::wstring from a null pointer.
			std::wstring message = messageBuffer
				? std::wstring(messageBuffer, messageSize)
				: std::format(L"Unknown error 0x{:08X}", errorCode);

			if (messageBuffer)
				LocalFree(messageBuffer);

			if (additionalMessage.empty())
				return message;

			return ConvertAnsiToWide(std::format(
				"{} failed with error: {} ({})",
				additionalMessage,
				ConvertWideToANSI(message),
				errorCode
			));
		}

	private:
		DWORD errorCode;
		std::string additionalMessage;
	};
}
