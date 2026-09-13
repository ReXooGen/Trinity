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
def read_vec3(a):
    b = read_bytes(a, 12)
    return struct.unpack('<fff', b) if b else (0.0, 0.0, 0.0)

g = 0x146C29C88
p = read_ptr(g)
mgr = read_ptr(p) if p else 0
data = read_ptr(mgr + 0xB8)
count = read_u32(mgr + 0xC0)

player_owner = 0
player_poss = 0
for i in range(count):
    ch = read_ptr(data + i * 8)
    if not ch or ch < 0x10000: continue
    poss = read_ptr(ch + 0xA0)
    if poss > 0x10000 and read_ptr(poss + 0xD0) == ch:
        player_owner = ch
        player_poss = poss
        break

print(f"Player owner: 0x{player_owner:X}, poss: 0x{player_poss:X}")
player_actor = read_ptr(player_owner + 0x68)
px, py, pz = read_vec3(player_owner + 0x90)
if px == 0 and py == 0 and pz == 0:
    px, py, pz = read_vec3(player_actor + 0x90)

print(f"Player pos: ({px:.2f}, {py:.2f}, {pz:.2f})")

# Check all fields of player_owner and player_actor for mount links!
print("\n--- Scanning Player Owner pointers ---")
for off in range(0, 0x200, 8):
    val = read_ptr(player_owner + off)
    if val > 0x10000:
        # Check if val has gauge 19
        marker = read_ptr(val + 0x20)
        root = read_ptr(marker + 0x18) if marker else 0
        sarr = read_ptr(root + 0x58) if root else 0
        has19 = False
        if sarr:
            for s in range(25):
                if read_u32(sarr + s * 0x90) == 19: has19 = True
        if has19:
            print(f"  player_owner + 0x{off:X} -> 0x{val:X} HAS GAUGE 19!")

print("\n--- Scanning Player Actor pointers ---")
for off in range(0, 0x200, 8):
    val = read_ptr(player_actor + off)
    if val > 0x10000:
        marker = read_ptr(val + 0x20)
        root = read_ptr(marker + 0x18) if marker else 0
        sarr = read_ptr(root + 0x58) if root else 0
        has19 = False
        if sarr:
            for s in range(25):
                if read_u32(sarr + s * 0x90) == 19: has19 = True
        if has19:
            print(f"  player_actor + 0x{off:X} -> 0x{val:X} HAS GAUGE 19!")

print("\n--- Entities within 50m of player with gauge 19 ---")
for i in range(count):
    ch = read_ptr(data + i * 8)
    if not ch or ch < 0x10000: continue
    
    actor = read_ptr(ch + 0x68)
    if not actor: actor = ch
    
    # Position can be at ch + 0x90 or actor + 0x90
    ex, ey, ez = read_vec3(ch + 0x90)
    if ex == 0 and ey == 0 and ez == 0:
        ex, ey, ez = read_vec3(actor + 0x90)
        
    dist = ((ex - px)**2 + (ey - py)**2 + (ez - pz)**2)**0.5
    
    marker = read_ptr(actor + 0x20)
    root = read_ptr(marker + 0x18) if marker else 0
    sarr = read_ptr(root + 0x58) if root else 0
    has19 = False
    hp = 0
    if sarr:
        for s in range(30):
            st = read_u32(sarr + s * 0x90)
            if s == 0 and st == 0: hp = read_u32(sarr + s * 0x90 + 8)
            if st == 19: has19 = True
            
    if has19:
        poss = read_ptr(ch + 0xA0)
        objType = read_u32(ch + 0x48)
        obj50 = read_u32(ch + 0x50)
        print(f"[{i}] owner=0x{ch:X} act=0x{actor:X} pos=({ex:.1f},{ey:.1f},{ez:.1f}) dist={dist:.1f}m hp={hp} objType={objType} obj50={obj50} poss=0x{poss:X}")
