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

def r16(addr):
    b = rpm(addr, 2)
    return struct.unpack('<H', b)[0] if b else 0

def read_str(addr, maxlen=64):
    b = rpm(addr, maxlen)
    if not b: return ''
    idx = b.find(b'\x00')
    if idx != -1: b = b[:idx]
    return b.decode('utf-8', errors='ignore')

def get_rtti(obj):
    vt = r64(obj)
    if not vt: return ""
    col = r64(vt - 8)
    if not col: return ""
    td = r32(col + 0xC)
    if not td: return ""
    return read_str(0x140000000 + td + 0x10, 64)

# Kliff, Damiane, Oongka actors
actors = [(0x3eb46035140, "Kliff"), (0x3eb4ec2f500, "Damiane"), (0x3eb4ec2d5c0, "Oongka")]

for act, name in actors:
    print(f"=== {name} (0x{act:x}) ===")
    p8 = r64(act + 8)
    print(f"  act+8: 0x{p8:x} ({get_rtti(p8)})")
    if p8 >= 0x10000000 and p8 < 0x7fffffffffff:
        for off in range(0, 0x200, 8):
            comp = r64(p8 + off)
            if comp >= 0x10000000 and comp < 0x7fffffffffff:
                rtti = get_rtti(comp)
                desc = r64(comp + 0x80)
                cnt = r32(desc + 0x10) if desc >= 0x10000000 else 0
                if cnt > 0 or "Equip" in rtti:
                    print(f"    p8+0x{off:x} -> 0x{comp:x} (rtti='{rtti}', count={cnt})")
                    if cnt > 0:
                        arr = r64(desc + 8)
                        for item_i in range(min(cnt, 5)):
                            entry = arr + item_i * 0xC8
                            tid = r16(entry + 2)
                            tag = r16(entry + 0xC0)
                            print(f"      Item {item_i}: tid={tid} tag={tag}")
