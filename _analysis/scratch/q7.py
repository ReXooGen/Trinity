# Walk the Havok hkClass tables for the character proxy to recover field offsets.
#
# hkClass (Havok 2023, x64):
#   +0x00 name*, +0x08 parent*, +0x10 objectSize i32, +0x14 numImplementedInterfaces i32,
#   +0x18 declaredEnums*, +0x20 numDeclaredEnums i32, +0x28 declaredMembers*,
#   +0x30 numDeclaredMembers i32
# hkClassMember:
#   +0x00 name*, +0x08 class*, +0x10 enum*, +0x18 type u8, +0x19 subtype u8,
#   +0x1A flags u16, +0x1C offset u16, ...
import struct

VIEW = 0x140000000


def u64(addr):
    r = mcp.call("bn_memory_read", address=hex(addr), length=8)
    return struct.unpack("<Q", bytes.fromhex(r["hex"]))[0]


def u32(addr):
    r = mcp.call("bn_memory_read", address=hex(addr), length=4)
    return struct.unpack("<I", bytes.fromhex(r["hex"]))[0]


def u16(addr):
    r = mcp.call("bn_memory_read", address=hex(addr), length=2)
    return struct.unpack("<H", bytes.fromhex(r["hex"]))[0]


def cstr(addr, n=64):
    if not addr:
        return ""
    r = mcp.call("bn_memory_read", address=hex(addr), length=n)
    raw = bytes.fromhex(r["hex"])
    return raw.split(b"\x00")[0].decode("ascii", "replace")


def dump_class(addr, label):
    name = cstr(u64(addr))
    if not name or not name.isprintable():
        return False
    size = u32(addr + 0x10)
    num_members = u32(addr + 0x30)
    members = u64(addr + 0x28)
    out(f"\n=== hkClass @ {hex(addr)}  name={name!r} size=0x{size:X} members={num_members}")
    if not members or num_members == 0 or num_members > 200:
        return True
    for i in range(num_members):
        m = members + i * 0x28
        mn = cstr(u64(m))
        off = u16(m + 0x1C)
        typ = mcp.call("bn_memory_read", address=hex(m + 0x18), length=2)["hex"]
        out(f"   +0x{off:03X}  {mn:<34} type={typ}")
    return True


for a in [0x14536e980, 0x14537cb18, 0x14537cb28, 0x1453909a0, 0x1453909b0, 0x1468a80d8]:
    try:
        dump_class(a, "hknpCharacterProxy")
    except Exception as e:
        out(f"{hex(a)}: ERR {e}")
