#!/usr/bin/env python3
"""
Trinity menu-route inventory extractor.

Walks src/gui/menu.cpp and produces one row per visible menu control:
    tab | route id | route / Render* entry point | widget | label |
    State binding | menu.cpp line | source state (enabled / commented out)

Route ids and their Render* implementations are taken from the dispatcher in
Menu::Draw (the `strcmp(cur, "<id>")` chain), and the per-tab entry points from
the `switch (ui::CurrentTab())` block, so the mapping is read from the code
rather than hand-maintained.

Usage:
    python menu_inventory.py --repo . --out docs/audits/pe2949-menu-inventory.md
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

WIDGETS = [
    "ToggleFloat", "ToggleInt", "Toggle", "OptionItemWithSubtitle",
    "OptionItemWithBuff", "OptionItem", "Option", "SubmenuEquipItem",
    "SubmenuItem", "Submenu", "IntOption", "IntAction", "FloatOption",
    "Combo", "Search", "TextInput", "SwatchRow", "Header",
]

TAB_ENTRY = {
    "TabPlayer": "RenderPlayer",
    "TabInventory": "RenderInventoryHome",
    "TabTravel": "RenderTravel",
    "TabWorld": "RenderWorld",
    "TabSystem": "RenderSystem",
}


def strip_block_comments(text: str) -> str:
    """Blank out /* ... */ regions but keep every byte offset and newline."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            while i < n and not text.startswith("*/", i):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for _ in range(2):
                if i < n:
                    if text[i] != "\n":
                        out[i] = " "
                    i += 1
        else:
            i += 1
    return "".join(out)


def strip_line_comments(text: str) -> str:
    out = list(text)
    for m in re.finditer(r"//[^\n]*", text):
        for i in range(m.start(), m.end()):
            out[i] = " "
    return "".join(out)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def find_functions(src: str) -> list[tuple[str, int, int]]:
    """(name, start_offset, end_offset) for every `static void RenderX()` body."""
    funcs = []
    for m in re.finditer(r"^\s*(?:static\s+)?void\s+(Render\w+)\s*\([^)]*\)\s*\{", src, re.M):
        name = m.group(1)
        depth = 0
        i = m.end() - 1
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        funcs.append((name, m.start(), i))
    return funcs


def extract_call(src: str, open_paren: int) -> str:
    """Text of a call starting at its '(' with balanced parentheses."""
    depth = 0
    i = open_paren
    while i < len(src):
        c = src[i]
        if c == '"':
            i += 1
            while i < len(src) and src[i] != '"':
                if src[i] == "\\":
                    i += 1
                i += 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return src[open_paren:i + 1]
        i += 1
    return src[open_paren:]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--out", default="docs/audits/pe2949-menu-inventory.md")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    raw = (repo / "src/gui/menu.cpp").read_text(encoding="utf-8", errors="replace")
    # live source: block + line comments removed (so commented-out controls vanish)
    live = strip_line_comments(strip_block_comments(raw))

    funcs = find_functions(live)
    route_of_func = {}
    for m in re.finditer(r'strcmp\(cur,\s*"([^"]+)"\)\)\s*(Render\w+)\(\)', live):
        route_of_func.setdefault(m.group(2), m.group(1))
    for tab, fn in TAB_ENTRY.items():
        route_of_func.setdefault(fn, f"<{tab} root>")

    widget_alt = "|".join(WIDGETS)
    call_re = re.compile(r"ui::(" + widget_alt + r")\s*\(")
    label_re = re.compile(r'(?:LOC|")\s*\(?\s*"((?:[^"\\]|\\.)*)"')

    rows = []
    for name, start, end in funcs:
        body = live[start:end]
        for m in call_re.finditer(body):
            widget = m.group(1)
            call = extract_call(body, m.end() - 1)
            lm = label_re.search(call)
            label = lm.group(1) if lm else ""
            fields = sorted(set(re.findall(r"\bst\.(\w+)", call)))
            rows.append({
                "route": route_of_func.get(name, "?"),
                "func": name,
                "widget": widget,
                "label": label,
                "fields": fields,
                "line": line_of(live, start + m.start()),
            })

    # controls that exist only inside a block comment (e.g. No Clip)
    commented = []
    comment_re = re.compile(r"/\*.*?\*/", re.S)
    for m in comment_re.finditer(raw):
        for cm in call_re.finditer(m.group(0)):
            call = extract_call(m.group(0), cm.end() - 1)
            lm = label_re.search(call)
            commented.append({
                "label": lm.group(1) if lm else "",
                "widget": cm.group(1),
                "line": line_of(raw, m.start() + cm.start()),
            })

    # --- tab assignment ------------------------------------------------
    # Route ids come from the dispatcher (`strcmp(cur, "<id>") RenderX();`).
    # A route's tab is the tab of the function that OPENS it with ui::Submenu,
    # so the mapping is solved as a small fixpoint rather than hard-coded.
    func_bodies = {n: live[s:e] for n, s, e in funcs}
    route_fn = {m.group(1): m.group(2)
                for m in re.finditer(r'strcmp\(cur,\s*"([^"]+)"\)\)\s*(Render\w+)\(\)', live)}

    route_re = re.compile(r"^[a-z][a-z0-9_]*$")
    opens: dict[str, list[str]] = {}
    for name, body in func_bodies.items():
        ids = []
        for cm in re.finditer(r"ui::Submenu(?:Item|EquipItem)?\s*\(", body):
            call = extract_call(body, cm.end() - 1)
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', call):
                # The route id is whichever string literal the dispatcher
                # actually knows about (Submenu puts it 2nd, SubmenuItem 3rd).
                if lit in route_fn or (route_re.match(lit) and not ids.count(lit)):
                    if lit in route_fn:
                        ids.append(lit)
                        break
        opens[name] = ids

    tab_of_func = {fn: tab.replace("Tab", "").upper() for tab, fn in TAB_ENTRY.items()}
    route_tab: dict[str, str] = {}
    changed = True
    while changed:
        changed = False
        for fn, ids in opens.items():
            if fn in tab_of_func:
                for rid in ids:
                    if rid not in route_tab:
                        route_tab[rid] = tab_of_func[fn]
                        changed = True
        for rid, fn in route_fn.items():
            if rid in route_tab and fn not in tab_of_func:
                tab_of_func[fn] = route_tab[rid]
                changed = True
        # A routed page often delegates to a shared renderer (e.g.
        # RenderInventoryRestore -> RenderRestoreCategoryPage); those helpers
        # belong to the same tab as their caller.
        for fn, body in func_bodies.items():
            if fn not in tab_of_func:
                continue
            for callee in re.findall(r"\b(Render\w+)\s*\(", body):
                if callee in func_bodies and callee not in tab_of_func:
                    tab_of_func[callee] = tab_of_func[fn]
                    changed = True

    def tab_for(row) -> str:
        return tab_of_func.get(row["func"], route_tab.get(row["route"], "?"))

    lines = ["# PE 2949 menu-route inventory", "",
             f"Source: `src/gui/menu.cpp` ({len(funcs)} `Render*` entry points).", "",
             f"Visible controls: **{len(rows)}**. Controls present only inside a "
             f"block comment: **{len(commented)}**.", "",
             "| # | tab | route id | entry point | widget | label | State binding | menu.cpp |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for i, r in enumerate(sorted(rows, key=lambda x: x["line"]), 1):
        tab = tab_for(r)
        fields = ", ".join(f"`st.{f}`" for f in r["fields"]) or "-"
        lines.append(f"| {i} | {tab} | `{r['route']}` | `{r['func']}` | {r['widget']} | "
                     f"{r['label']} | {fields} | {r['line']} |")

    if commented:
        lines += ["", "## Controls compiled out of the menu", "",
                  "| label | widget | menu.cpp | note |",
                  "| --- | --- | --- | --- |"]
        for c in commented:
            lines.append(f"| {c['label']} | {c['widget']} | {c['line']} | "
                         f"inside a block comment - not reachable by the player |")

    out = repo / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(rows)} visible controls, {len(commented)} commented-out -> {out}")
    for c in commented:
        print(f"  commented out: {c['widget']} '{c['label']}' @ line {c['line']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
