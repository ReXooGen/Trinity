#pragma once

#include <string>
#include <vector>

namespace trinity::loc
{
    struct LanguageInfo
    {
        std::string name;     // Display name, e.g. "English", "简体中文", "한국어"
        std::string code;     // Language code, e.g. "en", "zh", "ko"
        std::string filePath; // Full path to the .ini file (empty for built-in English)
    };

    // Initializes and scans available language files (Trinity_*.ini) beside Trinity.asi
    void Init();

    // Re-scans language files
    void RefreshLanguages();

    // Returns list of discovered languages (always starts with built-in English)
    const std::vector<LanguageInfo>& GetLanguages();
    int GetLanguageCount();
    const char* GetLanguageName(int index);
    const char* GetLanguageCode(int index);

    // Get / Set active language
    int GetCurrentLanguageIndex();
    void SetLanguage(int index);
    void SetLanguageByCode(const char* code);

    // Translates a UI string. If not found or English, returns the original text.
    const char* Tr(const char* text);
}

// Convenience macro for UI localization
#define LOC(str) trinity::loc::Tr(str)
