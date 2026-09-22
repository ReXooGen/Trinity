# Raw dump around the data references to member-name strings.
# A hkClassMember holds its name pointer first, so the reference address is the
# start of the member record - the byte offset lives a few fields in.
import struct

REFS = {
    "collisionFilterInfo": [0x1452de4f8, 0x1452e4408, 0x1452e4788, 0x1452e9f40,
                            0x1452ed960, 0x1452eefb0, 0x1452f3408],
    "hknpCharacterProxyCinfo": [0x14533b160, 0x14537cac0, 0x14539d090],
    "hknpCharacterProxy": [0x14536e980, 0x14537cb18, 0x1453909a0],
}


def dump(addr, n=0x30):
    r = mcp.call("bn_memory_read", address=hex(addr), length=n)
    raw = bytes.fromhex(r["hex"])
    out(f"  @ {hex(addr)}")
    out("    hex: " + raw.hex(" "))
    # qwords and plausible u16 offsets
    qs = [struct.unpack_from("<Q", raw, i)[0] for i in range(0, len(raw) - 7, 8)]
    out("    qwords: " + ", ".join(hex(q) for q in qs))
    u16s = [struct.unpack_from("<H", raw, i)[0] for i in range(0, len(raw) - 1, 2)]
    out("    u16:    " + ", ".join(f"{v:#06x}" for v in u16s))
    u32s = [struct.unpack_from("<I", raw, i)[0] for i in range(0, len(raw) - 3, 4)]
    out("    u32:    " + ", ".join(f"{v:#010x}" for v in u32s))


for label, addrs in REFS.items():
    out(f"\n########## {label} ##########")
    for a in addrs:
        dump(a)

# Also: what does the NAME string's neighbourhood look like (is there a class
# table laid out as [name][parent][size]...)?
out("\n########## hknpCharacterProxy Cinfo name string area ##########")
dump(0x1452f8ab8 - 0x20, 0x60)
