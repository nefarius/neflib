#pragma once

#include <handleapi.h>
#include <minwindef.h>
#include <winreg.h>

namespace nefarius::utilities::guards
{
	class HKEYHandleGuard
	{
	public:
		// Constructor takes the HKEY handle to manage
		explicit HKEYHandleGuard(HKEY handle) : handle_(handle)
		{
		}

		// Destructor releases the HKEY resource
		~HKEYHandleGuard()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				RegCloseKey(handle_);
			}
		}

		// Disable copy constructor and copy assignment operator
		HKEYHandleGuard(const HKEYHandleGuard&) = delete;
		HKEYHandleGuard& operator=(const HKEYHandleGuard&) = delete;

		// Move constructor
		HKEYHandleGuard(HKEYHandleGuard&& other) noexcept : handle_(other.handle_)
		{
			other.handle_ = (HKEY)INVALID_HANDLE_VALUE;
		}

		// Move assignment operator
		HKEYHandleGuard& operator=(HKEYHandleGuard&& other) noexcept
		{
			if (this != &other)
			{
				// Release current resource
				if (handle_ != INVALID_HANDLE_VALUE)
				{
					RegCloseKey(handle_);
				}
				// Transfer ownership
				handle_ = other.handle_;
				other.handle_ = (HKEY)INVALID_HANDLE_VALUE;
			}
			return *this;
		}

		// Function to manually release the handle, if needed
		void release()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				RegCloseKey(handle_);
				handle_ = (HKEY)INVALID_HANDLE_VALUE;
			}
		}

		// Accessor for the handle
		[[nodiscard]] HKEY get() const
		{
			return handle_;
		}

		[[nodiscard]] bool is_invalid() const
		{
			return handle_ == INVALID_HANDLE_VALUE;
		}

	private:
		HKEY handle_;
	};
}
