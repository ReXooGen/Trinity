# Locate where an added item (by InstID) physically landed: scan committed
# readable memory for the i64 instance id, then classify the containing
# structure (equip entry / holder slot / bucket) and print context.
import ctypes
import ctypes.wintypes as wintypes

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
READABLE = {0x02, 0x04, 0x20, 0x40}  # READONLY, READWRITE, EXECUTE_READ, EXECUTE_READWRITE

def get_pid(name="CrimsonDesert.exe"):
    TH32CS_SNAPPROCESS = 0x2
    class PE(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD),
                    ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", ctypes.c_char * 260)]
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PE(); pe.dwSize = ctypes.sizeof(pe)
    ok = k32.Process32First(snap, ctypes.byref(pe))
    pid = None
    while ok:
        if pe.szExeFile.decode() == name:
            pid = pe.th32ProcessID; break
        ok = k32.Process32Next(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    return pid

pid = get_pid()
h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
if not h:
    print("cannot open process"); raise SystemExit(1)

def read(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
    return buf.raw[:got.value] if ok else None

TARGETS = {
    0xF5725: "ShieldOfRadiance_added",
    0xF5726: "ShieldOfRinging_added",
}
# also a known-equipped InstID for structural comparison
KNOWN_EQUIP = 10439  # Fated Shadow instId

class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD), ("RegionSize", ctypes.c_size_t),
                ("State", wintypes.DWORD), ("Protect", wintypes.DWORD),
                ("Type", wintypes.DWORD)]

addr = 0
regions = []
while True:
    mbi = MBI()
    res = k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
    if not res: break
    if mbi.State == MEM_COMMIT and mbi.Protect in READABLE and mbi.RegionSize < 0x2000000:
        regions.append((mbi.BaseAddress or 0, mbi.RegionSize))
    addr = (mbi.BaseAddress or 0) + mbi.RegionSize
    if addr > 0x7FFFFFFEFFFF: break

print(f"scanning {len(regions)} regions...")
needles = {int(k).to_bytes(8, "little"): v for k, v in TARGETS.items()}
needles[int(KNOWN_EQUIP).to_bytes(8, "little")] = "FatedShadow_equipped"
hits = {v: [] for v in list(TARGETS.values()) + ["FatedShadow_equipped"]}
for base, size in regions:
    blob = read(base, size)
    if not blob: continue
    for pat, label in needles.items():
        start = 0
        while True:
            i = blob.find(pat, start)
            if i == -1: break
            hits[label].append(base + i)
            start = i + 1
            if len(hits[label]) > 12: break

for label, addrs in hits.items():
    print(f"\n=== {label}: {len(addrs)} hit(s)")
    for a in addrs[:6]:
        ctx = read(a - 32, 80)
        print(f"  {a:#x}  ...{ctx.hex(' ').upper()}")
k32.CloseHandle(h)
