import ctypes, ctypes.wintypes, struct, subprocess

kernel32 = ctypes.windll.kernel32
out = subprocess.check_output(['powershell', '-NoProfile', '-Command', '(Get-Process -Name CrimsonDesert).Id'])
pid = int(out.strip())
hProc = kernel32.OpenProcess(0x1F0FFF, False, pid)

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("PartitionId", ctypes.wintypes.WORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
    ]

MEM_COMMIT = 0x1000
mbi = MEMORY_BASIC_INFORMATION()
addr = 0x10000000

target_val = struct.pack('<Q', 1000008)
print(f"Scanning memory for instanceId 1000008 (0x{1000008:x})...")

hits = []
while kernel32.VirtualQueryEx(hProc, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    if mbi.State == MEM_COMMIT and (mbi.Protect & 0xEE) and not (mbi.Protect & 0x100):
        size = min(mbi.RegionSize, 10 * 1024 * 1024)
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t(0)
        if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
            raw = buf.raw[:n.value]
            idx = 0
            while True:
                idx = raw.find(target_val, idx)
                if idx == -1: break
                hits.append(addr + idx)
                idx += 8
    addr += mbi.RegionSize
    if addr > 0x7fffffffffff or len(hits) >= 20:
        break

print(f"Found {len(hits)} hits for instId 1000008:")
for h in hits:
    print(f"  Hit at 0x{h:x}")
    # Inspect entry around h:
    # If h is at entry + 0x20: entry = h - 0x20
    entry = h - 0x20
    ebuf = ctypes.create_string_buffer(0xD0)
    n = ctypes.c_size_t(0)
    if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(entry), ebuf, 0xD0, ctypes.byref(n)):
        raw = ebuf.raw
        tid = struct.unpack('<H', raw[2:4])[0]
        tagC0 = struct.unpack('<H', raw[0xC0:0xC2])[0]
        tagC8 = struct.unpack('<H', raw[0xC8:0xCA])[0]
        dcount80 = struct.unpack('<I', raw[0x80:0x84])[0]
        print(f"    Possible Entry @ 0x{entry:x}: tid={tid} tag(0xC0)={tagC0} tag(0xC8)={tagC8} dyeCount={dcount80}")
