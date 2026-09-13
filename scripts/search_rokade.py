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
PAGE_READWRITE = 0x04
PAGE_EXECUTE_READWRITE = 0x40

mbi = MEMORY_BASIC_INFORMATION()
addr = 0x10000000

print("Scanning memory for 'Rokade' and 'Exclaire'...")
hits_rokade = []
hits_exclaire = []

while kernel32.VirtualQueryEx(hProc, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    if mbi.State == MEM_COMMIT and (mbi.Protect & 0xEE) and not (mbi.Protect & 0x100):
        # Read chunk
        size = min(mbi.RegionSize, 10 * 1024 * 1024)
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t(0)
        if kernel32.ReadProcessMemory(hProc, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
            raw = buf.raw[:n.value]
            # Search ASCII and UTF-16
            for s in (b"Rokade", "Rokade".encode("utf-16le")):
                idx = 0
                while True:
                    idx = raw.find(s, idx)
                    if idx == -1: break
                    hits_rokade.append(addr + idx)
                    idx += len(s)
            for s in (b"Exclaire", "Exclaire".encode("utf-16le")):
                idx = 0
                while True:
                    idx = raw.find(s, idx)
                    if idx == -1: break
                    hits_exclaire.append(addr + idx)
                    idx += len(s)
    addr += mbi.RegionSize
    if addr > 0x7fffffffffff or (len(hits_rokade) > 10 and len(hits_exclaire) > 10):
        break

print(f"Rokade hits: {len(hits_rokade)}")
for h in hits_rokade[:10]:
    print(f"  0x{h:x}")
print(f"Exclaire hits: {len(hits_exclaire)}")
for h in hits_exclaire[:10]:
    print(f"  0x{h:x}")
