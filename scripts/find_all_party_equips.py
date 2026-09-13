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

targets = [
    ("Kliff (Tag 1)", 0x2BEDA0F0200),
    ("Damiane (Tag 4)", 0x2BEDA0F0300),
    ("Oongka (Tag 4)", 0x2BEDA0F0400),
    ("Horse (Tag 5)", 0x2BEDA0F0500),
]

for name, owner in targets:
    print(f"\n==================== {name}: 0x{owner:X} ====================")
    act = read_ptr(owner + 0x68)
    print(f"Actor: 0x{act:X}")
    
    # Check all pointers on owner and actor to see which one is an EquipComp
    # An equip comp has a TableDesc at +0x80 or +0x58 or +0x60 or +0x88
    for src_name, base_addr in [("owner", owner), ("actor", act)]:
        for off in range(0x20, 0x180, 8):
            ptr = read_ptr(base_addr + off)
            if ptr and ptr > 0x10000:
                # Check if ptr is an equip comp
                for tOff in [0x80, 0x88, 0x90, 0x50, 0x78, 0x38, 0x40, 0x60, 0x70]:
                    desc = read_ptr(ptr + tOff)
                    if desc and desc > 0x10000:
                        arr = read_ptr(desc + 8)
                        cnt = read_u32(desc + 0x10)
                        if arr and arr > 0x10000 and cnt > 0 and cnt <= 64:
                            # Read slots
                            gear = []
                            for i in range(cnt):
                                entry = arr + i * 0xC8
                                tid = read_u16(entry + 8)
                                tag = read_u16(entry + 0xC0)
                                if tid > 0 and tid != 0xFFFF:
                                    gear.append(f"tag{tag}:id{tid}")
                            if gear:
                                print(f"  FOUND EQUIP COMP via {src_name}+0x{off:X} -> 0x{ptr:X} (desc at +0x{tOff:X}, count={cnt}):")
                                print(f"     Gear: {', '.join(gear)}")
                                
                # Check subcontainer *(ptr + 0x38)
                sub_comp = read_ptr(ptr + 0x38)
                if sub_comp and sub_comp > 0x10000:
                    desc = read_ptr(sub_comp + 0x80)
                    if desc and desc > 0x10000:
                        arr = read_ptr(desc + 8)
                        cnt = read_u32(desc + 0x10)
                        if arr and arr > 0x10000 and cnt > 0 and cnt <= 64:
                            gear = []
                            for i in range(cnt):
                                entry = arr + i * 0xC8
                                tid = read_u16(entry + 8)
                                tag = read_u16(entry + 0xC0)
                                if tid > 0 and tid != 0xFFFF:
                                    gear.append(f"tag{tag}:id{tid}")
                            if gear:
                                print(f"  FOUND EQUIP COMP via {src_name}+0x{off:X}->sub+0x38 -> 0x{sub_comp:X}:")
                                print(f"     Gear: {', '.join(gear)}")
