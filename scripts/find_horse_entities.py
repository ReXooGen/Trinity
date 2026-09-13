import ctypes, ctypes.wintypes, struct, subprocess

kernel32 = ctypes.windll.kernel32
out = subprocess.check_output(['powershell', '-NoProfile', '-Command', '(Get-Process -Name CrimsonDesert).Id'])
pid = int(out.strip())
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

def rpm(addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t(0)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)): return buf.raw
    return None

def r64(addr):
    b = rpm(addr, 8)
    return struct.unpack('<Q', b)[0] if b else 0

def r32(addr):
    b = rpm(addr, 4)
    return struct.unpack('<I', b)[0] if b else 0

def r16(addr):
    b = rpm(addr, 2)
    return struct.unpack('<H', b)[0] if b else 0

def read_str(addr, maxlen=64):
    b = rpm(addr, maxlen)
    if not b: return ''
    idx = b.find(b'\x00')
    if idx != -1: b = b[:idx]
    return b.decode('utf-8', errors='ignore')

charMgrSlot = 0x146C29C88
mgr_ptr = r64(charMgrSlot)
mgr = r64(mgr_ptr) if mgr_ptr else 0
data = r64(mgr + 0xB0)
count = r32(mgr + 0x9C)

print(f"Scanning CharMgr ({count} entities)...")

for i in range(min(count, 1000)):
    slot = r64(data + i * 8)
    if not slot: continue
    v10 = r64(slot + 0x10)
    v8 = r64(slot + 8)
    owner = v10 if v10 >= 0x10000000 else (v8 if v8 >= 0x10000000 else slot)
    actor = r64(owner + 0x68)
    if not actor: actor = owner

    # Let's inspect strings reachable from owner or actor (within depth 2)
    names = []
    for base in (owner, actor):
        for off in range(0, 0x180, 8):
            val = r64(base + off)
            if val >= 0x10000000 and val < 0x7fffffffffff:
                s = read_str(val, 32)
                if s and s.isprintable() and len(s) >= 4:
                    if any(w in s for w in ("Horse", "horse", "Rokade", "Exclaire", "Vehicle", "Mount", "Dog", "Pet", "Animal")):
                        names.append((off, s))
                # Check 1 level deeper
                val2 = r64(val)
                if val2 >= 0x10000000 and val2 < 0x7fffffffffff:
                    s2 = read_str(val2, 32)
                    if s2 and s2.isprintable() and len(s2) >= 4:
                        if any(w in s2 for w in ("Horse", "horse", "Rokade", "Exclaire", "Vehicle", "Mount", "Dog", "Pet", "Animal")):
                            names.append((off, s2))

    if names:
        objType = r32(owner + 0x48) & 0xFF
        partyIdx = r32(owner + 0x50)
        print(f"Entity {i}: owner=0x{owner:x} actor=0x{actor:x} objType={objType} partyIdx={partyIdx}")
        for off, s in names:
            print(f"    +0x{off:x}: '{s}'")
