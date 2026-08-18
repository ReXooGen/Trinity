#pragma once
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace trinity
{
    // Console logger. The console is created lazily (only the process that
    // actually renders the game calls EnableConsole), so the launcher process
    // that also loads the ASI stays silent. Lines logged before the console
    // exists are buffered and flushed into it on creation.
    class Logger
    {
    public:
        enum Level { Info, Good, Warn, Error };

        static void EnableConsole(bool fileLogging)
        {
            std::lock_guard<std::mutex> lock(Mutex());
            if (s_console)
                return;

            AllocConsole();
            freopen_s(&s_conFp, "CONOUT$", "w", stdout);
            SetConsoleTitleA("Trinity");
            s_console = true;

            HMODULE module = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&EnableConsole), &module);
            if (fileLogging && module)
            {
                char path[MAX_PATH]{};
                if (GetModuleFileNameA(module, path, MAX_PATH))
                {
                    char* slash = strrchr(path, '\\');
                    if (slash)
                    {
                        strcpy_s(slash + 1, static_cast<size_t>(path + MAX_PATH - slash - 1),
                                 "Trinity.log");
                        // One complete, bounded log per game session.
                        s_logFp = _fsopen(path, "w", _SH_DENYNO);
                    }
                }
            }

            for (const auto& line : s_buffer)
                Emit(line);
            s_buffer.clear();
        }

        static void Shutdown()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            if (s_conFp)   { fclose(s_conFp); s_conFp = nullptr; }
            if (s_logFp)   { fclose(s_logFp); s_logFp = nullptr; }
            if (s_console) { FreeConsole(); s_console = false; }
        }

        static void Log(Level lvl, const char* fmt, ...)
        {
            char msg[1024];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(msg, sizeof(msg), fmt, ap);
            va_end(ap);

            Line line;
            line.lvl = lvl;
            SYSTEMTIME st;
            GetLocalTime(&st);
            char stamp[16];
            snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            line.stamp = stamp;
            line.text  = msg;

            std::lock_guard<std::mutex> lock(Mutex());
            if (s_console)
            {
                Emit(line);
            }
            else
            {
                s_buffer.emplace_back(std::move(line));
                if (s_buffer.size() > 256)
                    s_buffer.pop_front();
            }
        }

    private:
        struct Line { Level lvl = Info; std::string stamp, text; };

        static void Emit(const Line& l)
        {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD body = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            switch (l.lvl)
            {
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

            if (s_logFp)
            {
                static const char* names[] = { "INFO", "OK", "WARN", "ERROR" };
                std::fprintf(s_logFp, "%s [%s] Trinity %s\n", l.stamp.c_str(), names[l.lvl],
                             l.text.c_str());
                std::fflush(s_logFp);
            }
        }

        static std::mutex& Mutex()
        {
            static std::mutex m;
            return m;
        }

        static inline FILE*            s_conFp   = nullptr;
        static inline FILE*            s_logFp   = nullptr;
        static inline bool             s_console = false;
        static inline std::deque<Line> s_buffer;
    };
}

#define LOG(...)      ::trinity::Logger::Log(::trinity::Logger::Info,  __VA_ARGS__)
#define LOG_OK(...)   ::trinity::Logger::Log(::trinity::Logger::Good,  __VA_ARGS__)
#define LOG_WARN(...) ::trinity::Logger::Log(::trinity::Logger::Warn,  __VA_ARGS__)
#define LOG_ERR(...)  ::trinity::Logger::Log(::trinity::Logger::Error, __VA_ARGS__)
