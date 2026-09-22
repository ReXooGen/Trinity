#!/usr/bin/env python3
"""
Disassemble a range of the on-disk game image and locate the enclosing function.

Used for crash triage: given a faulting RVA from Trinity_Crash.txt, print the
exact instruction, its section, and the function prologue it belongs to.

Usage:
    python disasm_range.py --exe <path> --rva 0x47AF8C [--before 40] [--after 16]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import pe2949_audit as A  # noqa: E402


def find_function_start(reader: A.ImageReader, md, rva: int, max_back: int = 0x800):
    """Walk back for a plausible prologue, then verify linear decode reaches rva."""
    candidates = []
    start = rva - max_back
    buf = reader.read(start, max_back + 32)
    va_start = reader.pe.image_base + start
    for insn in md.disasm(buf, va_start):
        if insn.mnemonic == "int3":
            candidates.append(insn.address + insn.size)
        if insn.mnemonic in ("push", "sub", "mov", "lea") and insn.address + insn.size <= rva:
            if candidates and candidates[-1] == insn.address:
                pass
    # The reliable signal: a run of int3 (padding) immediately before the prologue.
    best = None
    for i in range(rva - start - 1, 0, -1):
        if buf[i] == 0xCC and buf[i - 1] == 0xCC:
            best = start + i + 1
            break
    return best


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--rva", required=True)
    ap.add_argument("--before", type=int, default=0x40)
    ap.add_argument("--after", type=int, default=0x20)
    args = ap.parse_args()

    rva = int(args.rva, 0)
    pe = A.parse_pe(Path(args.exe))
    md = A.make_disassembler()

    with A.ImageReader(pe) as reader:
        sec = reader.section_for_rva(rva)
        print(f"image base      : 0x{pe.image_base:X}")
        print(f"faulting RVA    : 0x{rva:X}   VA 0x{pe.image_base + rva:X}")
        print(f"section         : {sec.name if sec else '?'} "
              f"(exec={sec.executable if sec else '?'})")
        print(f"file offset     : {reader.file_offset(rva)}")
        print()

        start = max(0, rva - args.before)
        span = args.before + args.after
        buf = reader.read(start, span)
        print(f"--- disassembly 0x{start:X} .. 0x{start + span:X} ---")
        for insn in md.disasm(buf, pe.image_base + start):
            marker = "  <== FAULT" if insn.address == pe.image_base + rva else ""
            rip = ""
            try:
                for op in insn.operands:
                    if (op.type == A.capstone.x86.X86_OP_MEM and
                            op.mem.base == A.capstone.x86.X86_REG_RIP):
                        tgt = insn.address + insn.size + op.mem.disp
                        rip = f"   ; -> 0x{tgt:X} (rva 0x{tgt - pe.image_base:X})"
            except Exception:
                pass
            print(f"  0x{insn.address:X} (rva 0x{insn.address - pe.image_base:<8X}) "
                  f"{insn.bytes.hex(' '):<24} {insn.mnemonic} {insn.op_str}{rip}{marker}")

        fstart = find_function_start(reader, md, rva)
        print()
        if fstart is not None:
            print(f"probable function start (after int3 padding): RVA 0x{fstart:X} "
                  f"VA 0x{pe.image_base + fstart:X}")
            buf2 = reader.read(fstart, 64)
            print("--- prologue ---")
            for insn in md.disasm(buf2, pe.image_base + fstart):
                print(f"  0x{insn.address:X} (rva 0x{insn.address - pe.image_base:<8X}) "
                      f"{insn.bytes.hex(' '):<24} {insn.mnemonic} {insn.op_str}")
                if insn.address > pe.image_base + fstart + 40:
                    break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
