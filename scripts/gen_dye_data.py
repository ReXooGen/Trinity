#!/usr/bin/env python3
"""Regenerate src/game/dye_data.h from the game's own data tables.

Reads two gamedata tables straight out of the game's pak archives (the same
.pamt/.paz format src/game/pak.cpp parses at runtime):

  dyecolorgroupinfo.pabgb      - the dyehouse's 10 color families, each with
                                 109 preset shades (9 neutrals + a 10x10 grid).
  partprefabdyeslotinfo.pabgb  - the registry of part prefabs that HAVE dye
                                 channels; anything absent cannot be dyed
                                 (the game's own dyehouse gates on this).

Run after a game patch if the dyehouse palette looks off or dyeable gear is
being filtered out:

    python scripts/gen_dye_data.py [game_root]

game_root defaults to the usual Steam install path below; it is the folder
holding the numbered chunk directories (0000..0035). Pass your own path if the
game lives on another drive or library.
"""
import os
import re
import struct
import sys

DEFAULT_ROOT = r"C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "src", "game", "dye_data.h")

# Menu order + display names for the family stringKeys, hue-ordered. A family
# the game adds later still gets emitted (at the end, named by stringKey).
FAMILY_NAMES = [
    ("Her_Color_Group_I",   "Red"),
    ("Tom_Color_Group_I",   "Orange"),
    ("Por_Color_Group_I",   "Yellow"),
    ("Bar_Color_Group_I",   "Lime"),
    ("Cal_Color_Group_I",   "Green"),
    ("Kwe_Color_Group_I",   "Teal"),
    ("Del_Color_Group_I",   "Cyan"),
    ("Dem_Color_Group_III", "Blue"),
    ("Dem_Color_Group_II",  "Magenta"),
    ("Dem_Color_Group_I",   "Rose"),
]


# --- pak reader (offline twin of src/game/pak.cpp) --------------------------
def lz4_block(src, usize):
    dst = bytearray(usize)
    s, d, n = 0, 0, len(src)
    while s < n:
        token = src[s]; s += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[s]; s += 1; lit += b
                if b != 255:
                    break
        if lit:
            dst[d:d + lit] = src[s:s + lit]; s += lit; d += lit
        if s >= n:
            break
        off = src[s] | (src[s + 1] << 8); s += 2
        mlen = token & 15
        if mlen == 15:
            while True:
                b = src[s]; s += 1; mlen += b
                if b != 255:
                    break
        mlen += 4
        start = d - off
        for k in range(mlen):  # byte-by-byte: overlap is legal
            dst[d + k] = dst[start + k]
        d += mlen
    if d != usize:
        raise ValueError(f"lz4: decoded {d}, expected {usize}")
    return bytes(dst)


def name_from_blob(blob, off):
    parts = []
    while off != 0xFFFFFFFF and len(parts) < 64:
        if off + 5 > len(blob):
            break
        parent = struct.unpack_from("<I", blob, off)[0]
        ln = blob[off + 4]
        parts.append(blob[off + 5:off + 5 + ln].decode("utf-8", "replace"))
        off = parent
    return "".join(reversed(parts))


def find_files(root, wanted):
    """Yield (chunk_dir, file_entry_bytes) for every wanted file name."""
    wanted = {w.lower() for w in wanted}
    found = {}
    for entry in sorted(os.listdir(root)):
        chunk_dir = os.path.join(root, entry)
        pamt = os.path.join(chunk_dir, "0.pamt")
        if not os.path.isfile(pamt):
            continue
        d = open(pamt, "rb").read()
        paz_count = struct.unpack_from("<I", d, 4)[0]
        o = 12 + paz_count * 12
        dir_blob_sz = struct.unpack_from("<I", d, o)[0]; o += 4 + dir_blob_sz
        file_blob_sz = struct.unpack_from("<I", d, o)[0]; o += 4
        file_blob = d[o:o + file_blob_sz]; o += file_blob_sz
        n_dirs = struct.unpack_from("<I", d, o)[0]; o += 4 + n_dirs * 16
        n_files = struct.unpack_from("<I", d, o)[0]; o += 4
        for i in range(n_files):
            fe = struct.unpack_from("<IIIIHBB", d, o + i * 20)
            fname = name_from_blob(file_blob, fe[0]).lower()
            if fname in wanted and fname not in found:
                found[fname] = (chunk_dir, fe)
    return found


def extract(chunk_dir, fe):
    _, offset, csize, usize, paz_idx, comp, _ = fe
    with open(os.path.join(chunk_dir, f"{paz_idx}.paz"), "rb") as f:
        f.seek(offset)
        raw = f.read(csize)
    if comp == 0:
        return raw
    if comp == 2:
        return lz4_block(raw, usize)
    raise ValueError(f"unexpected compression {comp}")


# --- table parsing -----------------------------------------------------------
def parse_color_groups(d):
    """[(key, stringKey, [(r,g,b) x count])]. Record: u32 key, u32 nameLen,
    name, NUL, u32 shadeCount, shadeCount x {u8 b,g,r,a; u32 condition},
    then a trailer (u32, u8, u32 key again, u32 idLen, digits, 4 bytes)."""
    o, groups = 0, []
    while o + 8 < len(d):
        key = struct.unpack_from("<I", d, o)[0]; o += 4
        ln = struct.unpack_from("<I", d, o)[0]; o += 4
        name = d[o:o + ln].decode(); o += ln + 1
        cnt = struct.unpack_from("<I", d, o)[0]; o += 4
        shades = []
        for _ in range(cnt):
            b, g, r, a, cond = struct.unpack_from("<BBBBI", d, o); o += 8
            shades.append((r, g, b))
        groups.append((key, name, shades))
        o += 4 + 1 + 4
        idln = struct.unpack_from("<I", d, o)[0]
        o += 4 + idln + 4
    if o != len(d):
        raise ValueError(f"dyecolorgroupinfo: consumed {o} of {len(d)}")
    return groups


def dyeable_prefabs(d):
    """Every cd_* token in the registry (row keys, variants AND model-path
    basenames) - membership is deliberately generous, since a miss here hides
    a dyeable item from the menu while a stray extra name costs nothing."""
    names = set()
    for m in re.finditer(rb"cd_[a-z0-9_]+", d):
        names.add(m.group().decode())
    return names


def fnv1a(s):
    h = 2166136261
    for c in s.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_ROOT
    files = find_files(root, ["dyecolorgroupinfo.pabgb",
                              "partprefabdyeslotinfo.pabgb"])
    for need in ("dyecolorgroupinfo.pabgb", "partprefabdyeslotinfo.pabgb"):
        if need not in files:
            sys.exit(f"ERROR: {need} not found under {root}")

    groups = parse_color_groups(extract(*files["dyecolorgroupinfo.pabgb"]))
    by_sk = {sk: (key, shades) for key, sk, shades in groups}

    ordered = []
    for sk, label in FAMILY_NAMES:
        if sk in by_sk:
            key, shades = by_sk.pop(sk)
            ordered.append((key, label, sk, shades))
    for sk, (key, shades) in sorted(by_sk.items()):  # future additions
        ordered.append((key, sk, sk, shades))

    prefabs = dyeable_prefabs(extract(*files["partprefabdyeslotinfo.pabgb"]))
    hashes = sorted({fnv1a(p) for p in prefabs})

    shade_counts = {len(s) for _, _, _, s in ordered}
    if shade_counts != {109}:
        print(f"WARNING: shade counts {shade_counts} != {{109}} - "
              "the menu grid assumes 9 neutrals + 10x10.")

    with open(OUT, "w", newline="\n") as f:
        w = f.write
        w("// Generated by scripts/gen_dye_data.py - DO NOT EDIT BY HAND.\n")
        w("// Source: the game's own dyecolorgroupinfo.pabgb and\n")
        w("// partprefabdyeslotinfo.pabgb (see the script header for how to\n")
        w("// regenerate after a game patch).\n")
        w("#pragma once\n\n#include <cstdint>\n\n")
        w("namespace trinity::game\n{\n")
        w("    // --- The dyehouse's preset palette -----------------------------------\n")
        w("    // One family per dyecolorgroupinfo row, hue-ordered for the menu.\n")
        w("    // shades[0..8] are the family's 9 neutral tones; shades[9..108] are\n")
        w("    // the 10x10 grid the game's dye UI shows (row = darker, column =\n")
        w("    // more saturated). The dye record stores the family key + raw RGB.\n")
        w("    struct DyeShadeRGB { uint8_t r, g, b; };\n")
        w("    struct DyeFamily\n    {\n")
        w("        uint32_t    key;        // dyecolorgroupinfo._key (dye record +0)\n")
        w("        const char* name;       // menu label\n")
        w("        const char* stringKey;  // engine name, survives key drift\n")
        w("        DyeShadeRGB shades[109];\n    };\n\n")
        w(f"    inline constexpr int kDyeFamilyCount = {len(ordered)};\n")
        w("    inline constexpr int kDyeShadeCount  = 109;\n")
        w("    inline constexpr int kDyeNeutrals    = 9;  // shades[0..8]\n")
        w("    inline constexpr int kDyeGridCols    = 10; // shades[9..108]\n")
        w("    inline constexpr int kDyeGridRows    = 10;\n\n")
        w(f"    inline constexpr DyeFamily kDyeFamilies[{len(ordered)}] =\n    {{\n")
        for key, label, sk, shades in ordered:
            w(f"        {{ 0x{key:08X}u, \"{label}\", \"{sk}\",\n          {{")
            for i, (r, g, b) in enumerate(shades):
                if i % 8 == 0:
                    w("\n            ")
                w(f"{{{r},{g},{b}}},")
            w("\n          } },\n")
        w("    };\n\n")
        w("    // --- Dyeable part prefabs --------------------------------------------\n")
        w("    // Sorted FNV-1a (32-bit) hashes of every lowercase cd_* prefab name in\n")
        w("    // partprefabdyeslotinfo.pabgb - the registry the game's own dyehouse\n")
        w("    // gates on. An equipped item whose icon prefab is not in here has no\n")
        w("    // dye channels: applying would write records but change nothing.\n")
        w(f"    inline constexpr int kDyeablePrefabCount = {len(hashes)};\n")
        w(f"    inline constexpr uint32_t kDyeablePrefabHashes[{len(hashes)}] =\n    {{\n")
        for i in range(0, len(hashes), 8):
            w("        " + " ".join(f"0x{h:08X}u," for h in hashes[i:i + 8]) + "\n")
        w("    };\n}\n")

    print(f"Wrote {os.path.normpath(OUT)}: {len(ordered)} families, "
          f"{len(hashes)} dyeable prefab hashes.")


if __name__ == "__main__":
    main()
