#include "pch.h"

#include <nefarius/neflib/AnyString.hpp>


template
void nefarius::utilities::StripNullCharacters(std::wstring& s);

template
void nefarius::utilities::StripNullCharacters(std::string& s);

template <typename StringType>
void nefarius::utilities::StripNullCharacters(StringType& s)
{
	if constexpr (std::is_same_v<StringType, std::string>)
	{
		s.erase(std::ranges::find(s, '\0'), s.end());
	}
	else if constexpr (std::is_same_v<StringType, std::wstring>)
	{
		s.erase(std::ranges::find(s, L'\0'), s.end());
	}
}
