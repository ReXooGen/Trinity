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

horse = 0x2BEDA0F0300
td = read_ptr(horse + 0x88)
tag = read_u8(td + 1) if td else 0
print(f"Horse 0x{horse:X}:")
print(f"  obj50 (partyIndex) = {read_u32(horse + 0x50)}")
print(f"  objType (0x48) = {read_u32(horse + 0x48)}")
print(f"  td (0x88) = 0x{td:X}, tag = {tag}")
print(f"  poss (0xA0) = 0x{read_ptr(horse + 0xA0):X}")
act = read_ptr(horse + 0x68)
print(f"  actor (0x68) = 0x{act:X}")

# Let's inspect equip component on the horse!
print("\n--- Inspecting horse components ---")
for off in range(0, 0x180, 8):
    v = read_ptr(horse + off)
    if v > 0x10000:
        print(f"  horse + 0x{off:X} = 0x{v:X}")

print("\n--- Inspecting horse actor components ---")
for off in range(0, 0x180, 8):
    v = read_ptr(act + off)
    if v > 0x10000:
        print(f"  act + 0x{off:X} = 0x{v:X}")
