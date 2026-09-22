#!/usr/bin/env python3
"""Print the kCharMgrAnchors consensus detail from a scan JSON."""
import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "docs/audits/pe2949-scan.json"
data = json.load(open(path, encoding="utf-8"))

for row in data["results"]:
    if row["kind"] != "anchors":
        continue
    print(f"{row['symbol']}  status={row['status']}")
    for a in row["anchor_results"]:
        target = ""
        if a["matches"]:
            m = a["matches"][0]
            target = "rva=0x{:X} global=0x{:X}".format(m["rva"], m["global_target"])
        print("  [{}] movOff={:<5} matches={:<3} {}".format(
            a["index"], a["movOff"], a["match_count"], target))
        print("       {}".format(a["pattern"]))
        for m in a["matches"][:1]:
            for insn in m["decoded"][:6]:
                print("         +0x{:<3X} {}".format(insn["rva"] - m["rva"], insn["text"]))
