#pragma once

#include <cstddef>

namespace trinity::game
{
    // Everything related to teleporting: live position tracking, jumping to
    // an arbitrary point, and saved presets. See offsets.h (kSig_MoveUpdate)
    // for the RE background - live-verified to track the local player's own
    // coordinates; writes reuse the same tracked move-owner pointer.
    class Teleport
    {
    public:
        // Installs the movement-update hook. Requires MH_Initialize() first.
        static bool Install();
        static void Remove();

        // Last-seen live position (world x,y,z). Returns false if no write
        // has been observed yet (e.g. still at the main menu).
        static bool GetLastPosition(float* x, float* y, float* z);

        // True on frames where Free Flight is actively driving the player's
        // vertical velocity (a direction is held while airborne). Exposed so the
        // HUD can light "FLY".
        static bool GetFlightEngaged();

        // Copies the last-seen position to the system clipboard as plain
        // text ("X Y Z"), ready to paste somewhere as a future preset.
        // Returns false if there's no position yet or the clipboard write
        // failed.
        static bool CopyPositionToClipboard();

        // --- Fast travel / map-gimmick catalog -----------------------------
        // The game's own fast-travel network is the LevelGimmickSceneObjectInfo
        // registry. Scenes flagged _useTeleport are the REAL waypoint networks
        // (the same filter the world map uses); they are listed first, region-
        // grouped and named by the game's own area boxes ("Hernand 0021").
        // Every other scene (ores, chests, shops, bells...) follows as a named
        // POI category - travel to those is best-effort. TravelToNode fires the
        // game's real, streaming-correct fast travel (sub_505140).
        //
        // LoadCatalog() only REQUESTS a build: the catalog is assembled on the
        // game thread (the area-name table lazy-loads its rows there), so it
        // returns false for a frame or two, then CatalogReady() flips once
        // in-world. Call it every frame while the menu is open (cheap).
        // Waypoint node lists are prebuilt; POI lists build on first access
        // (EnsureCategoryNodes, menu thread, raw reads only).
        static bool   LoadCatalog();
        static bool   CatalogReady();
        static size_t CategoryCount();
        static bool   GetCategory(size_t cat, const char** name, size_t* nodeCount);

        static bool   EnsureCategoryNodes(size_t cat);
        static size_t NodeCount(size_t cat);
        static bool   GetNode(size_t cat, size_t node, const char** label,
                              float* x, float* y, float* z);

        // Queues the game's fast travel to a catalog node. The call is dispatched
        // on the game thread (from the movement hook), matching how the game does
        // it. Returns false if the indices are out of range or no travel function
        // was resolved.
        static bool   TravelToNode(size_t cat, size_t node);

        // --- Map Marker Teleport --------------------------------------------
        // Status code returned by TeleportToMarker.
        enum class MarkerStatus
        {
            Success,
            NotReady,
            NoPlayer,
            NoMarker,
            UnsafeContext,
            InvalidCoordinates,
            WriteFailed,
        };

        // Returns true if marker hooks and world origin are resolved and ready.
        static bool MarkerReady();

        // Returns true if a valid destination marker has been placed on the map.
        static bool HasMarker();

        // Reads the latest detected map marker coordinates (raw marker X, Y, Z).
        // Returns false if no valid marker exists.
        static bool GetMarkerPosition(float* x, float* y, float* z);

        // Teleports the active player to the detected map marker.
        // Uses fallbackHeight (default 1200.0f) if the marker altitude is 0.
        // Activates temporary fall protection when safe insertion is needed.
        static MarkerStatus TeleportToMarker(float fallbackHeight = 1200.0f);
    };
}
