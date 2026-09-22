"""Direct named-pipe probe of the CE MCP bridge (bypasses the MCP server)."""
import json
import sys
import time

try:
    import win32file
    import win32pipe
except ImportError:
    print("pywin32 missing")
    sys.exit(2)

PIPE = r"\\.\pipe\CE_MCP_Bridge_v99"

try:
    h = win32file.CreateFile(
        PIPE,
        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
        0, None, win32file.OPEN_EXISTING, 0, None,
    )
except Exception as e:
    print("open failed:", e)
    sys.exit(3)

print("pipe opened")

req = json.dumps({"id": 1, "command": "ping", "params": {}}).encode()
ok, written = win32file.WriteFile(h, len(req).to_bytes(4, "little") + req)
print("wrote", written)

t0 = time.time()
got = win32pipe.PeekNamedPipe(h, 0)
while time.time() - t0 < 8:
    avail = win32pipe.PeekNamedPipe(h, 0)[1]
    if avail >= 4:
        break
    time.sleep(0.05)

avail = win32pipe.PeekNamedPipe(h, 0)[1]
print("available after wait:", avail, "elapsed", round(time.time() - t0, 2))
if avail >= 4:
    _, hdr = win32file.ReadFile(h, 4)
    n = int.from_bytes(hdr, "little")
    _, body = win32file.ReadFile(h, n)
    print("response:", body.decode("utf-8", "replace")[:800])
else:
    print("NO RESPONSE (bridge blocked or not servicing)")

win32file.CloseHandle(h)
