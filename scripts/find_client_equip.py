import ctypes, struct, subprocess

kernel32 = ctypes.windll.kernel32
out = subprocess.check_output(['powershell', '-NoProfile', '-Command', '(Get-Process -Name CrimsonDesert).Id'])
pid = int(out.strip())
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rpm(addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t(0)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        return buf.raw
    return None

def r64(addr):
    b = rpm(addr, 8)
    return struct.unpack('<Q', b)[0] if b else 0

def r32(addr):
    b = rpm(addr, 4)
    return struct.unpack('<I', b)[0] if b else 0

def read_str(addr, maxlen=64):
    b = rpm(addr, maxlen)
    if not b: return ''
    idx = b.find(b'\x00')
    if idx != -1: b = b[:idx]
    return b.decode('utf-8', errors='ignore')

def get_rtti_name(obj_addr):
    vt = r64(obj_addr)
    if not vt: return ""
    rtti_complete = r64(vt - 8)
    if not rtti_complete: return ""
    type_desc = r32(rtti_complete + 0xC)
    if not type_desc: return ""
    return read_str(0x140000000 + type_desc + 0x10, 64)

# Let's inspect the player actors in g_actors:
# Kliff = 0x3eb46035140
# Damiane = 0x3eb4ec2f500
# Oongka = 0x3eb4ec2d5c0
actors = [0x3eb46035140, 0x3eb4ec2f500, 0x3eb4ec2d5c0]
names = ["Kliff", "Damiane", "Oongka"]

for name, act in zip(names, actors):
    print(f"=== {name} Actor: 0x{act:x} ({get_rtti_name(act)}) ===")
    # Scan all pointers in this actor for Equip components
    for off in range(0, 0x400, 8):
        c = r64(act + off)
        if c >= 0x10000000 and c < 0x7fffffffffff:
            tname = get_rtti_name(c)
            if "Equip" in tname or "Scene" in tname or "Character" in tname or "Mesh" in tname or "Slot" in tname:
                print(f"  +0x{off:x}: 0x{c:x} -> {tname}")
                desc = r64(c + 0x80)
                if desc >= 0x10000000:
                    cnt = r32(desc + 0x10)
                    print(f"    desc=0x{desc:x} count={cnt}")
