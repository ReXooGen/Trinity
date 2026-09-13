#pragma once
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace trinity
{
    // High-performance Console & File logger for Trinity.
    // Features:
    //  - Millisecond precision timestamps with Thread ID tracking
    //  - Automatic anti-spam consecutive message deduplication ("Repeated X times")
    //  - Rate-limiting throttle (LOG_THROTTLE) & once-per-session logging (LOG_ONCE)
    //  - Thread-safe, non-blocking file streaming & color-coded console output
    class Logger
    {
    public:
        // CombatVerbose: console-only, NEVER written to file (prevents 72GB log runaway)
        enum Level { Debug, Info, Good, Warn, Error, CombatVerbose };

        // Hard cap: 50 MB. When exceeded, current log is rotated to Trinity.1.log and a fresh file begins.
        static constexpr size_t kMaxLogBytes = 50ULL * 1024ULL * 1024ULL;

        static void InitFileLogging(HMODULE module)
        {
            std::lock_guard<std::mutex> lock(Mutex());
            if (s_logFp) return;

            char path[MAX_PATH]{};
            if (module && GetModuleFileNameA(module, path, MAX_PATH))
            {
                char* slash = strrchr(path, '\\');
                if (!slash) slash = strrchr(path, '/');
                if (slash)
                {
                    strcpy_s(slash + 1, static_cast<size_t>(path + MAX_PATH - slash - 1), "Trinity.log");
                    s_logPath = path;
                    TruncateOversizedLog(path);
                    s_logFp = _fsopen(path, "a", _SH_DENYNO);
                    s_logBytesWritten = GetLogFileSize(path);
                }
            }

            if (s_logFp)
            {
                for (const auto& line : s_buffer)
                    EmitToFile(line);
            }
        }

        static void EnableConsole(bool showConsole, bool fileLogging)
        {
            std::lock_guard<std::mutex> lock(Mutex());
            
            if (showConsole && !s_console)
            {
                AllocConsole();
                freopen_s(&s_conFp, "CONOUT$", "w", stdout);
                SetConsoleTitleA("Trinity - Crimson Desert");
                s_console = true;
            }

            HMODULE module = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&EnableConsole), &module);
            if (fileLogging && module && !s_logFp)
            {
                char path[MAX_PATH]{};
                if (GetModuleFileNameA(module, path, MAX_PATH))
                {
                    char* slash = strrchr(path, '\\');
                    if (!slash) slash = strrchr(path, '/');
                    if (slash)
                    {
                        strcpy_s(slash + 1, static_cast<size_t>(path + MAX_PATH - slash - 1),
                                 "Trinity.log");
                        s_logPath = path;
                        TruncateOversizedLog(path);
                        s_logFp = _fsopen(path, "a", _SH_DENYNO);
                        s_logBytesWritten = GetLogFileSize(path);
                    }
                }
            }

            for (const auto& line : s_buffer)
                Emit(line);
            s_buffer.clear();
        }

        static void DisableConsole()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            if (!s_console) return;

            if (s_conFp) { fclose(s_conFp); s_conFp = nullptr; }
            FreeConsole();
            s_console = false;
        }

        static void Shutdown()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            FlushDeduplication();
            if (s_conFp)   { fclose(s_conFp); s_conFp = nullptr; }
            if (s_logFp)   { fclose(s_logFp); s_logFp = nullptr; }
            if (s_console) { FreeConsole(); s_console = false; }
        }

        static void Log(Level lvl, const char* fmt, ...)
        {
            char msg[2048];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            LogInternal(lvl, msg);
        }

        static void LogThrottled(uint32_t intervalMs, Level lvl, const char* fmt, ...)
        {
            char msg[2048];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            const ULONGLONG now = GetTickCount64();
            std::lock_guard<std::mutex> lock(Mutex());
            auto& last = s_throttleMap[msg];
            if (now - last < intervalMs)
                return;
            last = now;

            LogInternalLocked(lvl, msg);
        }

        static void LogOnce(Level lvl, const char* fmt, ...)
        {
            char msg[2048];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            std::lock_guard<std::mutex> lock(Mutex());
            if (s_loggedOnce.find(msg) != s_loggedOnce.end())
                return;
            s_loggedOnce.insert(msg);

            LogInternalLocked(lvl, msg);
        }

    private:
        struct Line
        {
            Level lvl = Info;
            std::string stamp;
            DWORD tid = 0;
            std::string text;
        };

        static void LogInternal(Level lvl, const char* msg)
        {
            std::lock_guard<std::mutex> lock(Mutex());
            LogInternalLocked(lvl, msg);
        }

        static void LogInternalLocked(Level lvl, const char* msg)
        {
            // CombatVerbose messages are console-only and NEVER touch the file.
            // This is the critical guard against 72GB runaway log files.
            const bool fileEligible = (lvl != CombatVerbose);

            const ULONGLONG now = GetTickCount64();
            // Suppress rapid identical spam within 3 seconds
            if (s_lastMessage == msg && s_lastLevel == lvl && (now - s_lastTime < 3000))
            {
                ++s_repeatCount;
                s_lastTime = now;
                return;
            }

            FlushDeduplication();

            s_lastMessage = msg;
            s_lastLevel = lvl;
            s_lastTime = now;
            s_repeatCount = 0;

            Line line;
            line.lvl = lvl;
            line.tid = GetCurrentThreadId();

            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[32];
            snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            line.stamp = stamp;
            line.text  = msg;

            if (s_console)
            {
                // CombatVerbose: write to console only, never call EmitToFile
                if (fileEligible)
                    Emit(line);
                else
                    EmitConsoleOnly(line);
            }
            else
            {
                s_buffer.emplace_back(std::move(line));
                if (s_buffer.size() > 512)
                    s_buffer.pop_front();
                if (s_logFp && fileEligible)
                    EmitToFile(s_buffer.back());
            }
        }

        static void FlushDeduplication()
        {
            if (s_repeatCount > 0)
            {
                char repeatMsg[128];
                snprintf(repeatMsg, sizeof(repeatMsg), "--- [Previous message repeated %u times] ---", s_repeatCount);

                Line repLine;
                repLine.lvl = Level::Info;
                repLine.tid = GetCurrentThreadId();
                SYSTEMTIME st;
                GetLocalTime(&st);
                char stamp[32];
                snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
                repLine.stamp = stamp;
                repLine.text = repeatMsg;

                if (s_console)
                    Emit(repLine);
                else if (s_logFp)
                    EmitToFile(repLine);

                s_repeatCount = 0;
            }
        }

        // Returns current file size in bytes (0 if not found)
        static size_t GetLogFileSize(const char* path)
        {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &info))
                return (static_cast<size_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            return 0;
        }

        // If the log file already exceeds the cap (e.g. leftover from last session), delete it.
        static void TruncateOversizedLog(const char* path)
        {
            if (GetLogFileSize(path) > kMaxLogBytes)
                DeleteFileA(path);
        }

        // Rotate: Trinity.log → Trinity.1.log, then open a fresh Trinity.log
        static void RotateLog()
        {
            if (!s_logFp || s_logPath.empty()) return;
            fclose(s_logFp);
            s_logFp = nullptr;

            std::string rotated = s_logPath;
            // Replace .log with .1.log
            const size_t dot = rotated.rfind('.');
            if (dot != std::string::npos)
                rotated.insert(dot, ".1");
            else
                rotated += ".1";

            DeleteFileA(rotated.c_str());                     // Remove old rotation
            MoveFileExA(s_logPath.c_str(), rotated.c_str(),  // Rename current → .1
                        MOVEFILE_REPLACE_EXISTING);

            s_logFp = _fsopen(s_logPath.c_str(), "w", _SH_DENYNO);
            s_logBytesWritten = 0;

            // Write a rotation notice at the top of the new file
            if (s_logFp)
            {
                const char* notice = "[LOGGER] Log rotated – previous log saved to Trinity.1.log (50MB cap)\n";
                std::fputs(notice, s_logFp);
                s_logBytesWritten += strlen(notice);
                std::fflush(s_logFp);
            }
        }

        static void EmitToFile(const Line& l)
        {
            if (!s_logFp) return;
            static const char* names[] = { "DEBUG", "INFO", "OK", "WARN", "ERROR", "VERBOSE" };
            char buf[2176];
            const int n = std::snprintf(buf, sizeof(buf), "%s [TID %lu] [%s] %s\n",
                                        l.stamp.c_str(), l.tid, names[l.lvl], l.text.c_str());
            if (n <= 0) return;

            // Enforce 50MB cap: rotate before write if needed
            if (s_logBytesWritten + static_cast<size_t>(n) > kMaxLogBytes)
                RotateLog();

            if (s_logFp)
            {
                std::fputs(buf, s_logFp);
                std::fflush(s_logFp);
                s_logBytesWritten += static_cast<size_t>(n);
            }
        }

        static WORD LevelColor(Level lvl)
        {
            switch (lvl)
            {
            case Debug:         return FOREGROUND_INTENSITY;
            case Good:          return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case Warn:          return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case Error:         return FOREGROUND_RED | FOREGROUND_INTENSITY;
            case CombatVerbose: return FOREGROUND_BLUE | FOREGROUND_GREEN; // Teal – visually distinct
            default:            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            }
        }

        // Console-only emit: no file write.
        static void EmitConsoleOnly(const Line& l)
        {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(h, FOREGROUND_INTENSITY);
            std::printf("%s ", l.stamp.c_str());
            SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::printf("Trinity ");
            SetConsoleTextAttribute(h, LevelColor(l.lvl));
            std::printf("%s\n", l.text.c_str());
            SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::fflush(stdout);
        }

        static void Emit(const Line& l)
        {
            EmitConsoleOnly(l);
            EmitToFile(l);   // EmitToFile will skip CombatVerbose (caller must ensure fileEligible)
        }

        static std::mutex& Mutex()
        {
            static std::mutex m;
            return m;
        }

        static inline FILE*                                         s_conFp = nullptr;
        static inline FILE*                                         s_logFp = nullptr;
        static inline bool                                          s_console = false;
        static inline std::deque<Line>                              s_buffer;
        static inline std::string                                   s_lastMessage;
        static inline Level                                         s_lastLevel = Info;
        static inline ULONGLONG                                     s_lastTime = 0;
        static inline uint32_t                                      s_repeatCount = 0;
        static inline std::unordered_map<std::string, ULONGLONG>    s_throttleMap;
        static inline std::unordered_set<std::string>               s_loggedOnce;
        static inline std::string                                   s_logPath;
        static inline size_t                                        s_logBytesWritten = 0;
    };
}

#define LOG(...)           ::trinity::Logger::Log(::trinity::Logger::Info,  __VA_ARGS__)
#define LOG_INFO(...)      ::trinity::Logger::Log(::trinity::Logger::Info,  __VA_ARGS__)
#define LOG_DEBUG(...)     ::trinity::Logger::Log(::trinity::Logger::Debug, __VA_ARGS__)
#define LOG_OK(...)        ::trinity::Logger::Log(::trinity::Logger::Good,  __VA_ARGS__)
#define LOG_WARN(...)      ::trinity::Logger::Log(::trinity::Logger::Warn,  __VA_ARGS__)
#define LOG_ERR(...)       ::trinity::Logger::Log(::trinity::Logger::Error, __VA_ARGS__)
#define LOG_ONCE(...)      ::trinity::Logger::LogOnce(::trinity::Logger::Info, __VA_ARGS__)
#define LOG_WARN_ONCE(...) ::trinity::Logger::LogOnce(::trinity::Logger::Warn, __VA_ARGS__)
#define LOG_THROTTLE(intervalMs, ...) ::trinity::Logger::LogThrottled(intervalMs, ::trinity::Logger::Info, __VA_ARGS__)
// LOG_COMBAT: visible on console, NEVER written to file. Use for per-frame/per-hit events.
#define LOG_COMBAT(...)    ::trinity::Logger::Log(::trinity::Logger::CombatVerbose, __VA_ARGS__)
