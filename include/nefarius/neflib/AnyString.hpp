#pragma once

namespace nefarius::utilities
{
	template <class T>
	concept string_type = std::same_as<T, std::basic_string<
		                                   typename T::value_type, typename T::traits_type, typename T::allocator_type>>
		;

	template <typename StringType>
	void StripNullCharacters(StringType& s);
}
