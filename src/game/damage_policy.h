#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace trinity::game::damage_policy
{
    // Positional dispatcher bytes, not guessed action/parry/tackle enums.
    struct NativeFlags
    {
        uint8_t a6 = 0, a7 = 0, a8 = 0, a9 = 0, a10 = 0;
    };

    struct HealthState
    {
        bool verified = false; // Entry identity, HP, floor AND native state reads succeeded.
        int64_t current = 0;
        int64_t floor = 0;
        bool nativeProtected = false;
    };

    struct HitContext
    {
        uintptr_t source = 0;
        uintptr_t victimRoot = 0;
        int64_t time = 0;
        bool definitionVerified = false;
        bool noDead = true;
    };

    // The head belongs to the calling thread. Only the top frame is eligible:
    // an unclassified nested hit must shadow, never inherit, its lethal parent.
    class ScopedHitContext
    {
    public:
        ScopedHitContext(const ScopedHitContext*& head, HitContext context) noexcept
            : head_(head), previous_(head), context_(context) { head_ = this; }
        ~ScopedHitContext() noexcept { head_ = previous_; }
        ScopedHitContext(const ScopedHitContext&) = delete;
        ScopedHitContext& operator=(const ScopedHitContext&) = delete;

        const HitContext& Context() const noexcept { return context_; }

    private:
        const ScopedHitContext*& head_;
        const ScopedHitContext* previous_;
        HitContext context_;
    };

    inline HitContext ContextForDispatch(const ScopedHitContext* head, uintptr_t returnAddress,
                                         uintptr_t verifiedHpReturn) noexcept
    {
        return head && verifiedHpReturn && returnAddress == verifiedHpReturn
            ? head->Context() : HitContext{};
    }

    struct OutgoingHit
    {
        uint16_t statusId = 0;
        uint16_t healthStatusId = 0; // Dispatcher ID, not the legacy StatType enum.
        bool healthStatusKnown = false;
        uintptr_t source = 0;
        uintptr_t verifiedPlayerOwner = 0;
        uintptr_t victimRoot = 0;
        int64_t time = 0;
        HitContext context{};
        bool enemyTarget = false;
        NativeFlags flags{};
        HealthState health{};
    };

    // Ordinary combat causes and protagonist damage types (Kliff sword/unarmed,
    // Damiane rapier/musket/martial, Oongka axe/hammer/martial).
    // Native DamageBuffData context verification (definitionVerified + !noDead)
    // decides lethality; this gate preserves special flags and excludes fall damage.
    inline bool IsOrdinaryCombatCause(uint8_t a8, uint8_t a9)
    {
        // Strictly exclude fall damage / high landing impact (a8=0x13 / 19) and invalid broadcast bytes
        if (a8 == 0x13 || a8 == 19 || a8 == 0xFF)
            return false;

        // Kliff sword basic attack I/II (8,10) and unarmed (0,0)
        if ((a8 == 8 && a9 == 10) || (a8 == 0 && a9 == 0))
            return true;

        // Protagonist combat actions and skill damage types from skill.pabgb
        if (a8 == 8 || a8 == 0 || a8 == 1 || a8 == 3 || a8 == 10 || a8 == 11 || a8 == 12 || a8 == 14 || a8 == 16)
        {
            switch (a9)
            {
            case 0:   // Unarmed / default combat action
            case 10:  // Sword / basic strike / Damiane run attack
            case 32:  // Axe roll attack / Damiane shield dash
            case 58:  // Skill strike
            case 101: // Companion passive / strike
            case 121: // Weapon mastery
            case 128: // Combat action
            case 132: // Rapier down thrust / JumpGrabSpin / Battle axe
            case 136: // Combat action / Taunt
            case 138: // Combat action
            case 140: // Fast avoid / Evade strike
            case 141: // Demian kick
            case 144: case 145:
            case 152: // Lift attack
            case 153: // Dash attack
            case 157: // Demian rapier hard attack / finish / stride thrust
            case 160:
            case 164: // Battle axe down attack
            case 165: case 166: // Caliburn rapier
            case 176:
            case 180: // Shield glider / Rocket pack strike
            case 185:
            case 188: // Demian rapier normal attack / charge attack / run kick / low attack / jump attack / parry
            case 189: // Demian wall kick
            case 236: case 237:
                return true;
            default:
                break;
            }
        }
        return false;
    }

    inline bool CanAmplify(const OutgoingHit& hit, int64_t delta)
    {
        // Keep existing script/grass exclusions, but never use magnitude as proof of combat.
        if (delta >= 0 || delta <= -500000000LL || delta == -250000000LL)
            return false;
        if (!hit.healthStatusKnown || hit.healthStatusId == 0xFFFF || hit.statusId != hit.healthStatusId ||
            !hit.source || !hit.verifiedPlayerOwner || hit.source != hit.verifiedPlayerOwner ||
            !hit.enemyTarget || !hit.health.verified || hit.health.current <= 0 ||
            hit.health.floor > 0 || hit.health.nativeProtected)
            return false;

        if (!hit.context.definitionVerified || hit.context.noDead || !hit.victimRoot ||
            hit.context.source != hit.source || hit.context.victimRoot != hit.victimRoot ||
            hit.context.time != hit.time)
            return false;

        // The verified native caller supplies a6=0, a7=boolean calculation result,
        // a10=0. Context, not this tuple alone, decides lethality.
        // Preserve other special causes and control flags.
        const bool ordinaryCause = IsOrdinaryCombatCause(hit.flags.a8, hit.flags.a9);
        return hit.flags.a6 == 0 && hit.flags.a7 <= 1 && ordinaryCause && hit.flags.a10 == 0;
    }

    inline int64_t ApplyOutgoing(const OutgoingHit& hit, int64_t delta,
                                 bool oneHitKill, float multiplier)
    {
        if (!CanAmplify(hit, delta)) return delta;
        if (oneHitKill)
        {
            // No giant HP+constant arithmetic, overflow, floor write, or forced downing.
            const int64_t lethal = -hit.health.current;
            return delta < lethal ? delta : lethal;
        }
        if (multiplier == 1.0f || !std::isfinite(multiplier) || multiplier < 0.0f)
            return delta;
        const double scaled = static_cast<double>(delta) * multiplier;
        // Saturate at -INT64_MAX, leaving native HP+delta representable for positive HP.
        constexpr int64_t max = (std::numeric_limits<int64_t>::max)();
        if (scaled <= -static_cast<double>(max)) return -max;
        return static_cast<int64_t>(scaled);
    }
}
