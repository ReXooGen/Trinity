#pragma once

namespace trinity
{
    // Feature persistence: a plain-text Trinity.ini next to Trinity.asi.
    // The Auto Save flag itself is always read and written; the feature
    // values (God Mode, damage multipliers, Game Speed...) are only applied
    // on load when Auto Save was on last session.
    class Settings
    {
    public:
        static void Load();          // read Trinity.ini; restore features if Auto Save was on
        static void Save();          // write the Auto Save flag + current feature values (owner only)
        static void ResetFeatures(); // put every feature in State back to its default
        static void ResetBinds();    // put every key/pad bind in State back to its default

        // Claim Trinity.ini for this process; Save() is a no-op until this is
        // called. The launcher loads the ASI too, and its State never leaves
        // the values Load() read at startup because it has no menu. Without an
        // owner it would write that stale snapshot back over the game's file on
        // exit, silently reverting the session's changes. Called from the
        // render path, which only runs where the game actually presents.
        static void ClaimOwnership();
    };
}
