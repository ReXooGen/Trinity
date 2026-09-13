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

comp = 0x2BE81140C80 # Damiane
desc = read_ptr(comp + 0x90)
arr = read_ptr(desc + 8)
cnt = read_u32(desc + 0x10)
print(f"Damiane equip comp 0x{comp:X}: desc=0x{desc:X}, arr=0x{arr:X}, count={cnt}")
for i in range(cnt):
    entry = arr + i * 0xC8
    tid = read_u16(entry + 0x08)
    # Check where the slotTag is in entry!
    tags = []
    for o in range(0, 0xC8, 2):
        v = read_u16(entry + o)
        if v in [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]:
            tags.append(f"+0x{o:X}={v}")
    print(f"  Slot {i}: tid={tid} candidates for slotTag: {', '.join(tags)}")
