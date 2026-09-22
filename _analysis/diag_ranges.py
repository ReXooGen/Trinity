"""Diagnostic: list minidump memory ranges + threads (read-only)."""
import struct, sys

buf = open(sys.argv[1], 'rb').read()
sig, ver, nstreams, dirrva, checksum, tds, flags = struct.unpack_from('<IIIIIIQ', buf, 0)
streams = {}
for i in range(nstreams):
    st, size, rva = struct.unpack_from('<III', buf, dirrva + i * 12)
    streams.setdefault(st, []).append((size, rva))

if 5 in streams:
    _, rva = streams[5][0]
    nr = struct.unpack_from('<I', buf, rva)[0]
    print("MemoryList ranges:", nr)
    p = rva + 4
    rs = []
    for i in range(nr):
        start, dsize, drva = struct.unpack_from('<QII', buf, p)
        p += 16
        rs.append((start, dsize, drva))
    rs.sort()
    print("lowest 5 :", [(hex(a), hex(s)) for a, s, _ in rs[:5]])
    print("highest 5:", [(hex(a), hex(s)) for a, s, _ in rs[-5:]])
    target = 0x13FED40
    hit = [r for r in rs if r[0] <= target < r[0] + r[1]]
    print("range containing RSP %X: %s" % (target, [(hex(a), hex(s)) for a, s, _ in hit]))
    near = [r for r in rs if abs(r[0] - target) < 0x400000]
    print("ranges within 4MB of RSP: %d" % len(near))

if 3 in streams:
    _, rva = streams[3][0]
    nthr = struct.unpack_from('<I', buf, rva)[0]
    print("\nThreads:", nthr)
    p = rva + 4
    for i in range(nthr):
        tid, susp, pc, prio, teb, stkstart, stksize, stkrva = struct.unpack_from('<IIIIQQII', buf, p)
        p += 48
        print("  tid=%-6d teb=%016X stack=%016X size=%-8d rva=%d" %
              (tid, teb, stkstart, stksize, stkrva))
