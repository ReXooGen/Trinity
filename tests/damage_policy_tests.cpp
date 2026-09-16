#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <thread>

#include "game/damage_policy.h"

namespace
{
    using namespace trinity::game::damage_policy;

    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    OutgoingHit MortalHit()
    {
        OutgoingHit hit{};
        hit.statusId = hit.healthStatusId = 427; // Deliberately different from legacy Health=0.
        hit.healthStatusKnown = true;
        hit.source = hit.verifiedPlayerOwner = 0x123456780ULL;
        hit.victimRoot = 0x234567890ULL;
        hit.time = 987654321;
        hit.context = {hit.source, hit.victimRoot, hit.time, true, false};
        hit.flags = {0, 0, 8, 10, 0}; // Verified SwordMastery_Attack_I/II native tuple.
        hit.enemyTarget = true;
        hit.health = {true, 100000, 0, false};
        return hit;
    }

    void ExpectNative(const OutgoingHit& hit, int64_t delta)
    {
        // Protection must win even with BOTH damage features enabled, and when OHK is off.
        Check(ApplyOutgoing(hit, delta, true, 1000.0f) == delta,
              "OHK changed a protected/unclassified native delta");
        Check(ApplyOutgoing(hit, delta, false, 1000.0f) == delta,
              "Multiplier bypassed native protection/classification");
    }

    void TestCaptureProgression()
    {
        auto hit = MortalHit();
        for (int64_t floor : {int64_t{1}, int64_t{1000}, int64_t{100000}})
        {
            hit.health.floor = floor;
            for (int64_t hp : {floor - 1, floor, floor + 1, floor + 50000})
            {
                hit.health.current = hp;
                ExpectNative(hit, -7); // Old code forced HP to floor or changed this to zero.
            }
        }
        hit = MortalHit();
        hit.health.nativeProtected = true; // State protection with ZERO floor must also win.
        ExpectNative(hit, -7);
        hit.health.nativeProtected = false;
        hit.health.verified = false; // Failed floor/state reads cannot mean "mortal".
        ExpectNative(hit, -7);
        hit = MortalHit();
        hit.context.noDead = true;
        for (int64_t hp : {int64_t{1}, int64_t{999}, int64_t{1000}, int64_t{1001}, int64_t{100000}})
        {
            hit.health.current = hp;
            // Native's NoDead cap can already have reduced the hit to zero. Preserve
            // its exact progression even with no entry floor and ordinary cause (8,10).
            const int64_t capped = hp < 2000 ? 0 : -(hp / 1000 * 1000 - 1000);
            ExpectNative(hit, capped);
            ExpectNative(hit, -1);
        }
    }

    void TestIdentityAndStatus()
    {
        auto hit = MortalHit();
        for (uint16_t status : {uint16_t{0}, uint16_t{17}, uint16_t{48},
                                uint16_t{49}, uint16_t{428}, uint16_t{0xFFFF}})
        {
            hit.statusId = status;
            ExpectNative(hit, -10);
        }
        hit = MortalHit();
        hit.healthStatusKnown = false;
        ExpectNative(hit, -10);
        hit.healthStatusKnown = true;
        hit.statusId = hit.healthStatusId = 0xFFFF;
        ExpectNative(hit, -10);
        hit = MortalHit();
        hit.source = 0; // Old !isEnemyAttacker admitted environmental/script damage.
        ExpectNative(hit, -10);
        hit.source = hit.verifiedPlayerOwner + 8; // A component/weapon is not its owner.
        ExpectNative(hit, -10);
        hit = MortalHit();
        hit.verifiedPlayerOwner = 0;
        ExpectNative(hit, -10);
        hit.source = 0; // Two null pointers must never establish player attribution.
        ExpectNative(hit, -10);
        hit = MortalHit();
        hit.enemyTarget = false;
        ExpectNative(hit, -10);
    }

    void TestSpecialFlags()
    {
        // No flag value may turn NoDead or missing provenance into a lethal hit.
        for (unsigned value = 0; value <= 255; ++value)
        {
            for (int position = 0; position != 5; ++position)
            {
                auto hit = MortalHit();
                uint8_t* fields[] = {&hit.flags.a6, &hit.flags.a7, &hit.flags.a8,
                                     &hit.flags.a9, &hit.flags.a10};
                *fields[position] = static_cast<uint8_t>(value);
                hit.context.noDead = true;
                ExpectNative(hit, -1);
                hit.context.noDead = false;
                hit.context.definitionVerified = false;
                ExpectNative(hit, -1);
            }
        }
        auto hit = MortalHit();
        Check(ApplyOutgoing(hit, -100, true, 1.0f) == -100000,
              "Normal sword (8,10) OHK is disabled");
        hit.flags.a7 = 1;
        Check(ApplyOutgoing(hit, -100, false, 3.0f) == -300,
              "Native calculation-result flag blocked ordinary outgoing multiplier");
        hit.context.noDead = true;
        ExpectNative(hit, -100);
        // Observed special caller tuples plus invalid/high-bit control bytes.
        const NativeFlags special[] = {
            {1, 0, 8, 10, 0}, {0, 2, 8, 10, 0}, {0, 0, 0x13, 10, 0},
            {0, 0, 0xE, 10, 1}, {0, 0, 8, 9, 0}, {0x80, 0, 8, 10, 0},
            {0, 0, 0xFF, 0xFF, 0xFF}
        };
        for (NativeFlags flags : special)
        {
            hit = MortalHit();
            hit.flags = flags;
            ExpectNative(hit, -100);
        }
    }

    void TestContextAttribution()
    {
        auto hit = MortalHit();
        hit.context = {};
        ExpectNative(hit, -1);
        hit.flags = {}; // Zero flags are not proof of lethal combat either.
        ExpectNative(hit, -1);
        hit = MortalHit();
        hit.context.definitionVerified = false;
        ExpectNative(hit, -1);
        hit = MortalHit();
        ++hit.context.source;
        ExpectNative(hit, -1);
        hit = MortalHit();
        ++hit.context.victimRoot;
        ExpectNative(hit, -1);
        hit = MortalHit();
        ++hit.context.time;
        ExpectNative(hit, -1);
        hit = MortalHit();
        hit.victimRoot = hit.context.victimRoot = 0;
        ExpectNative(hit, -1);
    }

    thread_local const ScopedHitContext* contextHead = nullptr;

    void TestNestedContextLifetime()
    {
        auto hit = MortalHit();
        const auto applyTop = [&]() {
            auto classified = hit;
            classified.context = contextHead ? contextHead->Context() : HitContext{};
            return ApplyOutgoing(classified, -7, true, 1.0f);
        };
        Check(applyTop() == -7, "Unscoped hit inherited permission");
        {
            ScopedHitContext outer(contextHead, hit.context);
            Check(applyTop() == -100000, "Verified outer sword lost permission");
            {
                auto noDead = hit.context;
                noDead.noDead = true;
                ScopedHitContext inner(contextHead, noDead);
                Check(applyTop() == -7, "Same source/victim nested NoDead borrowed outer permission");
                {
                    ScopedHitContext lethal(contextHead, hit.context);
                    Check(applyTop() == -100000, "Distinct verified nested lethal hit was globally disabled");
                }
                Check(applyTop() == -7, "Nested lethal return erased enclosing NoDead protection");
            }
            Check(applyTop() == -100000, "Nested return failed to restore outer context");
            {
                ScopedHitContext unknown(contextHead, {});
                Check(applyTop() == -7, "Unknown nested hit fell back to its parent");
            }
            try
            {
                ScopedHitContext inner(contextHead, {});
                throw std::runtime_error("simulated native C++ exception");
            }
            catch (const std::runtime_error&) {}
            Check(applyTop() == -100000, "Exception left a stale context");

            bool isolated = false;
            std::thread other([&] {
                isolated = contextHead == nullptr;
                ScopedHitContext inner(contextHead, {});
                isolated = isolated && !contextHead->Context().definitionVerified;
            });
            other.join();
            Check(isolated && applyTop() == -100000, "Damage context leaked between threads");
        }
        Check(contextHead == nullptr && applyTop() == -7, "Returned hit left stale TLS permission");
    }

    void TestDispatcherCallbacks()
    {
        auto hit = MortalHit();
        constexpr uintptr_t directHpReturn = 0x141FE7FDEULL;
        {
            ScopedHitContext outer(contextHead, hit.context);
            hit.context = ContextForDispatch(contextHead, directHpReturn, directHpReturn);
            Check(ApplyOutgoing(hit, -1, true, 1.0f) == -100000,
                  "Direct HP call did not receive the verified event");
            // An unrelated callback can target the SAME entity in the SAME tick.
            // It still must not inherit permission from the enclosing event.
            hit.context = ContextForDispatch(contextHead, directHpReturn + 1, directHpReturn);
            ExpectNative(hit, -1);
            hit.context = ContextForDispatch(contextHead, 0, 0);
            ExpectNative(hit, -1);
            const auto* saved = contextHead;
            contextHead = nullptr; // Production suspends TLS around the original dispatcher.
            hit.context = ContextForDispatch(contextHead, directHpReturn, directHpReturn);
            ExpectNative(hit, -1);
            {
                auto nestedHit = MortalHit();
                nestedHit.context.noDead = true;
                ScopedHitContext nested(contextHead, nestedHit.context);
                nestedHit.context = ContextForDispatch(contextHead, directHpReturn, directHpReturn);
                ExpectNative(nestedHit, -1);
            }
            Check(contextHead == nullptr, "Nested callback resurrected a suspended parent");
            contextHead = saved;
        }
        hit.context = ContextForDispatch(contextHead, directHpReturn, directHpReturn);
        ExpectNative(hit, -1); // Delayed callback after the event has returned.
    }

    void TestBoundariesAndOrdinaryCombat()
    {
        auto hit = MortalHit();
        constexpr int64_t max = (std::numeric_limits<int64_t>::max)();
        for (int64_t hp : {int64_t{1}, int64_t{100000}, int64_t{2000000000}, max})
        {
            hit.health.current = hp;
            const auto delta = ApplyOutgoing(hit, -1, true, 1.0f);
            Check(hp + delta == 0, "Verified mortal OHK did not reach zero without overflow");
        }
        hit = MortalHit();
        hit.statusId = hit.healthStatusId = 0;
        Check(ApplyOutgoing(hit, -1, true, 1.0f) == -hit.health.current,
              "A verified native Health=0 mapping was rejected");
        hit = MortalHit();
        hit.health.floor = -1;
        Check(ApplyOutgoing(hit, -1, true, 1.0f) == -hit.health.current,
              "A nonpositive floor disabled ordinary OHK");
        hit = MortalHit();
        Check(ApplyOutgoing(hit, -7, false, 3.0f) == -21, "Ordinary multiplier stopped working");
        Check(ApplyOutgoing(hit, -7, false, 1.0f) == -7, "Disabled features changed damage");
        Check(ApplyOutgoing(hit, -200000, true, 1.0f) == -200000, "OHK reduced a lethal native hit");
        for (int64_t delta : {int64_t{0}, int64_t{1}, max, int64_t{-250000000},
                              int64_t{-500000000}, -max, (std::numeric_limits<int64_t>::min)()})
            ExpectNative(hit, delta);
        for (int64_t delta : {int64_t{-249999999}, int64_t{-250000001}, int64_t{-499999999}})
            Check(ApplyOutgoing(hit, delta, false, 2.0f) == delta * 2,
                  "An adjacent ordinary delta was accidentally excluded");
        for (int64_t hp : {int64_t{0}, int64_t{-1}, (std::numeric_limits<int64_t>::min)()})
        {
            hit.health.current = hp;
            ExpectNative(hit, -1); // Do not drive already depleted HP into a secondary gauge.
        }
        hit = MortalHit();
        for (float multiplier : {-1.0f, std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()})
            Check(ApplyOutgoing(hit, -7, false, multiplier) == -7,
                  "Invalid multiplier changed sign or caused an out-of-range conversion");
        Check(ApplyOutgoing(hit, -7, false, (std::numeric_limits<float>::max)()) == -max,
              "Large multiplier overflowed");
    }
}

int main()
{
    try
    {
        TestCaptureProgression();
        TestIdentityAndStatus();
        TestSpecialFlags();
        TestContextAttribution();
        TestNestedContextLifetime();
        TestDispatcherCallbacks();
        TestBoundariesAndOrdinaryCombat();
        std::puts("damage_policy_tests: passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "damage_policy_tests: %s\n", error.what());
        return 1;
    }
}
