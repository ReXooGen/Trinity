"""Annotate a crashed thread's captured stack: module+offset for every pointer,
plus strings. Read-only analysis helper.

usage: stack_map.py <dump> [threadid]
"""
import struct, sys, os

buf = open(sys.argv[1], 'rb').read()
want_tid = int(sys.argv[2]) if len(sys.argv) > 2 else None

sig, ver, nstreams, dirrva, checksum, tds, flags = struct.unpack_from('<IIIIIIQ', buf, 0)
streams = {}
for i in range(nstreams):
    st, size, rva = struct.unpack_from('<III', buf, dirrva + i * 12)
    streams.setdefault(st, []).append((size, rva))

mods = []
_, rva = streams[4][0]
n = struct.unpack_from('<I', buf, rva)[0]
off = rva + 4
for i in range(n):
    base, size, chk, tds_, namerva = struct.unpack_from('<QIIII', buf, off)
    off += 108
    ln = struct.unpack_from('<I', buf, namerva)[0]
    nm = buf[namerva + 4:namerva + 4 + ln].decode('utf-16-le', 'replace')
    mods.append((base, size, os.path.basename(nm)))

def modfor(a):
    for base, size, nm in mods:
        if base <= a < base + size:
            return nm, a - base
    return None, None

# threads
_, rva = streams[3][0]
nthr = struct.unpack_from('<I', buf, rva)[0]
threads = []
p = rva + 4
for i in range(nthr):
    tid, susp, pc, prio, teb, stkstart, stksize, stkrva = struct.unpack_from('<IIIIQQII', buf, p)
    p += 48
    threads.append((tid, stkstart, stksize, stkrva))

# exception -> faulting thread + context
_, rva = streams[6][0]
etid = struct.unpack_from('<I', buf, rva)[0]
eaddr = struct.unpack_from('<Q', buf, rva + 24)[0]
ctxsize, ctxrva = struct.unpack_from('<II', buf, rva + 160)
ctx = buf[ctxrva:ctxrva + ctxsize]
rsp = struct.unpack_from('<Q', ctx, 0x98)[0]
rip = struct.unpack_from('<Q', ctx, 0xF8)[0]

tid = want_tid if want_tid is not None else etid
tgt = [t for t in threads if t[0] == tid][0]
_, stkstart, stksize, stkrva = tgt
blob = buf[stkrva:stkrva + stksize]
print("thread %d  stack %016X..%016X (%d bytes)  RSP=%016X RIP=%016X" %
      (tid, stkstart, stkstart + stksize, stksize, rsp, rip))
print("fault: %s+0x%X" % modfor(eaddr))

# figure out stack area below/above rsp
lo = stkstart
for o in range(0, max(0, len(blob) - 8), 8):
    v = struct.unpack_from('<Q', blob, o)[0]
    a = stkstart + o
    tag = ''
    nm, r = modfor(v)
    if nm:
        tag = "%s+0x%X" % (nm, r)
    mark = ' <<< RSP' if a == rsp else (' (above RSP)' if a > rsp else '')
    if tag or (a >= rsp - 0x200):
        print("  %016X: %016X  %s%s" % (a, v, tag, mark))

# strings anywhere in the stack blob
print("\n-- strings on stack --")
cur = b''
start = 0
for i, b in enumerate(blob):
    if 32 <= b < 127:
        if not cur:
            start = i
        cur += bytes([b])
    else:
        if len(cur) >= 4:
            print("  %016X  %s" % (stkstart + start, cur.decode('ascii')))
        cur = b''
