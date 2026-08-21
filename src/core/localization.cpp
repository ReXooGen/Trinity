#include "localization.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include "mod.h"
#include "logger.h"

namespace trinity::loc
{
    namespace
    {
        std::mutex s_mutex;
        std::vector<LanguageInfo> s_languages;
        int s_currentIndex = 0;
        std::string s_currentCode = "en";
        std::unordered_map<std::string, std::string> s_translations;

        std::string GetModuleDir()
        {
            char path[MAX_PATH]{};
            const DWORD n = GetModuleFileNameA(Mod::Get().Module(), path, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return "";
            char* slash = strrchr(path, '\\');
            if (!slash)
                return "";
            *(slash + 1) = '\0';
            return std::string(path);
        }

        std::string StripBOM(const std::string& str)
        {
            if (str.size() >= 3 &&
                static_cast<unsigned char>(str[0]) == 0xEF &&
                static_cast<unsigned char>(str[1]) == 0xBB &&
                static_cast<unsigned char>(str[2]) == 0xBF)
            {
                return str.substr(3);
            }
            return str;
        }

        std::string Trim(const std::string& str)
        {
            std::string s = StripBOM(str);
            const size_t first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            const size_t last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, (last - first + 1));
        }

        void LoadLanguageFile(const std::string& filePath)
        {
            s_translations.clear();
            if (filePath.empty())
                return; // English / built-in

            std::ifstream file(filePath);
            if (!file.is_open())
            {
                LOG_WARN("localization: failed to open language file: %s", filePath.c_str());
                return;
            }

            std::string line;
            std::string currentSection = "";
            while (std::getline(file, line))
            {
                std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                    continue;

                if (trimmed.front() == '[' && trimmed.back() == ']')
                {
                    currentSection = Trim(trimmed.substr(1, trimmed.length() - 2));
                    continue;
                }

                const size_t eq = trimmed.find('=');
                if (eq != std::string::npos)
                {
                    std::string key = Trim(trimmed.substr(0, eq));
                    std::string val = Trim(trimmed.substr(eq + 1));
                    if (!key.empty() && !val.empty())
                    {
                        if (currentSection == "UI" || currentSection.empty())
                        {
                            s_translations[key] = val;
                        }
                    }
                }
            }
            LOG_OK("localization: loaded %zu translations from %s", s_translations.size(), filePath.c_str());
        }

        bool ReadLanguageHeader(const std::string& filePath, std::string& outName, std::string& outCode)
        {
            const size_t slash = filePath.find_last_of("\\/");
            std::string fname = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;

            // Strict blacklist for known non-language configuration files
            if (fname == "Trinity.ini" || fname == "Trinity.ini.example" ||
                fname.find("Profile") != std::string::npos ||
                fname.find("Dye") != std::string::npos ||
                fname.find("Equipment") != std::string::npos ||
                fname.find("Preset") != std::string::npos ||
                fname.find("Setting") != std::string::npos ||
                fname.find("Config") != std::string::npos)
            {
                return false;
            }

            std::ifstream file(filePath);
            if (!file.is_open())
                return false;

            std::string line;
            std::string currentSection = "";
            bool hasLanguageSection = false;
            while (std::getline(file, line))
            {
                std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                    continue;

                if (trimmed.front() == '[' && trimmed.back() == ']')
                {
                    currentSection = Trim(trimmed.substr(1, trimmed.length() - 2));
                    if (currentSection == "Language")
                        hasLanguageSection = true;
                    continue;
                }

                if (currentSection == "Language")
                {
                    const size_t eq = trimmed.find('=');
                    if (eq != std::string::npos)
                    {
                        std::string key = Trim(trimmed.substr(0, eq));
                        std::string val = Trim(trimmed.substr(eq + 1));
                        if (key == "Name") outName = val;
                        else if (key == "Code") outCode = val;
                    }
                }
            }

            // Derive code from filename (e.g. Trinity_zh.ini -> zh) if not set
            if (outCode.empty())
            {
                const size_t under = fname.rfind('_');
                const size_t dot = fname.rfind('.');
                if (under != std::string::npos && dot != std::string::npos && dot > under)
                    outCode = fname.substr(under + 1, dot - under - 1);
            }

            // Only accept if explicit [Language] section exists OR filename has a recognized language suffix
            if (!hasLanguageSection)
            {
                if (outCode != "zh" && outCode != "ko" && outCode != "ja" &&
                    outCode != "es" && outCode != "ru" && outCode != "ptbr" &&
                    outCode != "pt" && outCode != "id" && outCode != "de" &&
                    outCode != "fr")
                {
                    return false;
                }
            }

            if (outName.empty())
            {
                if (outCode == "ko") outName = "한국어 (Korean)";
                else if (outCode == "zh") outName = "简体中文 (Chinese)";
                else if (outCode == "ja") outName = "日本語 (Japanese)";
                else if (outCode == "es") outName = "Español (Spanish)";
                else if (outCode == "ru") outName = "Русский (Russian)";
                else if (outCode == "ptbr" || outCode == "pt") outName = "Português (Portuguese)";
                else if (outCode == "id") outName = "Bahasa Indonesia";
                else if (outCode == "de") outName = "Deutsch (German)";
                else if (outCode == "fr") outName = "Français (French)";
                else outName = fname;
            }

            return true;
        }
    }

    void Init()
    {
        RefreshLanguages();
    }

    void RefreshLanguages()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_languages.clear();

        // 0: Always Built-in English
        LanguageInfo en;
        en.name = "English";
        en.code = "en";
        en.filePath = "";
        s_languages.push_back(en);

        const std::string dir = GetModuleDir();
        if (!dir.empty())
        {
            auto ScanPattern = [&](const std::string& pattern, const std::string& baseDir) {
                WIN32_FIND_DATAA fd{};
                HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            const std::string fullPath = baseDir + fd.cFileName;
                            std::string name, code;
                            if (ReadLanguageHeader(fullPath, name, code))
                            {
                                // Prevent duplicates by code
                                bool exists = false;
                                for (const auto& existing : s_languages)
                                {
                                    if (existing.code == code) { exists = true; break; }
                                }
                                if (!exists)
                                {
                                    LanguageInfo lang;
                                    lang.name = name;
                                    lang.code = code;
                                    lang.filePath = fullPath;
                                    s_languages.push_back(lang);
                                }
                            }
                        }
                    } while (FindNextFileA(hFind, &fd));
                    FindClose(hFind);
                }
            };

            ScanPattern(dir + "Trinity_*.ini", dir);
            ScanPattern(dir + "Languages\\Trinity_*.ini", dir + "Languages\\");
            ScanPattern(dir + "languages\\Trinity_*.ini", dir + "languages\\");
        }

        LOG_OK("localization: discovered %zu language(s)", s_languages.size());

        // Restore current language or fallback to English
        int foundIdx = 0;
        for (size_t i = 0; i < s_languages.size(); ++i)
        {
            if (s_languages[i].code == s_currentCode)
            {
                foundIdx = static_cast<int>(i);
                break;
            }
        }
        s_currentIndex = foundIdx;
        s_currentCode = s_languages[foundIdx].code;
        LoadLanguageFile(s_languages[foundIdx].filePath);
    }

    const std::vector<LanguageInfo>& GetLanguages()
    {
        return s_languages;
    }

    int GetLanguageCount()
    {
        return static_cast<int>(s_languages.size());
    }

    const char* GetLanguageName(int index)
    {
        if (index >= 0 && index < static_cast<int>(s_languages.size()))
            return s_languages[index].name.c_str();
        return "English";
    }

    const char* GetLanguageCode(int index)
    {
        if (index >= 0 && index < static_cast<int>(s_languages.size()))
            return s_languages[index].code.c_str();
        return "en";
    }

    int GetCurrentLanguageIndex()
    {
        return s_currentIndex;
    }

    void SetLanguage(int index)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (index < 0 || index >= static_cast<int>(s_languages.size()))
            index = 0;

        s_currentIndex = index;
        s_currentCode = s_languages[index].code;
        LoadLanguageFile(s_languages[index].filePath);
    }

    void SetLanguageByCode(const char* code)
    {
        if (!code || code[0] == '\0')
            return;

        std::lock_guard<std::mutex> lock(s_mutex);
        for (size_t i = 0; i < s_languages.size(); ++i)
        {
            if (s_languages[i].code == code)
            {
                s_currentIndex = static_cast<int>(i);
                s_currentCode = s_languages[i].code;
                LoadLanguageFile(s_languages[i].filePath);
                return;
            }
        }
        // Fallback to English if code not found
        s_currentIndex = 0;
        s_currentCode = "en";
        LoadLanguageFile("");
    }

    const char* Tr(const char* text)
    {
        if (!text || text[0] == '\0' || s_currentIndex == 0)
            return text;

        auto it = s_translations.find(text);
        if (it != s_translations.end())
            return it->second.c_str();

        return text;
    }
}
