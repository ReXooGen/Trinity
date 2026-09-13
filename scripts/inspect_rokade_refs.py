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

def read_str(addr, maxlen=64):
    b = rpm(addr, maxlen)
    if not b: return ''
    idx = b.find(b'\x00')
    if idx != -1: b = b[:idx]
    return b.decode('utf-8', errors='ignore')

# Inspect around the Rokade string hits
hits = [0x27f514aac, 0x3eb3c09fca2, 0x3eb3c1d16dc, 0x3eb3c3a45a0]
for h in hits:
    print(f"=== Hit 0x{h:x} ===")
    ctx = rpm(h - 32, 96)
    print("  context:", ctx)

# Let's search who points to 0x27f514aac or 0x3eb3c09fca2 or 0x3eb3c3a45a0 in CharMgr entities!
charMgrSlot = 0x146C29C88
mgr_ptr = r64(charMgrSlot)
mgr = r64(mgr_ptr) if mgr_ptr else 0
data = r64(mgr + 0xB0)
count = r32(mgr + 0x9C)

print(f"Scanning CharMgr ({count} entities) for references to Rokade strings or pointers...")
for i in range(min(count, 1000)):
    slot = r64(data + i * 8)
    if not slot: continue
    v10 = r64(slot + 0x10)
    v8 = r64(slot + 8)
    owner = v10 if v10 >= 0x10000000 else (v8 if v8 >= 0x10000000 else slot)
    
    # Check memory of owner (0x200 bytes)
    ow_data = rpm(owner, 0x200)
    if not ow_data: continue
    for off in range(0, 0x200, 8):
        val = struct.unpack('<Q', ow_data[off:off+8])[0]
        for h in hits:
            if abs(val - h) < 16:
                print(f"  FOUND Rokade in CharMgr Entity {i}: owner=0x{owner:x} + 0x{off:x} -> 0x{val:x}")
        # Also check if val is a string pointer that reads "Rokade"
        if val >= 0x10000000 and val < 0x7fffffffffff:
            s = read_str(val, 16)
            if "Rokade" in s:
                print(f"  FOUND string 'Rokade' in CharMgr Entity {i}: owner=0x{owner:x} + 0x{off:x} -> 0x{val:x} ('{s}')")
