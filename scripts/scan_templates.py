import ctypes, struct, subprocess

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

templates = {}

for i in range(min(count, 1000)):
    slot = r64(data + i * 8)
    if not slot: continue
    v10 = r64(slot + 0x10)
    v8 = r64(slot + 8)
    owner = v10 if v10 >= 0x10000000 else (v8 if v8 >= 0x10000000 else slot)
    
    # Check +0x100, +0x108, +0x110, +0x118, +0x120
    for off in (0x100, 0x108, 0x110, 0x118, 0x120, 0xF0, 0xF8):
        ptr = r64(owner + off)
        if ptr >= 0x10000000 and ptr < 0x7fffffffffff:
            s = read_str(ptr, 64)
            if s and s.isprintable() and len(s) >= 3:
                templates.setdefault(s, []).append((i, owner, off))
                break

print(f"Total distinct template strings found: {len(templates)}")
for name, list_entities in sorted(templates.items()):
    if any(k in name.lower() for k in ("horse", "pet", "dog", "mount", "rokade", "kliff", "damian", "oongka", "vehic")):
        print(f"Template '{name}': {len(list_entities)} entities (e.g. idx {list_entities[0][0]}, owner=0x{list_entities[0][1]:x})")
