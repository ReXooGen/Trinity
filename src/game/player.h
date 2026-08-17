#pragma once

#include <cstdint>

namespace trinity::game
{
    // Player stat features (God Mode, Infinite Stamina, Infinite Spirit) and
    // the incoming/outgoing damage multipliers.
    //
    // How it works, and why it survives game updates (everything is located by
    // byte signature, never a baked address):
    //  - Crimson Desert is a three-protagonist game (Kliff plus two companions,
    //    all controllable/summonable and able to coexist), so RefreshSelf()
    //    resolves the whole PLAYER SET fresh every game tick from the engine's
    //    gameplay-character manager (kCharMgrAnchors): the manager owns a
    //    vector of every character and we collect each one whose class tag is
    //    player (SelfPlayer / OtherPlayer). From each we walk to its stat entries
    //    and battle-damage identity and cache just those. Because the resolve is
    //    fresh, a body transition (mount / transform / character swap) that
    //    reallocates a character is picked up on the next tick - there is no
    //    stale-pointer cache to churn. The movement-update tick drives the
    //    refresh on the game thread.
    //  - We hook the engine's single stat-commit funnel (every HP/Stamina/
    //    Spirit write - damage, drain, heal, regen - passes through it) and,
    //    for whichever tracked-player entry a toggle applies to, force current
    //    back to full right after each write. Because that happens inside the
    //    commit call - before any death check up the stack can read the
    //    lowered value - HP never registers at a lethal value, so fall
    //    damage and one-shots can't kill; and because it fires on every
    //    write instead of polling, Stamina/Spirit never need a per-frame
    //    pin either. Every read/write is guarded, so a stale pointer after a
    //    reload is dropped rather than crashing.
    class Player
    {
    public:
        // Installs the stat-accessor and stat-commit hooks. Requires
        // MH_Initialize() first.
        static bool Install();
        static void Remove();

        // Re-resolve the player set fresh from the character-manager global and
        // refresh every tracked protagonist's stat entries. Churn-proof (no
        // cache): a body transition or character swap that reallocates a
        // character is picked up here. Must run on the game thread; the
        // Game-thread tick to resolve player and mount entities fresh from the character manager
        static void Tick();

        // Refresh/pin current player and mount stat entries. Safe to call from render thread.
        static void RefreshSelf();

        // True once at least one protagonist's health entry has been observed.
        static bool Ready();

        // Returns the tracked actor address (0 = Primary/Kliff, 1 = Companion 1, 2 = Companion 2).
        static uintptr_t GetActor(int index);
        static int GetTrackedPlayerCount();

        // DEBUG: dump every player-ish character in the manager vector to the
        // console - class tag, vtable, possessor round-trip, vital-chain status
        // and HP - so we can see how the three protagonists (and summoned
        // companions) are actually represented at runtime. Read-only and
        // SEH-guarded; safe to call from the menu thread.
        static void DumpCharacters();
    };
}
