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

charMgrSlot = 0x146C29C88
mgr_ptr = r64(charMgrSlot)
mgr = r64(mgr_ptr) if mgr_ptr else 0
data = r64(mgr + 0xB0)
count = r32(mgr + 0x9C)

print(f"Total entities in CharMgr: {count}")

results = []

for i in range(min(count, 1000)):
    slot = r64(data + i * 8)
    if not slot: continue
    v10 = r64(slot + 0x10)
    v8 = r64(slot + 8)
    owner = v10 if v10 >= 0x10000000 else (v8 if v8 >= 0x10000000 else slot)
    actor = r64(owner + 0x68)
    if not actor: actor = owner

    # Scan for equip components on owner and actor
    found_comps = []
    candidates = [owner, actor]
    sub = r64(owner + 0x68)
    if sub >= 0x10000000: candidates.append(sub)
    sub2 = r64(actor + 8)
    if sub2 >= 0x10000000: candidates.append(sub2)
    sub3 = r64(owner + 0x38)
    if sub3 >= 0x10000000: candidates.append(sub3)

    for c_cand in candidates:
        for toff in (0x80, 0x88):
            desc = r64(c_cand + toff)
            if desc >= 0x10000000 and desc < 0x7fffffffffff:
                arr = r64(desc + 8)
                cnt = r32(desc + 0x10)
                if arr >= 0x10000000 and 0 < cnt < 64:
                    found_comps.append((c_cand, toff, arr, cnt))

    if found_comps:
        objType = r32(owner + 0x48) & 0xFF
        partyIdx = r32(owner + 0x50)
        items = []
        for comp, toff, arr, cnt in found_comps:
            for item_i in range(cnt):
                # Try both strides: 0xC8 and 0xD0
                for stride, tagOff in ((0xC8, 0xC0), (0xD0, 0xC8)):
                    entry = arr + item_i * stride
                    tid = r16(entry + 2)
                    tag = r16(entry + tagOff)
                    iid = r64(entry + 0x20)
                    if 0 < tid < 0xFFFF and tag < 32:
                        items.append((comp, item_i, tid, tag, iid))
                        break
        if items:
            results.append((i, owner, actor, objType, partyIdx, items))

print(f"Entities with items: {len(results)}")
for r in results:
    print(f"Idx {r[0]}: owner=0x{r[1]:x} actor=0x{r[2]:x} objType={r[3]} partyIdx={r[4]}")
    for it in r[5]:
        print(f"    comp=0x{it[0]:x} item[{it[1]}]: tid={it[2]} tag={it[3]} instId={it[4]}")
