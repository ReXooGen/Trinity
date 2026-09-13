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

g = 0x146C29C88
p = read_ptr(g)
mgr = read_ptr(p) if p else 0
data = read_ptr(mgr + 0xB8)
count = read_u32(mgr + 0xC0)

print(f"Total entities: {count}")

for i in range(min(15, count)):
    ch = read_ptr(data + i * 8)
    if not ch: continue
    
    actor = read_ptr(ch + 0x68)
    td = read_ptr(ch + 0x88)
    tag = read_u8(td + 1) if td else 0
    poss = read_ptr(ch + 0xA0)
    
    # Check stats
    marker = read_ptr(actor + 0x20) if actor else 0
    root = read_ptr(marker + 0x18) if marker else 0
    sarr = read_ptr(root + 0x58) if root else 0
    has19 = False
    hp = 0
    if sarr:
        for s in range(30):
            st = read_u32(sarr + s * 0x90)
            if s == 0: hp = read_u32(sarr + 8)
            if st == 19: has19 = True
            
    # Check equip comp on actor or ch
    sub = read_ptr(actor + 0x68) if actor else 0
    comp = read_ptr(sub + 0x38) if sub else 0
    if not comp: comp = read_ptr(actor + 0x38) if actor else 0
    
    items = []
    if comp:
        tblDesc = read_ptr(comp + 0x80)
        if tblDesc:
            arr = read_ptr(tblDesc + 0x08)
            cnt = read_u32(tblDesc + 0x10)
            if arr and cnt < 50:
                for k in range(cnt):
                    entry = arr + k * 0xC8
                    tid = read_u16(entry + 0x08)
                    slotTag = read_u16(entry + 0xC0)
                    if tid > 0: items.append(f"tag{slotTag}:id{tid}")
                    
    print(f"[{i}] owner=0x{ch:X} act=0x{actor:X} tag={tag} poss=0x{poss:X} hp={hp} has19={has19}")
    print(f"     comp=0x{comp:X} items={items}")
