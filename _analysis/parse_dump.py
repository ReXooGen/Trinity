"""Read-only minidump analyser (analysis helper, does not touch mod source).

Extracts: exception record + faulting thread context, module list, and scans the
faulting thread stack for return addresses / strings that point into loaded
modules (esp. Trinity.asi and CrimsonDesert.exe).
"""
import struct
import sys
import os

path = sys.argv[1]
buf = open(path, 'rb').read()
print("dmp:", path, "size:", len(buf))

sig, ver, nstreams, dirrva, checksum, tds, flags = struct.unpack_from('<IIIIIIQ', buf, 0)
print("signature: %08X streams=%d flags=0x%X timestamp=%d" % (sig, nstreams, flags, tds))

streams = {}
for i in range(nstreams):
    st, size, rva = struct.unpack_from('<III', buf, dirrva + i * 12)
    streams.setdefault(st, []).append((size, rva))
print("stream types:", sorted(streams.keys()))
names = {3: 'ThreadList', 4: 'ModuleList', 5: 'MemoryList', 6: 'Exception',
         7: 'SystemInfo', 9: 'Memory64List', 15: 'MiscInfo', 16: 'MemoryInfoList'}
for k in sorted(streams):
    print("  stream %-4d %-14s %s" % (k, names.get(k, '?'), streams[k]))

# ---------------- modules ----------------
mods = []
if 4 in streams:
    _, rva = streams[4][0]
    n = struct.unpack_from('<I', buf, rva)[0]
    off = rva + 4
    for i in range(n):
        base, size, chk, tds_, namerva = struct.unpack_from('<QIIII', buf, off)
        # MINIDUMP_MODULE is 108 bytes
        off += 108
        ln = struct.unpack_from('<I', buf, namerva)[0]
        nm = buf[namerva + 4:namerva + 4 + ln].decode('utf-16-le', 'replace')
        mods.append((base, size, nm, tds_, chk))
print("\n=== MODULES (%d) ===" % len(mods))
for base, size, nm, tds_, chk in sorted(mods):
    print("  %016X - %016X  ts=%08X  %s" % (base, base + size, tds_, nm))

def modfor(addr):
    for base, size, nm, tds_, chk in mods:
        if base <= addr < base + size:
            return nm, base, addr - base
    return None, None, None

# ---------------- memory access ----------------
ranges = []
# MINIDUMP_MEMORY_LIST (stream 5): each descriptor is StartOfMemoryRange u64 +
# (DataSize u32, Rva u32) = 16 bytes, with data stored inline at Rva.
if 5 in streams:
    _, rva = streams[5][0]
    nr = struct.unpack_from('<I', buf, rva)[0]
    p = rva + 4
    for i in range(nr):
        start, dsize, drva = struct.unpack_from('<QII', buf, p)
        p += 16
        ranges.append((start, dsize, drva))
if 9 in streams:
    _, rva = streams[9][0]
    nr = struct.unpack_from('<Q', buf, rva)[0]
    baserva = struct.unpack_from('<Q', buf, rva + 8)[0]
    p = rva + 16
    cur = baserva
    for i in range(nr):
        start, dsize = struct.unpack_from('<QQ', buf, p)
        p += 16
        ranges.append((start, dsize, cur))
        cur += dsize
print("\nmemory64 ranges: %d" % len(ranges))

def vread(addr, size):
    for start, dsize, fileoff in ranges:
        if start <= addr and addr + size <= start + dsize:
            o = fileoff + (addr - start)
            return buf[o:o + size]
    return None

def vread_avail(addr, size):
    """Return as much as the dump actually holds at addr (clamped)."""
    for start, dsize, fileoff in ranges:
        if start <= addr < start + dsize:
            n = min(size, start + dsize - addr)
            o = fileoff + (addr - start)
            return buf[o:o + n]
    return None

# ---------------- exception ----------------
if 6 not in streams:
    print("no exception stream")
    sys.exit(0)
_, rva = streams[6][0]
tid, _align = struct.unpack_from('<II', buf, rva)
ecode, eflags, erec, eaddr, nparam, _ua = struct.unpack_from('<IIQQII', buf, rva + 8)
params = struct.unpack_from('<15Q', buf, rva + 40)
print("\n=== EXCEPTION ===")
print("  ThreadId        : %d (0x%X)" % (tid, tid))
print("  ExceptionCode   : 0x%08X" % ecode)
print("  ExceptionFlags  : 0x%08X" % eflags)
print("  ExceptionAddress: 0x%016X  (%s+0x%X)" % (eaddr, modfor(eaddr)[0], modfor(eaddr)[2] or 0))
print("  NumberParameters: %d" % nparam)
for i in range(nparam):
    print("     param[%d] = 0x%016X" % (i, params[i]))

ctxsize, ctxrva = struct.unpack_from('<II', buf, rva + 160)
ctx = buf[ctxrva:ctxrva + ctxsize]
regs = {}
RN = ['Rax', 'Rcx', 'Rdx', 'Rbx', 'Rsp', 'Rbp', 'Rsi', 'Rdi', 'R8', 'R9', 'R10',
      'R11', 'R12', 'R13', 'R14', 'R15', 'Rip']
for i, nm in enumerate(RN):
    regs[nm] = struct.unpack_from('<Q', ctx, 0x78 + i * 8)[0]
EFLAGS = struct.unpack_from('<I', ctx, 0x44)[0]
print("\n=== FAULTING THREAD CONTEXT ===")
for i in range(0, len(RN), 4):
    print("  " + "  ".join("%-4s: 0x%016X" % (RN[j], regs[RN[j]]) for j in range(i, min(i + 4, len(RN)))))
print("  EFLAGS: 0x%08X" % EFLAGS)

# ---------------- stack scan ----------------
rsp = regs['Rsp']
print("\n=== STACK SCAN from RSP=0x%016X (0x%X bytes) ===" % (rsp, 0x10000))
st = vread_avail(rsp, 0x10000)
print("  captured stack bytes: %d" % (len(st) if st else 0))
if st is None:
    st = b''
    print("  (stack memory not present in dump)")
else:
    hits = []
    for o in range(0, len(st) - 8, 8):
        v = struct.unpack_from('<Q', st, o)[0]
        nm, base, r = modfor(v)
        if nm:
            hits.append((rsp + o, v, nm, r))
    print("  pointer hits into modules: %d" % len(hits))
    for a, v, nm, r in hits:
        print("   [sp+0x%05X] %016X  %s+0x%X" % (a - rsp, v, os.path.basename(nm), r))

# strings on the stack (menu labels can survive here)
def strings(blob, base):
    out = []
    cur = b''
    start = 0
    for i, b in enumerate(blob):
        if 32 <= b < 127:
            if not cur:
                start = i
            cur += bytes([b])
        else:
            if len(cur) >= 5:
                out.append((base + start, cur.decode('ascii')))
            cur = b''
    if len(cur) >= 5:
        out.append((base + start, cur.decode('ascii')))
    return out

if st:
    print("\n=== ASCII strings on stack (len>=5) ===")
    seen = set()
    for a, s in strings(st, rsp):
        if s in seen:
            continue
        seen.add(s)
        print("   0x%016X  %s" % (a, s))

# ---------------- all threads: hunt for Trinity.asi frames ----------------
TRIN = None
for base, size, nm, tds_, chk in mods:
    if os.path.basename(nm).lower() == 'trinity.asi':
        TRIN = (base, size)
print("\n=== TRINITY.ASI module: %s ===" % (TRIN,))

if 3 in streams and TRIN:
    _, rva = streams[3][0]
    nthr = struct.unpack_from('<I', buf, rva)[0]
    p = rva + 4
    print("\n=== PER-THREAD STACK SCAN (Trinity.asi frames) ===")
    for i in range(nthr):
        tid_, susp, pc, prio, teb, stkstart, stksize, stkrva = struct.unpack_from('<IIIIQQII', buf, p)
        p += 48
        blob = buf[stkrva:stkrva + stksize] if stksize else b''
        found = []
        for o in range(0, max(0, len(blob) - 8), 8):
            v = struct.unpack_from('<Q', blob, o)[0]
            if TRIN[0] <= v < TRIN[0] + TRIN[1]:
                found.append((v - TRIN[0], v))
        if found:
            print("  thread %-6d (teb=%016X) stack %016X..%016X : %d trinity frame(s)" %
                  (tid_, teb, stkstart, stkstart + stksize, len(found)))
            for r, v in found:
                print("        Trinity.asi+0x%-6X  (0x%016X)" % (r, v))
