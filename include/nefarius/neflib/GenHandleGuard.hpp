#pragma once


namespace nefarius::utilities::guards
{
	/**
	 * Scope guard for handles that use INVALID_HANDLE_VALUE to signify failure.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 */
	class InvalidHandleGuard
	{
	public:
		// Constructor takes the HANDLE to manage
		explicit InvalidHandleGuard(HANDLE handle) : handle_(handle)
		{
		}

		// Destructor releases the HANDLE resource
		~InvalidHandleGuard()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle_);
			}
		}

		// Disable copy constructor and copy assignment operator
		InvalidHandleGuard(const InvalidHandleGuard&) = delete;
		InvalidHandleGuard& operator=(const InvalidHandleGuard&) = delete;

		// Move constructor
		InvalidHandleGuard(InvalidHandleGuard&& other) noexcept : handle_(other.handle_)
		{
			other.handle_ = INVALID_HANDLE_VALUE;
		}

		// Move assignment operator
		InvalidHandleGuard& operator=(InvalidHandleGuard&& other) noexcept
		{
			if (this != &other)
			{
				// Release current resource
				if (handle_ != INVALID_HANDLE_VALUE)
				{
					CloseHandle(handle_);
				}
				// Transfer ownership
				handle_ = other.handle_;
				other.handle_ = INVALID_HANDLE_VALUE;
			}
			return *this;
		}

		// Function to manually release the handle, if needed
		void release()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle_);
				handle_ = INVALID_HANDLE_VALUE;
			}
		}

		// Accessor for the handle
		[[nodiscard]] HANDLE get() const
		{
			return handle_;
		}

		[[nodiscard]] bool is_invalid() const
		{
			return handle_ == INVALID_HANDLE_VALUE;
		}

	private:
		HANDLE handle_;
	};

	/**
	 * Scope guard for handles that use NULL to signify failure.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	09.08.2024
	 */
	class NullHandleGuard
	{
	public:
		// Constructor takes the HANDLE to manage
		explicit NullHandleGuard(HANDLE handle) : handle_(handle)
		{
		}

		// Destructor releases the HANDLE resource
		~NullHandleGuard()
		{
			if (handle_ != NULL)
			{
				CloseHandle(handle_);
			}
		}

		// Disable copy constructor and copy assignment operator
		NullHandleGuard(const NullHandleGuard&) = delete;
		NullHandleGuard& operator=(const NullHandleGuard&) = delete;

		// Move constructor
		NullHandleGuard(NullHandleGuard&& other) noexcept : handle_(other.handle_)
		{
			other.handle_ = NULL;
		}

		// Move assignment operator
		NullHandleGuard& operator=(NullHandleGuard&& other) noexcept
		{
			if (this != &other)
			{
				// Release current resource
				if (handle_ != NULL)
				{
					CloseHandle(handle_);
				}
				// Transfer ownership
				handle_ = other.handle_;
				other.handle_ = INVALID_HANDLE_VALUE;
			}
			return *this;
		}

		// Function to manually release the handle, if needed
		void release()
		{
			if (handle_ != NULL)
			{
				CloseHandle(handle_);
				handle_ = NULL;
			}
		}

		// Accessor for the handle
		[[nodiscard]] HANDLE get() const
		{
			return handle_;
		}

		[[nodiscard]] bool is_invalid() const
		{
			return handle_ == NULL;
		}

	private:
		HANDLE handle_;
	};
}
