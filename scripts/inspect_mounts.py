import ctypes, ctypes.wintypes, struct, subprocess

kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
psapi.EnumProcessModulesEx.argtypes = [ctypes.wintypes.HANDLE, ctypes.POINTER(ctypes.wintypes.HMODULE), ctypes.wintypes.DWORD, ctypes.POINTER(ctypes.wintypes.DWORD), ctypes.wintypes.DWORD]
psapi.GetModuleBaseNameA.argtypes = [ctypes.wintypes.HANDLE, ctypes.wintypes.HMODULE, ctypes.c_char_p, ctypes.wintypes.DWORD]

out = subprocess.check_output(['powershell', '-NoProfile', '-Command', '(Get-Process -Name CrimsonDesert).Id'])
pid = int(out.strip())
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

hMods = (ctypes.wintypes.HMODULE * 1024)()
cbNeeded = ctypes.wintypes.DWORD()
psapi.EnumProcessModulesEx(hProc, hMods, ctypes.sizeof(hMods), ctypes.byref(cbNeeded), 0x03)
nMods = cbNeeded.value // ctypes.sizeof(ctypes.wintypes.HMODULE)

trinity_base = 0
for i in range(nMods):
    mod = hMods[i]
    name_buf = ctypes.create_string_buffer(260)
    psapi.GetModuleBaseNameA(hProc, mod, name_buf, 260)
    if 'trinity' in name_buf.value.decode('latin-1').lower():
        trinity_base = mod
        break

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

def r16(addr):
    b = rpm(addr, 2)
    return struct.unpack('<H', b)[0] if b else 0

def r8(addr):
    b = rpm(addr, 1)
    return struct.unpack('<B', b)[0] if b else 0

def read_str(addr, maxlen=64):
    b = rpm(addr, maxlen)
    if not b: return ''
    idx = b.find(b'\x00')
    if idx != -1: b = b[:idx]
    return b.decode('utf-8', errors='ignore')

mount_acts = [r64(trinity_base + 0x1dbb68 + i * 8) for i in range(4)]
mount_owners = [r64(trinity_base + 0x1dbba8 + i * 8) for i in range(4)]

for i in range(4):
    act = mount_acts[i]
    owner = mount_owners[i]
    print(f'=== MOUNT {i} (act=0x{act:x}, owner=0x{owner:x}) ===')
    if not act: continue
    
    vt = r64(act)
    print(f'  Actor vtable: 0x{vt:x}')
    
    act_data = rpm(act, 0x300)
    for off in range(0, 0x300, 8):
        ptr = struct.unpack('<Q', act_data[off:off+8])[0]
        if ptr >= 0x10000000 and ptr < 0x7fffffffffff:
            s = read_str(ptr, 32)
            if s and s.isprintable() and len(s) >= 3:
                print(f'  act+0x{off:x} -> "{s}"')
    
    if owner:
        owner_data = rpm(owner, 0x300)
        for off in range(0, 0x300, 8):
            ptr = struct.unpack('<Q', owner_data[off:off+8])[0]
            if ptr >= 0x10000000 and ptr < 0x7fffffffffff:
                s = read_str(ptr, 32)
                if s and s.isprintable() and len(s) >= 3:
                    print(f'  owner+0x{off:x} -> "{s}"')

    for off in [0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0]:
        c = r64(act + off)
        if c >= 0x10000000:
            if c == 0x000003EB4F482300:
                print(f'  *** MATCH USER LOG comp=0x{c:x} at act+0x{off:x} ***')
            desc = r64(c + 0x80)
            if desc >= 0x10000000:
                arr = r64(desc + 8)
                cnt = r32(desc + 0x10)
                if arr >= 0x10000000 and cnt > 0 and cnt < 64:
                    print(f'  act+0x{off:x} is EquipComp 0x{c:x} (items count={cnt})')
                    for item_i in range(cnt):
                        entry = arr + item_i * 0xC8
                        tid = r16(entry + 2)
                        tag = r16(entry + 0xC0)
                        iid = r64(entry + 0x20)
                        if tid > 0 and tid != 0xffff:
                            print(f'    Item {item_i}: tid={tid} tag={tag} instId={iid}')
