"""Run a local python analysis script against the Binary Ninja MCP endpoint.

The script gets a single global `mcp` with:
    mcp.call(tool_name, **args) -> parsed result (dict/list/str)
Convenience wrappers are provided for the tools we use most.

Usage:  python bn_batch.py <script.py>
"""
import json
import sys

import bn_mcp


def _content(res):
    if isinstance(res, dict) and "content" in res:
        parts = [c.get("text", "") for c in res["content"]]
        joined = "\n".join(parts)
        try:
            return json.loads(joined)
        except Exception:
            return joined
    return res


class MCP:
    def __init__(self):
        bn_mcp.connect()

    def call(self, tool, **args):
        r = bn_mcp.rpc("tools/call", {"name": tool, "arguments": args})
        if "error" in r:
            return {"_error": r["error"]}
        return _content(r.get("result", r))

    # ---- convenience ----
    def search(self, needle):
        return self.call("bn_function_search", query=needle)

    def info(self, fn):
        return self.call("bn_function_info", function=fn)

    def disasm(self, fn, limit=200):
        return self.call("bn_function_disassembly", function=fn, limit=limit)

    def decompile(self, fn):
        return self.call("bn_function_decompile", function=fn)

    def callees(self, fn):
        return self.call("bn_function_callees", function=fn)

    def callers(self, fn):
        return self.call("bn_function_callers", function=fn)

    def xrefs_to(self, addr):
        return self.call("bn_data_xrefs_to", address=addr)

    def read(self, address, length=64):
        return self.call("bn_memory_read", address=address, length=length)

    def strings(self, **kw):
        return self.call("bn_string_list", **kw)


def main():
    path = sys.argv[1]
    mcp = MCP()

    def out(*a):
        print(*a)

    g = {"mcp": mcp, "out": out, "json": json}
    src = open(path, encoding="utf-8").read()
    exec(compile(src, path, "exec"), g)


if __name__ == "__main__":
    main()
