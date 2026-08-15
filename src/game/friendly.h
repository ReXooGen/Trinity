#pragma once

namespace trinity::game
{
    // Trust Multiplier - scales the trust ("Friendly" / 친밀도) you GAIN when
    // gifting NPCs or feeding/taming animals and mounts.
    //
    // How it works, and why it survives game updates (located by byte
    // signature, never a baked address; see the Friendly section of offsets.h
    // and the trinity-friendly-system notes for the full RE):
    //  - "Friendly" is the engine's single trust value behind both mechanics -
    //    gifting an NPC and feeding a wild animal both raise it, and it is the
    //    gauge that fills to tame a mount. It is a 0..100 value; reaching 100
    //    completes the tame.
    //  - Every GAMEPLAY change to it (gift, feed, AI-driven) funnels through one
    //    apply-and-replicate function that the save-loader path deliberately
    //    bypasses. We hook that funnel and, for each change that is a GAIN,
    //    scale the increase over the last value we let through for that target
    //    (a true per-gift delta multiplier), re-clamped to the game's max of
    //    100. Losses and already-max targets pass through untouched, and load-
    //    from-save is never re-scaled. The value is still server-clamped to 100
    //    in-process, so a large multiplier simply reaches max / tames in fewer
    //    gifts rather than overflowing.
    //
    // Purely reactive: the hook does all the work, so there is no per-frame
    // Tick. Inert (the hook passes everything through) unless the toggle is on
    // and the multiplier is above 1.0x.
    class Friendly
    {
    public:
        // Installs the friendly-apply hook. Requires MH_Initialize() first.
        // Non-fatal: if the signature does not resolve, Trust Multiplier is
        // disabled and the rest of the mod is unaffected.
        static bool Install();
        static void Remove();

        // True once the funnel hook is installed (the feature is available).
        static bool Ready();
    };
}
