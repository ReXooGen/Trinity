#pragma once
#include <Windows.h>
#include <atomic>
#include "logger.h"

namespace trinity::core
{
    enum class TickPart { Player, World, Inventory, Dye, Equipment, Friendly, Count };
    struct TickMetrics
    {
        inline static std::atomic<bool> enabled{false};
        inline static std::atomic<ULONGLONG> nextReport{0};
        inline static std::atomic<LONGLONG> maxima[static_cast<unsigned>(TickPart::Count)]{};
        inline static std::atomic<LONGLONG> overlayWait{0};
        inline static const LONGLONG frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
        static void SetEnabled(bool value)
        {
            if (enabled.load(std::memory_order_acquire) == value) return;
            enabled.store(false, std::memory_order_release);
            for (auto& maximum : maxima) maximum.store(0, std::memory_order_relaxed);
            overlayWait.store(0, std::memory_order_relaxed);
            nextReport.store(GetTickCount64() + 10000, std::memory_order_relaxed);
            enabled.store(value, std::memory_order_release);
        }
        static LONGLONG Now()
        {
            // Always raw: call sites pass the value straight into Record(); the
            // old 0-when-disabled gate made Record a no-op and left the game
            // thread unmeasured during normal play.
            LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart;
        }
        static void Record(TickPart part, LONGLONG start)
        {
            const LONGLONG end = NowRaw();
            if (!start || end < start) return;
            const LONGLONG elapsed = end - start;
            auto& maximum = maxima[static_cast<unsigned>(part)];
            LONGLONG old = maximum.load(std::memory_order_relaxed);
            while (elapsed > old && !maximum.compare_exchange_weak(old, elapsed, std::memory_order_relaxed)) {}
        }
        // Raw QPC regardless of the perfLogging opt-in: the maxima are cheap
        // (one atomic max per part) and play-time stutter is invisible without
        // them - the opt-in only ever measured menu sessions.
        static LONGLONG NowRaw()
        {
            LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart;
        }
        static void RecordOverlayWait(LONGLONG start)
        {
            const LONGLONG end = Now();
            if (!start || end < start) return;
            const LONGLONG elapsed = end - start;
            LONGLONG old = overlayWait.load(std::memory_order_relaxed);
            while (elapsed > old && !overlayWait.compare_exchange_weak(old, elapsed, std::memory_order_relaxed)) {}
        }
        // Called by render, not the measured game tick. One summary per 10s,
        // logged only while a part is hot (>= 2 ms) - always-on collection,
        // quiet output. The full per-part line stays available for opt-in
        // diagnostics via the Performance Diagnostics toggle.
        static void Report()
        {
            const bool verbose = enabled.load(std::memory_order_acquire);
            const ULONGLONG now = GetTickCount64();
            ULONGLONG due = nextReport.load(std::memory_order_relaxed);
            if (now < due || !frequency || !nextReport.compare_exchange_strong(due, now + 10000, std::memory_order_relaxed)) return;
            double values[6]{};
            bool hot = false;
            for (unsigned i = 0; i < 6; ++i)
            {
                values[i] = maxima[i].exchange(0, std::memory_order_relaxed) * 1000.0 / frequency;
                hot |= values[i] >= 2.0;
            }
            // The hot summary is a diagnostics tool: gated behind Performance
            // Diagnostics so normal sessions log nothing from this path.
            if (verbose)
                LOG_INFO("perf: game-tick max/10s ms player=%.3f world=%.3f inventory=%.3f dye=%.3f equipment=%.3f friendly=%.3f",
                         values[0], values[1], values[2], values[3], values[4], values[5]);
            const double waitMs = overlayWait.exchange(0, std::memory_order_relaxed) * 1000.0 / frequency;
            if (verbose) LOG_INFO("perf: overlay fence-wait max/10s ms=%.3f", waitMs);
        }
    };
}
