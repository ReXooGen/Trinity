#pragma once
#include <cctype>

namespace trinity
{
    // Case-insensitive substring test (`needle` somewhere in `hay`). Used
    // both for menu search-filter rows and for classifying item keys by
    // recognized substrings.
    inline bool ContainsNoCase(const char* hay, const char* needle)
    {
        for (; *hay; ++hay)
        {
            const char* h = hay;
            const char* n = needle;
            while (*h && *n &&
                   tolower(static_cast<unsigned char>(*h)) == tolower(static_cast<unsigned char>(*n)))
            { ++h; ++n; }
            if (!*n) return true;
        }
        return !*needle; // empty needle matches (incl. empty hay)
    }
}
