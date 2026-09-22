#!/usr/bin/env python3
"""Print selected scan rows in a compact readable form."""
import json
import sys

path = "docs/audits/pe2949-scan.json"
data = json.load(open(path, encoding="utf-8"))
wanted = sys.argv[1:]

for row in data["results"]:
    if wanted and row["symbol"] not in wanted:
        continue
    m = row["matches"][0] if row["matches"] else None
    loc = ""
    if m:
        loc = "{} rva=0x{:X} va=0x{:X} off={}".format(
            m.get("section"), m["rva"], m["va"],
            ("0x%X" % m["file_offset"]) if isinstance(m.get("file_offset"), int) else "n/a")
    print("{}  [{}]  {}".format(row["symbol"], row["status"], loc))
    print("   file: {}:{}".format(row["file"], row["line"]))
    print("   usage: {}".format(",".join(row["usage"]["modes"]) or "-"))
    print("   consumers: {}".format(", ".join(row["consumers"]) or "-"))
    if row.get("doc"):
        print("   doc: {}".format(row["doc"][:300]))
    print()
