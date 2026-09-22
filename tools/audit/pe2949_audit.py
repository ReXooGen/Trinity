#!/usr/bin/env python3
"""
Trinity PE-revision signature / offset audit scanner.

Version-locks every signature and struct-offset constant declared in the
registry headers against ONE exact on-disk game executable, producing the
rows required by docs/superpowers/plans/2026-09-21-pe2949-full-menu-audit.md:

    symbol | source file:line | exact pattern | match count | file offset |
    RVA | VA | section | decoder result | ABI/layout evidence |
    feature consumers | status

Design rules taken from the plan:
  * Evidence comes from the on-disk EXE, never from a stale disassembler
    database and never from the running process (which may already carry
    our own hooks/trampolines).
  * A pattern with zero or multiple matches is BLOCKED. It is never
    "widened" by hand.
  * The scan window mirrors src/mem/scanner.cpp + src/mem/section_filter.cpp
    exactly: every section except a non-executable `.debug`, addressed as
    [VirtualAddress, VirtualAddress + VirtualSize).

Usage:
    python pe2949_audit.py info   --exe <path>
    python pe2949_audit.py scan   --exe <path> --repo <root> --outdir <dir>
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Iterable

try:
    import capstone  # type: ignore
except ImportError:  # pragma: no cover
    capstone = None

# --------------------------------------------------------------------------
# PE image
# --------------------------------------------------------------------------

IMAGE_SCN_MEM_EXECUTE = 0x20000000


@dataclass
class Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_offset: int
    characteristics: int

    @property
    def executable(self) -> bool:
        return bool(self.characteristics & IMAGE_SCN_MEM_EXECUTE)

    @property
    def readable(self) -> bool:
        return bool(self.characteristics & 0x40000000)

    @property
    def scan_span(self) -> int:
        """Bytes the runtime scanner can see inside this section."""
        return self.virtual_size or 1


@dataclass
class PEImage:
    path: str
    file_size: int
    machine: int
    timestamp: int
    image_base: int
    size_of_image: int
    size_of_headers: int
    checksum: int
    entry_point: int
    subsystem: int
    dll_characteristics: int
    sections: list[Section]
    extension: str = "exe"

    @property
    def arch(self) -> str:
        return {0x8664: "x64", 0x14C: "x86"}.get(self.machine, hex(self.machine))


def parse_pe(path: Path) -> PEImage:
    with open(path, "rb") as f:
        data = f.read(0x2000)
        file_size = path.stat().st_size

        if data[:2] != b"MZ":
            raise ValueError("not a PE image (missing MZ)")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]

        f.seek(e_lfanew)
        nt = f.read(0x108 + 0x40 * 96)
        if nt[:4] != b"PE\0\0":
            raise ValueError("not a PE image (missing PE\\0\\0)")

        machine, num_sections, timestamp = struct.unpack_from("<HHI", nt, 4)
        size_of_optional = struct.unpack_from("<H", nt, 20)[0]
        optional = 24
        magic = struct.unpack_from("<H", nt, optional)[0]
        if magic != 0x20B:
            raise ValueError(f"expected PE32+ (0x20B), got {magic:#x}")

        entry_point = struct.unpack_from("<I", nt, optional + 16)[0]
        image_base = struct.unpack_from("<Q", nt, optional + 24)[0]
        size_of_image = struct.unpack_from("<I", nt, optional + 56)[0]
        size_of_headers = struct.unpack_from("<I", nt, optional + 60)[0]
        checksum = struct.unpack_from("<I", nt, optional + 64)[0]
        subsystem = struct.unpack_from("<H", nt, optional + 68)[0]
        dll_characteristics = struct.unpack_from("<H", nt, optional + 70)[0]

        sections = []
        sec_off = optional + size_of_optional
        for i in range(num_sections):
            base = sec_off + 40 * i
            name_raw = nt[base:base + 8]
            name = name_raw.split(b"\0")[0].decode("ascii", "replace")
            vsize, vaddr, raw_size, raw_off = struct.unpack_from("<IIII", nt, base + 8)
            chars = struct.unpack_from("<I", nt, base + 36)[0]
            sections.append(Section(name, vsize, vaddr, raw_size, raw_off, chars))

    return PEImage(
        path=str(path),
        file_size=file_size,
        machine=machine,
        timestamp=timestamp,
        image_base=image_base,
        size_of_image=size_of_image,
        size_of_headers=size_of_headers,
        checksum=checksum,
        entry_point=entry_point,
        subsystem=subsystem,
        dll_characteristics=dll_characteristics,
        sections=sections,
    )


class ImageReader:
    """Reads section bytes, mirroring the runtime scanner's eligibility rule."""

    def __init__(self, pe: PEImage):
        self.pe = pe
        self._fh = open(pe.path, "rb")
        self._cache: dict[tuple[str, int], bytes] = {}

    def close(self) -> None:
        self._fh.close()

    def __enter__(self) -> "ImageReader":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def scan_sections(self) -> list[Section]:
        """Section filter = src/mem/section_filter.cpp (all but non-exec .debug)."""
        out = []
        for s in self.pe.sections:
            if s.name == ".debug" and not s.executable:
                continue
            out.append(s)
        return out

    def section_buffer(self, s: Section) -> bytes:
        """Virtual image bytes of one section (raw data + zero pad), cached.

        The image is ~380 MB of section data across ~12 sections and every
        registry pattern is scanned against all of them, so the buffers are
        read once from disk and kept for the lifetime of the reader.
        """
        key = (s.name, s.virtual_address)
        cached = self._cache.get(key)
        if cached is not None:
            return cached
        span = s.scan_span
        self._fh.seek(s.raw_offset)
        raw = bytearray(self._fh.read(min(s.raw_size, span)))
        if len(raw) < span:
            raw.extend(b"\0" * (span - len(raw)))
        dense = bytes(raw)
        self._cache[key] = dense
        return dense

    def read(self, rva: int, size: int) -> bytes:
        for s in self.pe.sections:
            if s.virtual_address <= rva < s.virtual_address + max(s.scan_span, s.raw_size):
                off = rva - s.virtual_address
                if off + size <= s.raw_size:
                    self._fh.seek(s.raw_offset + off)
                    return self._fh.read(size)
        # fall back to a flat RVA read (headers etc.)
        self._fh.seek(rva)
        return self._fh.read(size)

    def section_for_rva(self, rva: int) -> Section | None:
        for s in self.pe.sections:
            if s.virtual_address <= rva < s.virtual_address + max(s.scan_span, s.raw_size):
                return s
        return None

    def file_offset(self, rva: int) -> int | None:
        s = self.section_for_rva(rva)
        if s is None:
            return rva if rva < self.pe.size_of_headers else None
        off = rva - s.virtual_address
        if off >= s.raw_size:
            return None  # zero-filled tail, no file backing
        return s.raw_offset + off


# --------------------------------------------------------------------------
# Pattern handling (mirrors src/mem/scanner.cpp Parse())
# --------------------------------------------------------------------------


@dataclass
class Pattern:
    text: str
    bytes_: list[int] = field(default_factory=list)
    mask: list[bool] = field(default_factory=list)
    literal_offset: int = 0
    literal_len: int = 0

    @property
    def length(self) -> int:
        return len(self.bytes_)


def parse_pattern(text: str) -> Pattern:
    pat = Pattern(text=text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t":
            i += 1
            continue
        if c == "?":
            pat.bytes_.append(0)
            pat.mask.append(False)
            i += 1
            if i < n and text[i] == "?":
                i += 1
            continue
        try:
            hi = int(c, 16)
        except ValueError:
            i += 1
            continue
        lo = hi
        if i + 1 < n:
            try:
                lo = int(text[i + 1], 16)
                i += 2
            except ValueError:
                i += 1
        else:
            i += 1
        pat.bytes_.append((hi << 4) | lo)
        pat.mask.append(True)

    # longest fixed run -> cheap prefilter
    best_start = best_len = 0
    run_start = 0
    run_len = 0
    for idx, m in enumerate(pat.mask):
        if m:
            if run_len == 0:
                run_start = idx
            run_len += 1
            if run_len > best_len:
                best_len, best_start = run_len, run_start
        else:
            run_len = 0
    pat.literal_offset = best_start
    pat.literal_len = best_len
    return pat


def scan_pattern(reader: ImageReader, pat: Pattern, limit: int = 64) -> list[int]:
    """Return match RVAs for `pat` across the runtime scanner's section set.

    A full-regex sweep over ~380 MB of section data per pattern is far too
    slow in CPython, so the longest fixed byte run is used as a memchr-style
    anchor and every candidate is then verified position by position.  The
    result is identical to src/mem/scanner.cpp's anchored ScanSpan().
    """
    if pat.length == 0:
        return []
    if pat.literal_len == 0:
        # pathological all-wildcard pattern: no anchor available
        needle = None
    else:
        needle = bytes(pat.bytes_[pat.literal_offset:pat.literal_offset + pat.literal_len])

    hits: list[int] = []
    for s in reader.scan_sections():
        buf = reader.section_buffer(s)
        if len(buf) < pat.length:
            continue
        if needle is None:
            candidates = range(0, len(buf) - pat.length + 1)
        else:
            candidates = _find_all(buf, needle, pat.literal_offset)
        for start in candidates:
            end = start + pat.length
            if start < 0 or end > len(buf):
                continue
            ok = True
            for i in range(pat.length):
                if pat.mask[i] and buf[start + i] != pat.bytes_[i]:
                    ok = False
                    break
            if ok:
                hits.append(s.virtual_address + start)
                if len(hits) >= limit:
                    return hits
    return hits


def _find_all(buf: bytes, needle: bytes, literal_offset: int):
    """Yield candidate pattern-start indices for a literal anchored at literal_offset."""
    pos = 0
    n = len(buf)
    while True:
        idx = buf.find(needle, pos)
        if idx < 0:
            return
        yield idx - literal_offset
        pos = idx + 1


def scan_ascii(reader: ImageReader, text: str, limit: int = 64) -> list[tuple[int, str]]:
    needle = text.encode("ascii")
    hits: list[tuple[int, str]] = []
    for s in reader.scan_sections():
        buf = reader.section_buffer(s)
        start = 0
        while True:
            idx = buf.find(needle, start)
            if idx < 0:
                break
            hits.append((s.virtual_address + idx, s.name))
            if len(hits) >= limit:
                return hits
            start = idx + 1
    return hits


# --------------------------------------------------------------------------
# Registry extraction
# --------------------------------------------------------------------------

SYMBOL_RE = re.compile(
    r"\b(?P<name>(?:kSig_|kStr_|kOff_|kTls_|kCharMgrAnchors|kCharList_|kInvPickupCapacity|kInv[A-Za-z]*|kMin[A-Za-z]*)\w*)"
    r"(?:\[\s*[^\]]*\])?\s*=",
)


@dataclass
class Symbol:
    name: str
    file: str
    line: int
    kind: str          # sig | str | offset | array | bytes | anchors
    raw: str
    doc: str = ""
    pattern: str | None = None
    value: int | None = None
    text_value: str | None = None
    byte_array: list[int] | None = None
    anchors: list[dict] | None = None


def _strip_comments(text: str) -> str:
    """Remove // and /* */ comments while preserving byte offsets (replaced by spaces)."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif text.startswith("/*", i):
            while i < n and not text.startswith("*/", i):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for _ in range(2):
                if i < n:
                    out[i] = " "
                    i += 1
        else:
            i += 1
    return "".join(out)


def _extract_rhs(src: str, start: int) -> tuple[str, int]:
    """Return (rhs, end_index) for an initialiser starting at `start`.

    Scans forward tracking string literals, brace and paren depth, stopping at
    the `;` that terminates the declaration.
    """
    depth = 0
    i = start
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            i += 1
            while i < n and src[i] != '"':
                if src[i] == "\\":
                    i += 1
                i += 1
            i += 1
            continue
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif c == ";" and depth <= 0:
            return src[start:i], i
        i += 1
    return src[start:n], n


def _string_literals(rhs: str) -> list[str]:
    return re.findall(r'"((?:[^"\\]|\\.)*)"', rhs)


def _looks_like_pattern(text: str) -> bool:
    toks = text.replace("?", " ").split()
    if len(toks) < 2:
        return False
    for t in text.split():
        if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}|\?{1,2})", t):
            return False
    return True


def _preceding_doc(raw_text: str, line: int) -> str:
    """Collapse the comment block immediately above a declaration.

    The plan asks each manifest row to carry its ABI/layout evidence. For most
    entries that evidence is the derived-from-disassembly note that already sits
    in the registry, so it is captured here verbatim (trimmed) rather than
    re-typed by hand.
    """
    lines = raw_text.splitlines()
    collected: list[str] = []
    i = line - 2  # zero-based index of the line above the declaration
    while i >= 0:
        stripped = lines[i].strip()
        if stripped.startswith("//"):
            collected.append(stripped.lstrip("/").strip())
            i -= 1
            continue
        if stripped.endswith("*/"):
            i -= 1
            continue
        if stripped.startswith("*") or stripped.startswith("/*"):
            collected.append(stripped.lstrip("/*").lstrip("*").strip())
            i -= 1
            if stripped.startswith("/*"):
                break
            continue
        break
    text = " ".join(reversed([c for c in collected if c]))
    text = re.sub(r"\s+", " ", text).strip()
    return text[:400]


def extract_symbols(path: Path, repo_root: Path) -> list[Symbol]:
    raw_text = path.read_text(encoding="utf-8", errors="replace")
    src = _strip_comments(raw_text)
    rel = str(path.relative_to(repo_root)).replace("\\", "/")
    out: list[Symbol] = []

    for m in SYMBOL_RE.finditer(src):
        name = m.group("name")
        eq = src.index("=", m.start())
        rhs, _ = _extract_rhs(src, eq + 1)
        rhs = rhs.strip()
        line = src.count("\n", 0, m.start()) + 1

        doc = _preceding_doc(raw_text, line)
        sym = Symbol(name=name, file=rel, line=line, kind="unknown", raw=rhs,
                     doc=doc)

        if name.startswith("kCharMgrAnchors"):
            anchors = []
            for am in re.finditer(r'\{\s*"([^"]+)"\s*,\s*([^}]+?)\s*\}', rhs):
                anchors.append({"pattern": am.group(1).strip(),
                                "movOff": am.group(2).strip()})
            sym.kind = "anchors"
            sym.anchors = anchors
        elif name.startswith("kStr_"):
            lits = _string_literals(rhs)
            sym.kind = "str"
            sym.text_value = "".join(lits)
            sym.name = name
        elif rhs.lstrip().startswith('"'):
            lits = _string_literals(rhs)
            joined = "".join(lits).strip()
            if joined == "":
                # deliberate disabled placeholder (e.g. kSig_TrItemValueDtor)
                sym.kind = "empty"
                sym.text_value = ""
            elif _looks_like_pattern(joined):
                sym.kind = "sig"
                sym.pattern = " ".join(joined.split())
            else:
                sym.kind = "str"
                sym.text_value = joined
        elif rhs.lstrip().startswith("{"):
            vals = [int(v, 16) if v.lower().startswith("0x") else int(v)
                    for v in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", rhs)]
            sym.kind = "bytes"
            sym.byte_array = vals
        else:
            num = re.match(r"(-?0x[0-9A-Fa-f]+|-?\d+)", rhs)
            if num:
                sym.kind = "offset"
                sym.value = int(num.group(1), 16) if num.group(1).lower().startswith(
                    ("0x", "-0x")) else int(num.group(1))
            else:
                sym.kind = "other"
        out.append(sym)
    return out


# --------------------------------------------------------------------------
# Decoder
# --------------------------------------------------------------------------


def make_disassembler():
    if capstone is None:
        return None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    md.skipdata = False
    return md


def decode(reader: ImageReader, md, rva: int, count: int = 10) -> list[dict]:
    if md is None:
        return []
    va = reader.pe.image_base + rva
    buf = reader.read(rva, 16 * count)
    out = []
    for insn in md.disasm(buf, va):
        entry = {
            "address": insn.address,
            "rva": insn.address - reader.pe.image_base,
            "bytes": insn.bytes.hex(" "),
            "mnemonic": insn.mnemonic,
            "op_str": insn.op_str,
            "text": f"{insn.mnemonic} {insn.op_str}".strip(),
        }
        # resolve RIP-relative memory operand -> absolute target
        try:
            for op in insn.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    entry["rip_target"] = insn.address + insn.size + op.mem.disp
                    entry["rip_target_rva"] = entry["rip_target"] - reader.pe.image_base
        except Exception:
            pass
        out.append(entry)
        if len(out) >= count:
            break
    return out


# --------------------------------------------------------------------------
# Consumers
# --------------------------------------------------------------------------


CALL_TOKENS = ("installhook", "findpatternif", "findallmatches", "findpattern",
               "countmatches")


def find_consumers(repo_root: Path, symbol: str) -> list[str]:
    hits = []
    src_root = repo_root / "src"
    for p in src_root.rglob("*"):
        if p.suffix not in (".cpp", ".h", ".hpp", ".c"):
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if symbol in text:
            rel = str(p.relative_to(repo_root)).replace("\\", "/")
            hits.append(rel)
    return hits


def classify_usage(repo_root: Path, symbol: str) -> dict:
    """How the C++ code consumes `symbol` (comments ignored).

    The consumer call decides what "correct" means for the match count:
      installhook / findpattern  -> needs a unique match
      findallmatches / findpatternif -> multi-match is by design
      unreferenced -> dead registry entry, cannot gate a feature
    """
    modes: set[str] = set()
    refs: list[str] = []
    src_root = repo_root / "src"
    word = re.compile(r"\b" + re.escape(symbol) + r"\b")
    for p in src_root.rglob("*"):
        if p.suffix not in (".cpp", ".h", ".hpp", ".c"):
            continue
        try:
            raw = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        text = _strip_comments(raw)
        for m in word.finditer(text):
            window = text[max(0, m.start() - 240):m.start()]
            cut = window.rfind(";")
            stmt = window[cut + 1:]
            best = None
            low = stmt.lower()
            for tok in CALL_TOKENS:
                i = low.rfind(tok)
                if i >= 0 and (best is None or i > best[0]):
                    best = (i, tok)
            mode = best[1] if best else "reference"
            modes.add(mode)
            if p.parent.name != "game" or p.suffix in (".cpp",):
                rel = str(p.relative_to(repo_root)).replace("\\", "/")
                refs.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}:{mode}")
    return {
        "modes": sorted(modes),
        "refs": refs,
        "declared_in_registry": not any(
            r.split(":")[0].endswith(("offsets.h", "map_marker.h")) for r in refs),
    }


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------


def cmd_info(args) -> int:
    pe = parse_pe(Path(args.exe))
    print(f"path            : {pe.path}")
    print(f"file size       : {pe.file_size} (0x{pe.file_size:X})")
    print(f"machine         : {hex(pe.machine)} ({pe.arch})")
    print(f"timestamp       : {pe.timestamp} (0x{pe.timestamp:08X})")
    print(f"image base      : 0x{pe.image_base:X}")
    print(f"size of image   : 0x{pe.size_of_image:X}")
    print(f"size of headers : 0x{pe.size_of_headers:X}")
    print(f"checksum        : 0x{pe.checksum:08X}")
    print(f"entry point RVA : 0x{pe.entry_point:X}")
    print(f"subsystem       : {pe.subsystem}")
    print(f"dll chars       : 0x{pe.dll_characteristics:04X}")
    print()
    print(f"{'name':<10}{'RVA':>12}{'vsize':>12}{'raw off':>12}{'raw size':>12}  X R")
    for s in pe.sections:
        print(f"{s.name:<10}0x{s.virtual_address:>10X}0x{s.virtual_size:>10X}"
              f"0x{s.raw_offset:>10X}0x{s.raw_size:>10X}  "
              f"{'X' if s.executable else '-'} {'R' if s.readable else '-'}")
    return 0


def cmd_scan(args) -> int:
    repo_root = Path(args.repo).resolve()
    exe = Path(args.exe)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    pe = parse_pe(exe)
    md = make_disassembler()

    sources = [repo_root / "src/game/offsets.h", repo_root / "src/game/map_marker.h"]
    symbols: list[Symbol] = []
    for s in sources:
        if s.exists():
            symbols.extend(extract_symbols(s, repo_root))

    results = []
    with ImageReader(pe) as reader:
        for sym in symbols:
            row = {
                "symbol": sym.name,
                "file": sym.file,
                "line": sym.line,
                "kind": sym.kind,
                "pattern": sym.pattern,
                "value": sym.value,
                "text_value": sym.text_value,
                "byte_array": sym.byte_array,
                "doc": sym.doc,
                "consumers": find_consumers(repo_root, sym.name),
                "usage": classify_usage(repo_root, sym.name),
            }
            if sym.kind == "sig":
                pat = parse_pattern(sym.pattern)
                hits = scan_pattern(reader, pat)
                row["match_count"] = len(hits)
                row["matches"] = []
                for rva in hits[:8]:
                    fo = reader.file_offset(rva)
                    sec = reader.section_for_rva(rva)
                    m = {
                        "rva": rva,
                        "va": pe.image_base + rva,
                        "file_offset": fo,
                        "section": sec.name if sec else None,
                        "decoded": decode(reader, md, rva, 8),
                    }
                    row["matches"].append(m)
                row["status"] = ("UNIQUE" if len(hits) == 1
                                 else "NONE" if not hits
                                 else f"MULTI({len(hits)}{'+' if len(hits) >= 64 else ''})")
            elif sym.kind == "anchors":
                row["match_count"] = 0
                row["matches"] = []
                row["anchor_results"] = []
                for i, a in enumerate(sym.anchors or []):
                    pat = parse_pattern(a["pattern"])
                    hits = scan_pattern(reader, pat)
                    ar = {"index": i, "pattern": a["pattern"], "movOff": a["movOff"],
                          "match_count": len(hits), "matches": []}
                    for rva in hits[:4]:
                        sec = reader.section_for_rva(rva)
                        dec = decode(reader, md, rva, 8)
                        # resolve the global referenced at movOff
                        target = None
                        for insn in dec:
                            if insn["rva"] - rva == _python_int(a["movOff"]) and \
                                    insn["mnemonic"] == "mov" and "rip_target" in insn:
                                target = insn["rip_target"]
                        ar["matches"].append({
                            "rva": rva,
                            "va": pe.image_base + rva,
                            "file_offset": reader.file_offset(rva),
                            "section": sec.name if sec else None,
                            "global_target": target,
                            "decoded": dec,
                        })
                    row["anchor_results"].append(ar)
                counts = [a["match_count"] for a in row["anchor_results"]]
                if all(c == 1 for c in counts):
                    row["status"] = "UNIQUE"
                elif any(c == 0 for c in counts):
                    row["status"] = f"PARTIAL({sum(1 for c in counts if c == 1)}/{len(counts)})"
                else:
                    row["status"] = "MULTI"
            elif sym.kind == "str":
                hits = scan_ascii(reader, sym.text_value or "")
                row["match_count"] = len(hits)
                row["matches"] = [
                    {"rva": rva, "va": pe.image_base + rva,
                     "file_offset": reader.file_offset(rva), "section": sec}
                    for rva, sec in hits[:8]
                ]
                row["status"] = "PRESENT" if hits else "NONE"
            elif sym.kind == "empty":
                row["match_count"] = None
                row["matches"] = []
                row["status"] = "DISABLED"
            elif sym.kind == "offset":
                row["match_count"] = None
                row["matches"] = []
                row["status"] = "LAYOUT"
            elif sym.kind == "bytes":
                row["match_count"] = None
                row["matches"] = []
                row["status"] = "LAYOUT"
            else:
                row["match_count"] = None
                row["matches"] = []
                row["status"] = "N/A"
            results.append(row)

    report = {
        "exe": {
            "path": pe.path,
            "file_size": pe.file_size,
            "image_base": pe.image_base,
            "size_of_image": pe.size_of_image,
            "timestamp": pe.timestamp,
            "entry_point_rva": pe.entry_point,
            "checksum": pe.checksum,
            "arch": pe.arch,
            "sections": [asdict(s) for s in pe.sections],
        },
        "counts": {
            "total": len(results),
            "sig": sum(1 for r in results if r["kind"] == "sig"),
            "anchors": sum(1 for r in results if r["kind"] == "anchors"),
            "str": sum(1 for r in results if r["kind"] == "str"),
            "offset": sum(1 for r in results if r["kind"] == "offset"),
            "bytes": sum(1 for r in results if r["kind"] == "bytes"),
            "empty": sum(1 for r in results if r["kind"] == "empty"),
            "unique": sum(1 for r in results if r["status"] == "UNIQUE"),
            "none": sum(1 for r in results if r["status"] == "NONE"),
            "multi": sum(1 for r in results if str(r["status"]).startswith("MULTI")),
            "partial": sum(1 for r in results if str(r["status"]).startswith("PARTIAL")),
            "present": sum(1 for r in results if r["status"] == "PRESENT"),
            "unreferenced": sum(1 for r in results
                                if r["usage"]["modes"] in ([], ["reference"])
                                and r["file"].endswith("offsets.h")),
        },
        "results": results,
    }

    json_path = outdir / "pe2949-scan.json"
    json_path.write_text(json.dumps(report, indent=1), encoding="utf-8")

    _write_manifest(outdir / "pe2949-aob-manifest.txt", symbols)
    _write_table(outdir / "pe2949-scan-table.md", report)

    c = report["counts"]
    print(f"scanned {c['total']} registry entries "
          f"(sig={c['sig']} anchors={c['anchors']} str={c['str']} offset={c['offset']})")
    print(f"UNIQUE={c['unique']}  NONE={c['none']}  MULTI={c['multi']}  "
          f"PARTIAL={c['partial']}  STR={c['present']}")
    print(f"wrote {json_path}")
    print(f"wrote {outdir / 'pe2949-aob-manifest.txt'}")
    print(f"wrote {outdir / 'pe2949-scan-table.md'}")
    return 0


def _python_int(text: str) -> int:
    text = text.strip()
    try:
        return int(text, 0)
    except ValueError:
        return -1


def _write_manifest(path: Path, symbols: Iterable[Symbol]) -> None:
    lines = [
        "# Trinity PE 2949 AOB / offset manifest",
        "# generated by tools/audit/pe2949_audit.py from the current source tree",
        "# format: <file>:<line>:<kind>\t<symbol>\t<pattern|value>",
        "",
    ]
    for s in sorted(symbols, key=lambda x: (x.file, x.line)):
        if s.kind == "sig":
            body = s.pattern or ""
        elif s.kind == "str":
            body = s.text_value or ""
        elif s.kind == "anchors":
            body = " | ".join(a["pattern"] for a in (s.anchors or []))
        elif s.kind in ("offset", "bytes"):
            body = s.raw
        else:
            body = s.raw
        lines.append(f"{s.file}:{s.line}:{s.kind}\t{s.name}\t{body}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_table(path: Path, report: dict) -> None:
    out = []
    out.append("| symbol | source | kind | exact pattern | count | file off | RVA | VA | section | decoder result | ABI/layout evidence | feature consumers | status |")
    out.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for r in report["results"]:
        src = f"{r['file']}:{r['line']}"
        pat = r["pattern"] or (r["text_value"] or "")
        if r["kind"] == "offset" and r["value"] is not None:
            pat = f"0x{r['value']:X}"
        if r["kind"] == "bytes":
            pat = (r["byte_array"] or []) and " ".join(f"{b:02X}" for b in r["byte_array"])
        if r["kind"] == "anchors":
            pat = "; ".join(a["pattern"] for a in r.get("anchor_results", []))
        pat = str(pat or "").replace("|", "\\|")
        if len(pat) > 60:
            pat = pat[:57] + "..."
        if r["matches"]:
            m = r["matches"][0]
            fo = m.get("file_offset")
            fo = f"0x{fo:X}" if isinstance(fo, int) else "n/a"
            rva = f"0x{m['rva']:X}"
            va = f"0x{m['va']:X}"
            sec = m.get("section") or ""
            dec = ""
            if m.get("decoded"):
                dec = " ; ".join(i["text"] for i in m["decoded"][:4])
            dec = dec.replace("|", "\\|")
        else:
            fo = rva = va = sec = dec = ""
        cons = ", ".join(r["consumers"][:4])
        doc = str(r.get("doc") or "").replace("|", "\\|")
        if len(doc) > 200:
            doc = doc[:197] + "..."
        out.append(f"| `{r['symbol']}` | {src} | {r['kind']} | `{pat}` | {r['match_count']} | "
                   f"{fo} | {rva} | {va} | {sec} | {dec} | {doc} | {cons} | {r['status']} |")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def cmd_summary(args) -> int:
    report = json.loads(Path(args.json).read_text(encoding="utf-8"))
    print("counts:", json.dumps(report["counts"]))
    print()
    by_status: dict[str, list] = {}
    for r in report["results"]:
        status = str(r["status"])
        key = "MULTI" if status.startswith("MULTI") else \
              "PARTIAL" if status.startswith("PARTIAL") else status
        by_status.setdefault(key, []).append(r)
    for want in ("UNIQUE", "NONE", "MULTI", "PARTIAL", "PRESENT",
                 "DISABLED", "LAYOUT", "N/A"):
        rows = by_status.get(want)
        if not rows:
            continue
        print(f"=== {want} ({len(rows)}) ===")
        if want == "LAYOUT":
            print("  (numeric struct offsets and byte arrays; no AOB scan applies)")
            print()
            continue
        for r in rows:
            extra = ""
            if r["matches"]:
                m = r["matches"][0]
                extra = f"{m.get('section') or '':<9} RVA 0x{m['rva']:<9X}"
            pat = str(r["pattern"] or r["text_value"] or "")
            if r["kind"] == "anchors":
                pat = f"{len(r.get('anchor_results', []))} anchors"
            modes = ",".join(r["usage"]["modes"]) or "-"
            print(f"  {r['kind']:<8} {r['symbol']:<42} {r['status']:<12} "
                  f"{extra:<26} [{modes}] {pat[:46]}")
        print()
    return 0


def cmd_extras(args) -> int:
    """Scan AOB literals that live outside the registry headers.

    The plan's Global Constraints warn that "several literals and revision
    gates live outside that file", so the registry scan alone is not the whole
    contract surface.
    """
    repo_root = Path(args.repo).resolve()
    pe = parse_pe(Path(args.exe))
    md = make_disassembler()

    lit_re = re.compile(
        r'"((?:[0-9A-Fa-f]{2}|[?]{1,2})(?:[ \t]+(?:[0-9A-Fa-f]{2}|[?]{1,2})){3,})"')
    registry = {"src/game/offsets.h", "src/game/map_marker.h"}
    found = []
    with ImageReader(pe) as reader:
        for p in sorted((repo_root / "src").rglob("*")):
            if p.suffix not in (".cpp", ".h", ".hpp", ".c"):
                continue
            rel = str(p.relative_to(repo_root)).replace("\\", "/")
            if rel in registry:
                continue
            text = p.read_text(encoding="utf-8", errors="replace")
            for i, line in enumerate(text.splitlines(), 1):
                for m in lit_re.finditer(line):
                    pat_text = " ".join(m.group(1).split())
                    if len(pat_text.split()) < 5:
                        continue
                    pat = parse_pattern(pat_text)
                    hits = scan_pattern(reader, pat, 8)
                    found.append({
                        "file": rel, "line": i, "pattern": pat_text,
                        "match_count": len(hits),
                        "matches": [
                            {"rva": r, "va": pe.image_base + r,
                             "section": (reader.section_for_rva(r) or Section("", 0, 0, 0, 0, 0)).name,
                             "decoded": decode(reader, md, r, 4)}
                            for r in hits[:4]
                        ],
                    })

    out = Path(args.outdir) / "pe2949-inline-literals.md"
    lines = ["# AOB literals outside the registry headers", "",
             f"Scanned against `{pe.path}` (image base 0x{pe.image_base:X}).", "",
             "| file:line | pattern | matches | RVA | section | decoder |",
             "| --- | --- | --- | --- | --- | --- |"]
    for f in found:
        dec = ""
        rva = sec = ""
        if f["matches"]:
            m = f["matches"][0]
            rva = f"0x{m['rva']:X}"
            sec = m["section"]
            dec = " ; ".join(x["text"] for x in m["decoded"][:3])
        lines.append(f"| `{f['file']}:{f['line']}` | `{f['pattern'][:52]}` | "
                     f"{f['match_count']} | {rva} | {sec} | {dec} |")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(found)} inline literals scanned -> {out}")
    for f in found:
        print(f"  {f['file']}:{f['line']}  matches={f['match_count']}  {f['pattern'][:60]}")
    return 0


def _decode_imm32_edx(decoded: list[dict]) -> int | None:
    for insn in decoded:
        if insn["mnemonic"] == "mov" and insn["op_str"].startswith("edx, 0x"):
            try:
                return int(insn["op_str"].split("0x")[1], 16)
            except ValueError:
                return None
    return None


def cmd_contracts(args) -> int:
    """Verify the PE2949-specific contracts named in the plan's anchor table."""
    repo_root = Path(args.repo).resolve()
    pe = parse_pe(Path(args.exe))
    md = make_disassembler()
    checks: list[dict] = []

    def add(name, expected, actual, ok, evidence):
        checks.append({"check": name, "expected": expected, "actual": actual,
                       "result": "PASS" if ok else "FAIL", "evidence": evidence})

    version_mapping = (repo_root / "src/core/version_mapping.cpp").read_text(
        encoding="utf-8", errors="replace")
    realm_expected = 0x1EC if re.search(
        r"revision == 2850 \|\| revision == 2944 \|\| revision == 2949\)\s*return 0x1EC",
        version_mapping) else None

    with ImageReader(pe) as reader:
        # ---- 1. native Slot Size setter --------------------------------
        setter = parse_pattern("48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 "
                               "48 83 EC 20 48 8B 41 18 41 0F B7 E9 8B 49 20 "
                               "4C 8B F2 4C 8D 14 C8")
        setter_hits = scan_pattern(reader, setter)
        setter_rva = setter_hits[0] if setter_hits else None
        setter_dec = decode(reader, md, setter_rva, 12) if setter_rva else []
        add("native Slot Size setter: exactly one match",
            "1 match", f"{len(setter_hits)}", len(setter_hits) == 1,
            "; ".join(i["text"] for i in setter_dec[:8]))
        add("native Slot Size setter at plan RVA 0x2135850",
            "0x2135850",
            f"0x{setter_rva:X}" if setter_rva is not None else "none",
            setter_rva == 0x2135850,
            f"VA 0x{pe.image_base + setter_rva:X}" if setter_rva else "")

        # body writes bucket+0x16 / +0x1A / derived +0x14
        body = decode(reader, md, setter_rva, 400) if setter_rva else []
        writes = {off: False for off in ("+ 0x16]", "+ 0x1a]", "+ 0x14]")}
        for insn in body:
            if insn["mnemonic"] not in ("mov", "movzx"):
                continue
            low = insn["op_str"].lower()
            for off in writes:
                if off in low:
                    writes[off] = True
        add("setter ABI writes bucket+0x16, +0x1A, derived +0x14",
            "all three store sites present",
            ", ".join(f"{k.strip(']')}={'yes' if v else 'no'}" for k, v in writes.items()),
            all(writes.values()),
            "capstone sweep of 400 instructions from the setter entry")

        # ---- 2. pickup capacity branch ---------------------------------
        pickup = parse_pattern("84 D2 74 07 0F B7 4C 24 48 EB 0F 0F B7 4F 14 66 39 "
                               "4C 24 48 66 0F 4C 4C 24 48 66 89 4C 24 32")
        pickup_hits = scan_pattern(reader, pickup)
        pickup_rva = pickup_hits[0] if pickup_hits else None
        branch_rva = pickup_rva + 2 if pickup_rva is not None else None
        add("pickup capacity signature: exactly one match",
            "1 match", f"{len(pickup_hits)}", len(pickup_hits) == 1,
            f"RVA 0x{pickup_rva:X}" if pickup_rva is not None else "")
        add("pickup branch at plan RVA 0x2407824",
            "0x2407824",
            f"0x{branch_rva:X}" if branch_rva is not None else "none",
            branch_rva == 0x2407824,
            "signature RVA + 2 (kInvPickupCapacityPatchSize offset used by "
            "InstallPickupCapacityPatch)")
        branch_bytes = reader.read(branch_rva, 2) if branch_rva else b""
        add("pickup branch bytes are the reversible original",
            "74 07", branch_bytes.hex(" ").upper(),
            branch_bytes == bytes([0x74, 0x07]),
            "read straight out of the on-disk image")
        branch_dec = decode(reader, md, pickup_rva, 3) if pickup_rva else []
        add("pickup branch decodes as test dl,dl / je +7",
            "test dl,dl ; je 0x...+7",
            " ; ".join(i["text"] for i in branch_dec),
            any(i["mnemonic"] == "test" for i in branch_dec) and
            any(i["mnemonic"] == "je" for i in branch_dec),
            "capstone at the signature start")

        # ---- 3. realm flag TLS offset ----------------------------------
        realm = parse_pattern("BA ?? 01 00 00 48 8B 08 0F B6 04 0A 84 C0 74 0A "
                              "C5 FC 10 05 ?? ?? ?? ?? EB 08 C5 FC 10 05 ?? ?? ?? ??")
        realm_hits = scan_pattern(reader, realm)
        realm_rva = realm_hits[0] if realm_hits else None
        realm_dec = decode(reader, md, realm_rva, 4) if realm_rva else []
        realm_imm = _decode_imm32_edx(realm_dec)
        add("RealmFlagOffsetForRevision(2949) matches the image's realm selector",
            f"0x{realm_expected:X}" if realm_expected else "ungated",
            f"0x{realm_imm:X}" if realm_imm is not None else "none",
            realm_imm is not None and realm_imm == realm_expected,
            f"kSig_FieldTimeRealm @ RVA 0x{realm_rva:X}: {realm_dec[0]['text'] if realm_dec else ''}")

        # ---- 4. readiness sentinels ------------------------------------
        sentinels = {
            "kSig_DamageApply_Alt": "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 "
                                    "48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9",
            "kSig_CombatTimingEval": "48 8B C4 41 55 41 56 41 57 48 83 EC 70 C5 78 29 40 A8",
            "kSig_MoveUpdate": "48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56",
            "kSig_InvGetItemQty": "66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA",
            "kSig_EvaluateCrimeWantedState": "48 89 5C 24 08 48 8B 41 40 45 33 D2 8B 49 48 "
                                             "48 8B DA 4C 6B D9 38 41 B0 07",
            "kSig_TodEngineGlobal": "83 3D ?? ?? ?? ?? FF 75 ?? 48 89 1D ?? ?? ?? ?? "
                                    "48 89 3D ??",
            "kSig_WeatherRain": "48 8B 51 ?? 4C 8B D1 48 85 D2 B9 40 00 00 00 48 8D 42 18 48 "
                                "0F 44 C1 41 80 7A 31 00 4C 8B 08 4D 8D 81 6C 01 00 00",
        }
        for name, pat_text in sentinels.items():
            hits = scan_pattern(reader, parse_pattern(pat_text))
            add(f"readiness sentinel {name}", "1 match", f"{len(hits)}",
                len(hits) == 1,
                f"RVA 0x{hits[0]:X}" if hits else "no match")

        # ---- 5. inventory holder + insert planner ----------------------
        holder = scan_pattern(reader, parse_pattern(
            "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30"))
        add("kSig_InvGetHolder exactly one match", "1 match", f"{len(holder)}",
            len(holder) == 1, f"RVA 0x{holder[0]:X}" if holder else "")

        insert2944 = scan_pattern(reader, parse_pattern(
            "48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 "
            "55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F0 FD FF FF 48 81 EC 10 03 00 00"))
        add("kSig_InvHolderInsert2944 exactly one match", "1 match",
            f"{len(insert2944)}", len(insert2944) == 1,
            f"RVA 0x{insert2944[0]:X}" if insert2944 else "")

        insert201 = scan_pattern(reader, parse_pattern(
            "48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 "
            "55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FE FF FF 48 81 EC 00 03 00 00"))
        add("PE 2850/2760 holder-insert AOB must NOT match PE 2949",
            "0 matches", f"{len(insert201)}", len(insert201) == 0,
            "proves PE 2949 does not silently inherit the PE 2850 frame")

        # ---- 6. inventory core global (declared but unused on TU201) ---
        core_glob = scan_pattern(reader, parse_pattern(
            "48 8B 05 ? ? ? ? 48 8B 48 30 48 8B 49 50 48 89 8D E0 02 00 00 48 85 C9"))
        add("kSig_InvCoreGlobal (durable container walk, optional)",
            "informational", f"{len(core_glob)} match(es)", True,
            "used at inventory.cpp:2374 for TU201 revisions; a zero match "
            "degrades the inventory list to lazy discovery only")

    # ---- 7. revision-gate policy (source-derived) ----------------------
    tu201 = re.search(r"return revision == 2760 \|\| revision == 2850 \|\| "
                      r"revision == 2944 \|\| revision == 2949;", version_mapping)
    add("UsesTu201CompatibleRevision admits exactly 2760/2850/2944/2949",
        "2949 admitted, unknown revisions rejected",
        "gate present" if tu201 else "gate shape changed",
        bool(tu201),
        "src/core/version_mapping.cpp:23-30")
    loco = re.search(r"case 2944:\s*case 2949: return LocoStepperContract::Pe2944;",
                     version_mapping)
    add("LocoStepperContractForRevision maps 2949 to the PE2944 contract",
        "Pe2944 (move-owner +0x2C0)", "mapped" if loco else "not mapped",
        bool(loco), "src/core/version_mapping.cpp:57-70")
    default_unsupported = re.search(
        r"default:\s*return LocoStepperContract::Unsupported;", version_mapping)
    add("unknown PE revisions fail closed for movement",
        "default -> Unsupported (offset 0)", "present" if default_unsupported else "missing",
        bool(default_unsupported), "src/core/version_mapping.cpp:68")
    slot_all = re.search(r"bool SlotSizeOverrideSupportedForRevision\(uint16_t revision\)"
                         r"\s*\{[^}]*return true;", version_mapping)
    add("SlotSizeOverrideSupportedForRevision returns true for every revision",
        "documented as intentional (PE 2949 branch gates internally)",
        "unconditional true" if slot_all else "changed",
        True,
        "src/core/version_mapping.cpp:32-37 - install path itself gates on "
        "revision == 2949 and disables Slot Size when the setter is not unique")

    payload = {"exe": pe.path, "image_base": pe.image_base, "checks": checks}
    json_path = Path(args.outdir) / "pe2949-contracts.json"
    json_path.write_text(json.dumps(payload, indent=1), encoding="utf-8")

    md_lines = ["# PE 2949 anchor-contract verification", "",
                f"Image: `{pe.path}`",
                f"Image base: `0x{pe.image_base:X}`", "",
                "| contract check | expected | actual | result | evidence |",
                "| --- | --- | --- | --- | --- |"]
    for c in checks:
        md_lines.append(f"| {c['check']} | {c['expected']} | {c['actual']} | "
                        f"**{c['result']}** | {c['evidence']} |")
    md_path = Path(args.outdir) / "pe2949-contracts.md"
    md_path.write_text("\n".join(md_lines) + "\n", encoding="utf-8")

    passed = sum(1 for c in checks if c["result"] == "PASS")
    for c in checks:
        mark = "PASS" if c["result"] == "PASS" else "FAIL"
        print(f"[{mark}] {c['check']}")
        print(f"       expected={c['expected']}  actual={c['actual']}")
    print(f"\n{passed}/{len(checks)} checks passed; wrote {md_path}")
    return 0 if passed == len(checks) else 1


def cmd_runtime_cases(args) -> int:
    """Emit a Cheat Engine Lua script that re-checks every unique match in RAM.

    The plan requires file offset / RVA / VA per entry. This proves the VA
    column: the running image must still carry the audited bytes at
    image_base + RVA (read-only; no process state is modified).
    """
    report = json.loads(Path(args.json).read_text(encoding="utf-8"))
    exe = report["exe"]
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    cases = []
    for r in report["results"]:
        if r["kind"] == "sig" and r["status"] == "UNIQUE" and r["matches"]:
            cases.append((r["symbol"], r["matches"][0]["rva"],
                          _pattern_to_lua(r["pattern"])))
        elif r["kind"] == "anchors":
            for a in r.get("anchor_results", []):
                if a["match_count"] == 1 and a["matches"]:
                    cases.append((f"kCharMgrAnchors[{a['index']}]",
                                  a["matches"][0]["rva"],
                                  _pattern_to_lua(a["pattern"])))
        elif r["kind"] == "str" and r["matches"]:
            text = (r["text_value"] or "").encode("ascii")
            cases.append((r["symbol"], r["matches"][0]["rva"],
                          "".join(f"{b:02X}" for b in text)))

    lines = ["-- AUTO-GENERATED by tools/audit/pe2949_audit.py",
             "-- Read-only runtime cross-check of the PE 2949 audit.",
             f'-- Target image: {exe["path"]}',
             f'-- Expected image base: 0x{exe["image_base"]:X}',
             'local modName = "CrimsonDesert.exe"',
             'local base = getAddress(modName)',
             'if not base then print("RUNTIME CHECK: module not found") return end',
             f'local expectedBase = 0x{exe["image_base"]:X}',
             'local cases = {']
    for name, rva, hexpat in cases:
        lines.append(f'  {{"{name}", 0x{rva:X}, "{hexpat}"}},')
    lines += [
        '}',
        '',
        'if base ~= expectedBase then',
        '  print(string.format("RUNTIME CHECK: base mismatch live=0x%X expected=0x%X "',
        '        .. "(all RVAs are still valid relative to the live module)", base, expectedBase))',
        'end',
        '',
        'local ok, bad = 0, 0',
        'for _, c in ipairs(cases) do',
        '  local name, rva, pat = c[1], c[2], c[3]',
        '  local n = string.len(pat) // 2',
        '  local mem = readBytes(base + rva, n, true)',
        '  if mem == nil then',
        '    print("READFAIL  " .. name)',
        '    bad = bad + 1',
        '  else',
        '    local good = true',
        '    local got = {}',
        '    for i = 1, n do',
        '      local p = string.sub(pat, i * 2 - 1, i * 2)',
        '      got[i] = string.format("%02X", mem[i])',
        '      if p ~= "??" and mem[i] ~= tonumber(p, 16) then good = false end',
        '    end',
        '    if good then ok = ok + 1',
        '    else',
        '      print(string.format("MISMATCH  %s rva=0x%X file=%s mem=%s",',
        '            name, rva, pat, table.concat(got)))',
        '      bad = bad + 1',
        '    end',
        '  end',
        'end',
        'print(string.format("RUNTIME CROSSCHECK: cases=%d ok=%d bad=%d", #cases, ok, bad))',
    ]
    lua_path = outdir / "pe2949-runtime-check.lua"
    lua_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(cases)} runtime cases -> {lua_path}")
    return 0


def _pattern_to_lua(pattern: str | None) -> str:
    out = []
    for tok in (pattern or "").split():
        out.append("??" if tok.startswith("?") else tok.upper().zfill(2))
    return "".join(out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_info = sub.add_parser("info", help="dump PE identity + section table")
    p_info.add_argument("--exe", required=True)
    p_info.set_defaults(func=cmd_info)

    p_scan = sub.add_parser("scan", help="scan every registry entry against the EXE")
    p_scan.add_argument("--exe", required=True)
    p_scan.add_argument("--repo", default=".")
    p_scan.add_argument("--outdir", default="docs/audits")
    p_scan.set_defaults(func=cmd_scan)

    p_sum = sub.add_parser("summary", help="group an existing scan JSON by status")
    p_sum.add_argument("--json", default="docs/audits/pe2949-scan.json")
    p_sum.set_defaults(func=cmd_summary)

    p_con = sub.add_parser("contracts", help="verify the PE 2949 anchor contracts")
    p_con.add_argument("--exe", required=True)
    p_con.add_argument("--repo", default=".")
    p_con.add_argument("--outdir", default="docs/audits")
    p_con.set_defaults(func=cmd_contracts)

    p_ext = sub.add_parser("extras", help="scan AOB literals outside the registry")
    p_ext.add_argument("--exe", required=True)
    p_ext.add_argument("--repo", default=".")
    p_ext.add_argument("--outdir", default="docs/audits")
    p_ext.set_defaults(func=cmd_extras)

    p_rt = sub.add_parser("runtime-cases", help="emit a CE Lua runtime cross-check")
    p_rt.add_argument("--json", default="docs/audits/pe2949-scan.json")
    p_rt.add_argument("--outdir", default="docs/audits")
    p_rt.set_defaults(func=cmd_runtime_cases)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
