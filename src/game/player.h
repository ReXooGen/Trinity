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
    //    all controllable/summonable and able to coexist), so Tick() periodically
    //    resolves the PLAYER SET from the gameplay-character manager
    //    (kCharMgrAnchors). The controlled body requires a possessor round-trip
    //    and valid health chain; character-indexed handles additionally require
    //    native party/equipment identity. Stat and combat slots are independent
    //    of those indices. Body transitions are picked up on the next resolve,
    //    and roots absent from the manager are dropped even if still readable.
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

        // Periodically resolve current player/mount roots from the character manager,
        // dropping unavailable handles on each resolve. Must run on the game thread.
        // Internal combat slot 0 remains the controlled body, independent of identity.
        static void Tick();

        // Refresh/pin revalidated player and mount stat entries; game-thread pump.
        static void RefreshSelf();

        // True while the controlled body's health entry and actor are available.
        static bool Ready();

        // Fresh character-indexed handles: 0 = Kliff, 1 = Damiane, 2 = Oongka.
        // Unavailable/unidentified characters return 0; index 0 is NOT the active-player slot.
        static uintptr_t GetActor(int index);
        static uintptr_t GetOwner(int index = 0);
        // Controlled body, independent of whether its character identity is known.
        static uintptr_t GetControlledOwner();
        // Counts identified character handles, not internal combat/stat slots.
        static int GetTrackedPlayerCount();
        // Controlled character identity (0..2), or -1 while unknown/unavailable.
        static int GetActiveCharacterIdx();
        static uintptr_t GetCharMgrGlobal();
        // Fresh core-profile lookup; valid, matching equipment takes priority over render state.
        static uintptr_t GetProfileOwner(int index);
        static uintptr_t GetProfileActor(int index);
        static uintptr_t GetProfileEquipComp(int index);

        // Active mount / vehicle actor tracking (Horse, Dragon, Wagon, Mount).
        struct MountDescriptor
        {
            uintptr_t actor = 0;
            uintptr_t owner = 0;
            uintptr_t equipComp = 0;
            float     distance = 0.0f;
            bool      isRidden = false;
            bool      hasHorseGear = false;
            char      name[64] = {};
            char      gearSummary[64] = {};
            char      label[128] = {};
        };

        static uintptr_t GetMountActor(int index = 0);
        static uintptr_t GetMountOwner(int index = 0);
        static int GetTrackedMountCount();
        static bool GetMountDescriptor(int index, MountDescriptor* out);

        // DEBUG: dump every player-ish character in the manager vector to the
        // console - class tag, vtable, possessor round-trip, vital-chain status
        // and HP - so we can see how the three protagonists (and summoned
        // companions) are actually represented at runtime. Read-only and
        // SEH-guarded; safe to call from the menu thread.
        static void DumpCharacters();
    private:
        static void TickImpl();
    };
}
