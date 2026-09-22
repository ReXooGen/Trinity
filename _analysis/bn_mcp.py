"""Minimal MCP (streamable HTTP) client for the Binary Ninja UI MCP endpoint.

Usage:
    python bn_mcp.py list
    python bn_mcp.py call <tool_name> '<json-args>'
    python bn_mcp.py py <file.py>          # run a python file via the 'execute' tool
"""
import json
import sys
import urllib.request
import urllib.error

URL = "http://127.0.0.1:24642/mcp"
SESSION = {"id": None}
_id = [0]


def _post(payload, expect_reply=True):
    data = json.dumps(payload).encode("utf-8")
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if SESSION["id"]:
        headers["mcp-session-id"] = SESSION["id"]
    req = urllib.request.Request(URL, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            sid = resp.headers.get("mcp-session-id")
            if sid:
                SESSION["id"] = sid
            body = resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        raise SystemExit(f"HTTP {e.code}: {e.read().decode('utf-8', 'replace')[:800]}")
    if not expect_reply or not body.strip():
        return None
    # streamable HTTP: body is SSE frames or a bare JSON document
    if body.lstrip().startswith("{"):
        return json.loads(body)
    for line in body.splitlines():
        if line.startswith("data:"):
            return json.loads(line[5:].strip())
    return None


def rpc(method, params=None, notify=False):
    _id[0] += 1
    payload = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        payload["params"] = params
    if not notify:
        payload["id"] = _id[0]
    return _post(payload, expect_reply=not notify)


def connect():
    r = rpc("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "trinity-recon", "version": "1.0"},
    })
    rpc("notifications/initialized", {}, notify=True)
    return r


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    init = connect()
    srv = (init or {}).get("result", {}).get("serverInfo", {})
    print(f"[mcp] connected: {srv}", file=sys.stderr)

    if cmd == "list":
        r = rpc("tools/list", {})
        for t in r["result"]["tools"]:
            print(f"- {t['name']}: {t.get('description','')[:160]}")
    elif cmd == "call":
        name = sys.argv[2]
        args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
        r = rpc("tools/call", {"name": name, "arguments": args})
        res = r.get("result", r)
        for c in res.get("content", []):
            print(c.get("text", ""))
        if res.get("isError"):
            print("[isError]", file=sys.stderr)
    elif cmd == "py":
        code = open(sys.argv[2], encoding="utf-8").read()
        # tool name is discovered lazily; 'execute' is the documented one
        for tool in ("execute", "run_python", "python"):
            r = rpc("tools/call", {"name": tool, "arguments": {"code": code}})
            if "error" not in r:
                res = r.get("result", {})
                for c in res.get("content", []):
                    print(c.get("text", ""))
                return
        raise SystemExit("no execute-like tool found; run `list`")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
