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

    inline bool MatchTokenWithSynonyms(const char* hay, const char* normHay, const char* token)
    {
        if (!token || !token[0]) return true;

        if (ContainsNoCase(hay, token) || (normHay && ContainsNoCase(normHay, token)))
            return true;

        // Plural / singular 's' stripping (e.g. "helmets" -> "helmet", "boots" -> "boot")
        const size_t tlen = strlen(token);
        if (tlen > 3 && token[tlen - 1] == 's')
        {
            char stem[64]{};
            if (tlen - 1 < sizeof(stem))
            {
                memcpy(stem, token, tlen - 1);
                stem[tlen - 1] = '\0';
                if (ContainsNoCase(hay, stem) || (normHay && ContainsNoCase(normHay, stem)))
                    return true;
            }
        }

        // Helm / Helmet / Hat / Hood / Cap / Headgear / Topi synonyms
        if (ContainsNoCase(token, "helmet") || ContainsNoCase(token, "helm") ||
            ContainsNoCase(token, "hat") || ContainsNoCase(token, "topi"))
        {
            if (ContainsNoCase(hay, "helm") || ContainsNoCase(hay, "helmet") ||
                ContainsNoCase(hay, "hat") || ContainsNoCase(hay, "hood") ||
                ContainsNoCase(hay, "cap") || ContainsNoCase(hay, "mask") ||
                ContainsNoCase(hay, "headgear") || ContainsNoCase(hay, "topi") ||
                (normHay && (ContainsNoCase(normHay, "helm") || ContainsNoCase(normHay, "helmet") ||
                             ContainsNoCase(normHay, "hat") || ContainsNoCase(normHay, "hood") ||
                             ContainsNoCase(normHay, "cap") || ContainsNoCase(normHay, "mask") ||
                             ContainsNoCase(normHay, "topi"))))
                return true;
        }

        // Armor / Armour / Attire / Suit / Cuirass / Vest / Zirah
        if (ContainsNoCase(token, "armor") || ContainsNoCase(token, "armour") ||
            ContainsNoCase(token, "attire") || ContainsNoCase(token, "zirah") ||
            ContainsNoCase(token, "suit"))
        {
            if (ContainsNoCase(hay, "armor") || ContainsNoCase(hay, "armour") ||
                ContainsNoCase(hay, "attire") || ContainsNoCase(hay, "suit") ||
                ContainsNoCase(hay, "cuirass") || ContainsNoCase(hay, "plate") ||
                ContainsNoCase(hay, "mail") || ContainsNoCase(hay, "vest") ||
                ContainsNoCase(hay, "coat") || ContainsNoCase(hay, "robe") ||
                ContainsNoCase(hay, "zirah") ||
                (normHay && (ContainsNoCase(normHay, "armor") || ContainsNoCase(normHay, "armour") ||
                             ContainsNoCase(normHay, "attire") || ContainsNoCase(normHay, "suit") ||
                             ContainsNoCase(normHay, "cuirass") || ContainsNoCase(normHay, "plate") ||
                             ContainsNoCase(normHay, "mail") || ContainsNoCase(normHay, "vest") ||
                             ContainsNoCase(normHay, "zirah"))))
                return true;
        }

        // Boots / Shoes / Greaves / Footwear / Sepatu
        if (ContainsNoCase(token, "boot") || ContainsNoCase(token, "boots") ||
            ContainsNoCase(token, "shoes") || ContainsNoCase(token, "sepatu"))
        {
            if (ContainsNoCase(hay, "boot") || ContainsNoCase(hay, "boots") ||
                ContainsNoCase(hay, "shoe") || ContainsNoCase(hay, "shoes") ||
                ContainsNoCase(hay, "greave") || ContainsNoCase(hay, "greaves") ||
                ContainsNoCase(hay, "sepatu") ||
                (normHay && (ContainsNoCase(normHay, "boot") || ContainsNoCase(normHay, "boots") ||
                             ContainsNoCase(normHay, "shoe") || ContainsNoCase(normHay, "shoes") ||
                             ContainsNoCase(normHay, "greave") || ContainsNoCase(normHay, "sepatu"))))
                return true;
        }

        // Gloves / Gauntlets / Bracers / Sarung Tangan
        if (ContainsNoCase(token, "glove") || ContainsNoCase(token, "gloves") ||
            ContainsNoCase(token, "gauntlet") || ContainsNoCase(token, "gauntlets"))
        {
            if (ContainsNoCase(hay, "glove") || ContainsNoCase(hay, "gloves") ||
                ContainsNoCase(hay, "gauntlet") || ContainsNoCase(hay, "gauntlets") ||
                ContainsNoCase(hay, "touch") || ContainsNoCase(hay, "bracer") ||
                ContainsNoCase(hay, "bracers") ||
                (normHay && (ContainsNoCase(normHay, "glove") || ContainsNoCase(normHay, "gloves") ||
                             ContainsNoCase(normHay, "gauntlet") || ContainsNoCase(normHay, "touch") ||
                             ContainsNoCase(normHay, "bracer"))))
                return true;
        }

        // Cloak / Cape / Mantle / Jubah
        if (ContainsNoCase(token, "cloak") || ContainsNoCase(token, "cape") ||
            ContainsNoCase(token, "mantle") || ContainsNoCase(token, "jubah"))
        {
            if (ContainsNoCase(hay, "cloak") || ContainsNoCase(hay, "cape") ||
                ContainsNoCase(hay, "mantle") || ContainsNoCase(hay, "jubah") ||
                (normHay && (ContainsNoCase(normHay, "cloak") || ContainsNoCase(normHay, "cape") ||
                             ContainsNoCase(normHay, "mantle") || ContainsNoCase(normHay, "jubah"))))
                return true;
        }

        // Sword / Blade / Saber / Pedang
        if (ContainsNoCase(token, "sword") || ContainsNoCase(token, "blade") ||
            ContainsNoCase(token, "saber") || ContainsNoCase(token, "pedang"))
        {
            if (ContainsNoCase(hay, "sword") || ContainsNoCase(hay, "blade") ||
                ContainsNoCase(hay, "saber") || ContainsNoCase(hay, "pedang") ||
                (normHay && (ContainsNoCase(normHay, "sword") || ContainsNoCase(normHay, "blade") ||
                             ContainsNoCase(normHay, "saber") || ContainsNoCase(normHay, "pedang"))))
                return true;
        }

        // Riding / Berkuda
        if (ContainsNoCase(token, "riding") || ContainsNoCase(token, "berkuda"))
        {
            if (ContainsNoCase(hay, "riding") || ContainsNoCase(hay, "berkuda") ||
                (normHay && (ContainsNoCase(normHay, "riding") || ContainsNoCase(normHay, "berkuda"))))
                return true;
        }

        // Leather / Kulit
        if (ContainsNoCase(token, "leather") || ContainsNoCase(token, "kulit"))
        {
            if (ContainsNoCase(hay, "leather") || ContainsNoCase(hay, "kulit") ||
                (normHay && (ContainsNoCase(normHay, "leather") || ContainsNoCase(normHay, "kulit"))))
                return true;
        }

        // Existing alias & typo tolerances
        if (ContainsNoCase(token, "abbys") || ContainsNoCase(token, "abbyss") || ContainsNoCase(token, "abis"))
        {
            if (ContainsNoCase(hay, "abyss") || (normHay && ContainsNoCase(normHay, "abyss"))) return true;
        }
        if (ContainsNoCase(token, "artifak") || ContainsNoCase(token, "artifac"))
        {
            if (ContainsNoCase(hay, "artifact") || (normHay && ContainsNoCase(normHay, "artifact"))) return true;
        }
        if (ContainsNoCase(token, "pouh") || ContainsNoCase(token, "puch"))
        {
            if (ContainsNoCase(hay, "pouch") || (normHay && ContainsNoCase(normHay, "pouch"))) return true;
        }

        return false;
    }

    // Smart matcher that handles multi-word token searches (order-independent),
    // synonyms (helmet/helm/hat, armor/attire), underscores, and typos.
    inline bool SearchMatches(const char* hay, const char* needle)
    {
        if (!needle || !needle[0]) return true;
        if (!hay || !hay[0]) return false;

        // 1. Direct fast substring match
        if (ContainsNoCase(hay, needle)) return true;

        // 2. Normalized hay (replace underscores/dashes with spaces)
        char normHay[256]{};
        size_t hi = 0;
        for (const char* p = hay; *p && hi < sizeof(normHay) - 1; ++p)
        {
            if (*p == '_' || *p == '-' || *p == ' ') normHay[hi++] = ' ';
            else normHay[hi++] = *p;
        }
        normHay[hi] = '\0';

        char normNeedle[128]{};
        size_t ni = 0;
        for (const char* p = needle; *p && ni < sizeof(normNeedle) - 1; ++p)
        {
            if (*p == '_' || *p == '-' || *p == ' ') normNeedle[ni++] = ' ';
            else normNeedle[ni++] = *p;
        }
        while (ni > 0 && normNeedle[ni - 1] == ' ') normNeedle[--ni] = '\0';
        normNeedle[ni] = '\0';

        if (normNeedle[0] && ContainsNoCase(normHay, normNeedle)) return true;

        // 3. Multi-word token matching: every word in needle must match in hay
        const char* np = normNeedle;
        bool allTokensMatched = true;
        int tokenCount = 0;
        while (*np)
        {
            while (*np == ' ') ++np;
            if (!*np) break;

            char token[64]{};
            size_t ti = 0;
            while (*np && *np != ' ' && ti < sizeof(token) - 1)
            {
                token[ti++] = *np++;
            }
            token[ti] = '\0';

            if (token[0])
            {
                ++tokenCount;
                if (!MatchTokenWithSynonyms(hay, normHay, token))
                {
                    allTokensMatched = false;
                    break;
                }
            }
        }

        return (tokenCount > 0) && allTokensMatched;
    }

    // Match against item display name AND internal key combined
    inline bool SearchMatchesItem(const char* name, const char* key, const char* needle)
    {
        if (!needle || !needle[0]) return true;
        if (SearchMatches(name, needle)) return true;
        if (SearchMatches(key, needle)) return true;

        // Check against combined text so queries spanning name and key tokens also succeed
        char combined[384]{};
        snprintf(combined, sizeof(combined), "%s %s", name ? name : "", key ? key : "");
        return SearchMatches(combined, needle);
    }
}
