#pragma once


namespace nefarius::utilities::guards
{
	class HDEVINFOHandleGuard
	{
	public:
		// Constructor takes the HDEVINFO handle to manage
		explicit HDEVINFOHandleGuard(HDEVINFO handle) : handle_(handle)
		{
		}

		// Destructor releases the HDEVINFO resource
		~HDEVINFOHandleGuard()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				SetupDiDestroyDeviceInfoList(handle_);
			}
		}

		// Disable copy constructor and copy assignment operator
		HDEVINFOHandleGuard(const HDEVINFOHandleGuard&) = delete;
		HDEVINFOHandleGuard& operator=(const HDEVINFOHandleGuard&) = delete;

		// Move constructor
		HDEVINFOHandleGuard(HDEVINFOHandleGuard&& other) noexcept : handle_(other.handle_)
		{
			other.handle_ = INVALID_HANDLE_VALUE;
		}

		// Move assignment operator
		HDEVINFOHandleGuard& operator=(HDEVINFOHandleGuard&& other) noexcept
		{
			if (this != &other)
			{
				// Release current resource
				if (handle_ != INVALID_HANDLE_VALUE)
				{
					SetupDiDestroyDeviceInfoList(handle_);
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
				SetupDiDestroyDeviceInfoList(handle_);
				handle_ = INVALID_HANDLE_VALUE;
			}
		}

		// Accessor for the handle
		[[nodiscard]] HDEVINFO get() const
		{
			return handle_;
		}

		[[nodiscard]] bool is_invalid() const
		{
			return handle_ == INVALID_HANDLE_VALUE;
		}

	private:
		HDEVINFO handle_;
	};
}
