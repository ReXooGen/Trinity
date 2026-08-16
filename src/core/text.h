#pragma once
#include <cctype>

namespace trinity
{
    // Case-insensitive substring test (`needle` somewhere in `hay`). Used
    // both for menu search-filter rows and for classifying item keys by
    // recognized substrings.
    inline bool ContainsNoCase(const char* hay, const char* needle)
    {
        if (!hay || !needle) return false;
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

    // Smart substring matcher that handles common typos, synonyms, and underscores/spaces
    inline bool SearchMatches(const char* hay, const char* needle)
    {
        if (!needle || !needle[0]) return true;
        if (!hay || !hay[0]) return false;
        if (ContainsNoCase(hay, needle)) return true;

        // Clean underscores / dashes / spaces comparison:
        char normHay[128]{};
        char normNeedle[128]{};
        size_t hi = 0, ni = 0;
        for (const char* p = hay; *p && hi < sizeof(normHay) - 1; ++p)
        {
            if (*p == '_' || *p == '-' || *p == ' ') normHay[hi++] = ' ';
            else normHay[hi++] = *p;
        }
        for (const char* p = needle; *p && ni < sizeof(normNeedle) - 1; ++p)
        {
            if (*p == '_' || *p == '-' || *p == ' ') normNeedle[ni++] = ' ';
            else normNeedle[ni++] = *p;
        }
        while (ni > 0 && normNeedle[ni - 1] == ' ') normNeedle[--ni] = '\0';

        if (normNeedle[0] && ContainsNoCase(normHay, normNeedle)) return true;

        // Alias & typo tolerance:
        // "abbys", "abbyss", "abis" -> matches "abyss"
        if (ContainsNoCase(needle, "abbys") || ContainsNoCase(needle, "abbyss") || ContainsNoCase(needle, "abis"))
        {
            if (ContainsNoCase(hay, "abyss") || ContainsNoCase(normHay, "abyss")) return true;
        }
        // "artifak", "artifac" -> matches "artifact"
        if (ContainsNoCase(needle, "artifak") || ContainsNoCase(needle, "artifac"))
        {
            if (ContainsNoCase(hay, "artifact") || ContainsNoCase(normHay, "artifact")) return true;
        }
        // "pouh", "puch" -> matches "pouch"
        if (ContainsNoCase(needle, "pouh") || ContainsNoCase(needle, "puch"))
        {
            if (ContainsNoCase(hay, "pouch") || ContainsNoCase(normHay, "pouch")) return true;
        }
        return false;
    }
}
