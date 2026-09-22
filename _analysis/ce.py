"""Direct JSON-RPC client for the Cheat Engine MCP named-pipe bridge.

Bypasses mcp_cheatengine.py entirely, so it is immune to the contention between
the several MCP server instances attached to the same pipe.

Usage:
    python ce.py <method> '<json-params>'
    python ce.py --py <file.lua>          # evaluate_lua with a Lua file
"""
import json
import sys
import time

import win32file
import win32pipe

PIPE = r"\\.\pipe\CE_MCP_Bridge_v99"


class CE:
    def __init__(self, timeout=60.0):
        self.timeout = timeout
        self.handle = None
        self._id = 0

    def _open(self):
        self.handle = win32file.CreateFile(
            PIPE,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0, None, win32file.OPEN_EXISTING, 0, None,
        )

    def _read_exact(self, n):
        buf = b""
        deadline = time.time() + self.timeout
        while len(buf) < n:
            if time.time() > deadline:
                raise TimeoutError(f"timeout reading {n} bytes (got {len(buf)})")
            avail = win32pipe.PeekNamedPipe(self.handle, 0)[1]
            if avail == 0:
                time.sleep(0.005)
                continue
            _, chunk = win32file.ReadFile(self.handle, min(avail, n - len(buf)))
            buf += chunk
        return buf

    def call(self, method, params=None):
        if self.handle is None:
            self._open()
        self._id += 1
        req = json.dumps({"jsonrpc": "2.0", "id": self._id,
                          "method": method, "params": params or {}}).encode()
        win32file.WriteFile(self.handle, len(req).to_bytes(4, "little") + req)
        n = int.from_bytes(self._read_exact(4), "little")
        body = self._read_exact(n).decode("utf-8", "replace")
        try:
            return json.loads(body)
        except Exception:
            return {"_raw": body}

    def close(self):
        if self.handle is not None:
            try:
                win32file.CloseHandle(self.handle)
            except Exception:
                pass
            self.handle = None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    ce = CE()
    try:
        if sys.argv[1] == "--py":
            code = open(sys.argv[2], encoding="utf-8").read()
            print(json.dumps(ce.call("evaluate_lua", {"code": code}), indent=2)[:200000])
        else:
            params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
            print(json.dumps(ce.call(sys.argv[1], params), indent=2)[:200000])
    finally:
        ce.close()


if __name__ == "__main__":
    main()
