import ctypes, struct, subprocess
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

out = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq CrimsonDesert.exe', '/FO', 'CSV', '/NH']).decode('utf-8', errors='ignore')
pid = int([p.strip('"') for p in out.strip().split('\n')[0].split(',')][1])
handle = kernel32.OpenProcess(0x1FFFFF, 0, pid)

def read_bytes(addr, size):
    buf = (ctypes.c_char * size)()
    rd = ctypes.c_size_t()
    if kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(rd)):
        return bytes(buf[:rd.value])
    return None

def read_u8(a): b = read_bytes(a, 1); return b[0] if b else 0
def read_u16(a): b = read_bytes(a, 2); return struct.unpack('<H', b)[0] if b else 0
def read_u32(a): b = read_bytes(a, 4); return struct.unpack('<I', b)[0] if b else 0
def read_ptr(a): b = read_bytes(a, 8); return struct.unpack('<Q', b)[0] if b else 0

comp = 0x2BEDA1A7580 # Kliff's comp
desc = read_ptr(comp + 0x90)
arr = read_ptr(desc + 8)
cnt = read_u32(desc + 0x10)
print(f"Kliff equip: desc=0x{desc:X}, arr=0x{arr:X}, count={cnt}")

for i in range(min(5, cnt)):
    entry = arr + i * 0xC8
    b = read_bytes(entry, 0xD0)
    print(f"\n--- Entry {i} (0x{entry:X}) ---")
    # print non-zero uint16s with offsets
    u16s = [f"+0x{off:X}:{val}" for off, val in [(o, struct.unpack('<H', b[o:o+2])[0]) for o in range(0, 0xC8, 2)] if val != 0]
    print("  Non-zero u16s:", ", ".join(u16s[:20]))
    
# Also check Horse comp
h_comp = 0x2BE81142800
h_desc = read_ptr(h_comp + 0x90)
h_arr = read_ptr(h_desc + 8)
h_cnt = read_u32(h_desc + 0x10)
print(f"\nHorse equip: desc=0x{h_desc:X}, arr=0x{h_arr:X}, count={h_cnt}")
for i in range(min(5, h_cnt)):
    entry = h_arr + i * 0xC8
    b = read_bytes(entry, 0xD0)
    print(f"\n--- Horse Entry {i} (0x{entry:X}) ---")
    u16s = [f"+0x{off:X}:{val}" for off, val in [(o, struct.unpack('<H', b[o:o+2])[0]) for o in range(0, 0xC8, 2)] if val != 0]
    print("  Non-zero u16s:", ", ".join(u16s[:20]))
