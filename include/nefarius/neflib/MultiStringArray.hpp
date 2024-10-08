#pragma once


namespace nefarius::utilities
{
	// Template class for double-NULL-terminated multi-string array
	template <typename CharT>
	class MultiStringArray
	{
	public:
		using StringType = std::basic_string<CharT>;
		using CharType = CharT;

		MultiStringArray() = default;

		// Construct from a vector of strings
		explicit MultiStringArray(const std::vector<StringType>& strings)
		{
			from_vector(strings);
		}

		// Construct from a single string
		explicit MultiStringArray(const StringType& str)
		{
			from_string(str);
		}

		// Preallocate by amount of bytes
		explicit MultiStringArray(size_t size)
		{
			data_.resize(size);
		}

		// Construct from C buffer
		explicit MultiStringArray(LPTSTR buffer, size_t bufferLength)
		{
			data_.assign(buffer, buffer + bufferLength);
		}

		// Convert to a vector of strings
		std::vector<StringType> to_vector() const
		{
			std::vector<StringType> result;
			const CharType* p = data_.data();
			while (*p)
			{
				result.emplace_back(p);
				p += std::char_traits<CharType>::length(p) + 1;
			}
			return result;
		}

		// Initialize from a vector of strings
		void from_vector(const std::vector<StringType>& strings)
		{
			size_t total_length = 0;
			for (const auto& str : strings)
			{
				total_length += str.size() + 1;
			}
			total_length++; // For the final double-NULL termination

			data_.resize(total_length);
			CharType* p = data_.data();
			for (const auto& str : strings)
			{
				std::memcpy(p, str.data(), str.size() * sizeof(CharType));
				p += str.size();
				*p++ = CharType('\0');
			}
			*p = CharType('\0');
		}

		// Initialize from a single string
		void from_string(const StringType& str)
		{
			data_.resize(str.size() + 2); // Original string size + double-NULL termination
			std::memcpy(data_.data(), str.data(), str.size() * sizeof(CharType));
			data_[str.size()] = CharType('\0');
			data_[str.size() + 1] = CharType('\0');
		}

		// Get the raw data
		const CharType* c_str() const
		{
			return data_.data();
		}

		// Get the raw data
		unsigned char* data() const
		{
			return (unsigned char*)data_.data();
		}

		// Get the size of the raw data in bytes
		[[nodiscard]] size_t size() const
		{
			return data_.size() * sizeof(CharType);
		}

		// Get the size of the raw data in characters
		[[nodiscard]] size_t chars() const
		{
			return data_.size();
		}

		// Looks for occurrence of specified string in the array
		[[nodiscard]] bool contains(const StringType& match)
		{
			const auto vector = to_vector();
			return std::ranges::find(vector, match) != vector.end();
		}

	private:
		std::vector<CharType> data_;
	};

	// Type aliases for narrow and wide versions
	using NarrowMultiStringArray = MultiStringArray<char>;
	using WideMultiStringArray = MultiStringArray<wchar_t>;
}
