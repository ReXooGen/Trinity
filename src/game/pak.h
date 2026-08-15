#pragma once

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal read-only reader for Crimson Desert's on-disk asset archives.
//
// Each content chunk is a folder `<gameRoot>\NNNN\` holding a `0.pamt` manifest
// plus one or more `N.paz` blobs. The manifest is plaintext: a directory tree
// and a file table, both keyed into two "name blobs" of parent-linked string
// fragments. File payloads are stored (comp 0), whole-file LZ4 (comp 2), or -
// for textures - a plaintext 128-byte DDS header followed by an LZ4-compressed
// mip payload (comp 1). We only ever pull a handful of UI atlases at startup,
// so this favours clarity over speed. (Format reverse-engineered from the
// engine's own pamt parser; see the Trinity icon-pipeline notes.)
// ---------------------------------------------------------------------------

namespace trinity::game::pak
{
    // Reads one file out of chunk `chunk` (e.g. 12 -> "0012") by its directory
    // path (forward-slash, e.g. "ui/texture") and file name. Returns the fully
    // decompressed bytes; false if the game root, chunk, or file isn't found.
    //
    // `optional` says a missing file is EXPECTED, so don't log it as an error -
    // the caller will handle the miss. Item icons need this: the item table
    // names a sprite for thousands of items and plenty of those .dds files
    // simply are not shipped (placeholders like "CreateIcon", cut content), so
    // every one of them would otherwise report a red error for something that
    // is merely an item without an icon. A real failure (bad chunk, unreadable
    // paz, decompression error) still logs regardless.
    bool ReadFile(int chunk, const char* dirPath, const char* fileName,
                  std::vector<uint8_t>& out, bool optional = false);

    // Resolves and caches the game root (the folder that contains the NNNN
    // chunk directories). Safe to call repeatedly. Returns false if not found.
    bool HaveGameRoot();
}
