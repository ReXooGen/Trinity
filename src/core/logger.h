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
        enum Level { Debug, Info, Good, Warn, Error };

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
                    s_logFp = _fsopen(path, "w", _SH_DENYNO);
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
                        s_logFp = _fsopen(path, "w", _SH_DENYNO);
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
                Emit(line);
            }
            else
            {
                s_buffer.emplace_back(std::move(line));
                if (s_buffer.size() > 512)
                    s_buffer.pop_front();
                if (s_logFp)
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

        static void EmitToFile(const Line& l)
        {
            if (!s_logFp) return;
            static const char* names[] = { "DEBUG", "INFO", "OK", "WARN", "ERROR" };
            std::fprintf(s_logFp, "%s [TID %lu] [%s] %s\n", l.stamp.c_str(), l.tid, names[l.lvl], l.text.c_str());
            if (l.lvl >= Warn)
                std::fflush(s_logFp);
        }

        static void Emit(const Line& l)
        {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD body = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            switch (l.lvl)
            {
            case Debug: body = FOREGROUND_INTENSITY; break;
            case Good:  body = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
            case Warn:  body = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
            case Error: body = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
            default: break;
            }

            SetConsoleTextAttribute(h, FOREGROUND_INTENSITY);
            std::printf("%s ", l.stamp.c_str());
            SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::printf("Trinity ");
            SetConsoleTextAttribute(h, body);
            std::printf("%s\n", l.text.c_str());
            SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::fflush(stdout);

            EmitToFile(l);
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
    };
}

#define LOG(...)           ::trinity::Logger::Log(::trinity::Logger::Info,  __VA_ARGS__)
#define LOG_DEBUG(...)     ::trinity::Logger::Log(::trinity::Logger::Debug, __VA_ARGS__)
#define LOG_OK(...)        ::trinity::Logger::Log(::trinity::Logger::Good,  __VA_ARGS__)
#define LOG_WARN(...)      ::trinity::Logger::Log(::trinity::Logger::Warn,  __VA_ARGS__)
#define LOG_ERR(...)       ::trinity::Logger::Log(::trinity::Logger::Error, __VA_ARGS__)
#define LOG_ONCE(...)      ::trinity::Logger::LogOnce(::trinity::Logger::Info, __VA_ARGS__)
#define LOG_WARN_ONCE(...) ::trinity::Logger::LogOnce(::trinity::Logger::Warn, __VA_ARGS__)
#define LOG_THROTTLE(intervalMs, ...) ::trinity::Logger::LogThrottled(intervalMs, ::trinity::Logger::Info, __VA_ARGS__)
