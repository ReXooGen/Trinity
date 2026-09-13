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

owner = 0x3eb4f482510
print(f'Owner: 0x{owner:x}')

vt = r64(owner)
print(f'Owner vtable: 0x{vt:x}')
rtti_complete = r64(vt - 8)
print(f'RTTI complete locator: 0x{rtti_complete:x}')
if rtti_complete:
    type_desc = r32(rtti_complete + 0xC)
    type_desc_va = 0x140000000 + type_desc
    type_name = read_str(type_desc_va + 0x10, 64)
    print(f'Owner Type Name: {type_name}')

comp_vt = r64(0x000003EB4F482300)
comp_rtti = r64(comp_vt - 8)
if comp_rtti:
    type_desc = r32(comp_rtti + 0xC)
    type_name = read_str(0x140000000 + type_desc + 0x10, 64)
    print(f'Comp Type Name: {type_name}')

# Check fields of owner
for off in range(0, 0x100, 8):
    val = r64(owner + off)
    s = read_str(val, 32)
    if s and s.isprintable() and len(s) >= 3:
        print(f'  +0x{off:x}: 0x{val:x} -> "{s}"')
