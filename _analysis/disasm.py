"""Disassemble module bytes at given RVAs and resolve referenced strings.

usage: disasm.py <pe-path> <hex-rva> [count] [more-rvas...]
Read-only analysis helper.
"""
import sys, struct
sys.path.insert(0, r"C:\Users\mul0\Documents\GitHub\Trinity\build-verify\pydeps")
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

path = sys.argv[1]
rvas = sys.argv[2:]
pe = pefile.PE(path, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
raw = open(path, 'rb').read()

def rva_to_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

def read_str(rva, maxlen=200):
    o = rva_to_off(rva)
    if o is None:
        return None
    chunk = raw[o:o + maxlen]
    end = chunk.find(b'\x00')
    if end < 0:
        return None
    s = chunk[:end]
    if len(s) < 3:
        return None
    if all(32 <= c < 127 for c in s):
        return s.decode('ascii')
    return None

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

for spec in rvas:
    rva = int(spec, 16)
    o = rva_to_off(rva)
    if o is None:
        print("RVA 0x%X not in any section" % rva)
        continue
    print("\n===== %s + 0x%X (file off 0x%X) =====" % (path.split('\\')[-1], rva, o))
    code = raw[o:o + 320]
    for ins in md.disasm(code, image_base + rva):
        line = "  %016X  %-28s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str)
        extra = ''
        for op in ins.operands:
            if op.type == 3 and op.mem.base == 41:  # X86_REG_RIP
                tgt = ins.address + ins.size + op.mem.disp
                trva = tgt - image_base
                s = read_str(trva)
                extra = "   ; -> rva 0x%X %s" % (trva, ('"%s"' % s) if s else '')
        if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            try:
                trva = int(ins.op_str, 16) - image_base
                extra = "   ; -> rva 0x%X" % trva
            except ValueError:
                pass
        print(line + extra)
        if ins.mnemonic == 'ret' or ins.mnemonic == 'int3':
            break
