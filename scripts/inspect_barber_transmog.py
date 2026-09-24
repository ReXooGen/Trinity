"""Read-only PE research CLI: survey, strings, candidate xrefs, RTTI and disassembly.

Searches all raw-backed executable sections, not only .text. Byte-pattern xrefs
are candidates; disassemble their containing runtime function to verify boundaries.
No process access, target writes, or native game calls.
"""
import argparse
import bisect
import hashlib
import json
import mmap
import re
import struct
from pathlib import Path

import capstone
import pefile
from capstone.x86_const import X86_OP_MEM, X86_REG_RIP


class Inspector:
    def __init__(self, path):
        self.file = open(path, 'rb')
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[1, 3, 13])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = self.pe.sections
        self.funcs = sorted((self.base + x.struct.BeginAddress,
                             self.base + x.struct.EndAddress)
                            for x in getattr(self.pe, 'DIRECTORY_ENTRY_EXCEPTION', []))
        self.starts = [x[0] for x in self.funcs]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def close(self):
        self.pe.close()
        self.data.close()
        self.file.close()

    def va(self, offset):
        for s in self.sections:
            if s.PointerToRawData <= offset < s.PointerToRawData + s.SizeOfRawData:
                return self.base + s.VirtualAddress + offset - s.PointerToRawData
        return None

    def offset(self, va):
        for s in self.sections:
            delta = va - self.base - s.VirtualAddress
            if 0 <= delta < s.SizeOfRawData:
                return s.PointerToRawData + delta
        return None

    def read(self, va, size):
        pos = self.offset(va)
        if pos is None:
            return b''
        for s in self.sections:
            if s.PointerToRawData <= pos < s.PointerToRawData + s.SizeOfRawData:
                return self.data[pos:min(pos + size, s.PointerToRawData + s.SizeOfRawData)]
        return b''

    def text(self, va):
        raw = self.read(va, 160).split(b'\0', 1)[0]
        if len(raw) >= 4 and all(32 <= c < 127 for c in raw):
            return raw.decode('ascii')
        return ''

    def bounds(self, va):
        idx = bisect.bisect_right(self.starts, va) - 1
        if idx >= 0 and self.funcs[idx][0] <= va < self.funcs[idx][1]:
            return self.funcs[idx]
        return None

    def hits(self, needle):
        pos = 0
        while True:
            pos = self.data.find(needle, pos)
            if pos < 0:
                return
            yield pos
            pos += 1

    def survey(self):
        imports = {}
        for attr in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for mod in getattr(self.pe, attr, []):
                imports[attr + ':' + mod.dll.decode()] = [
                    i.name.decode(errors='replace') if i.name else f'ordinal:{i.ordinal}'
                    for i in mod.imports]
        return {'sha256': hashlib.sha256(self.data).hexdigest(),
                'size': len(self.data), 'base': hex(self.base),
                'timestamp': hex(self.pe.FILE_HEADER.TimeDateStamp),
                'machine': hex(self.pe.FILE_HEADER.Machine),
                'runtime_functions': len(self.funcs), 'imports': imports,
                'sections': [{'name': s.Name.rstrip(b'\0').decode(),
                              'va': hex(self.base + s.VirtualAddress),
                              'raw_size': s.SizeOfRawData,
                              'executable': bool(s.Characteristics & 0x20000000)}
                             for s in self.sections]}

    def strings(self, patterns):
        found = {}
        filters = [re.compile(pattern.encode(), re.IGNORECASE) for pattern in patterns]
        for m in re.finditer(rb'[\x20-\x7e]{4,}', self.data):
            raw = m.group()
            if any(pattern.search(raw) for pattern in filters):
                va = self.va(m.start())
                if va:
                    found[va] = raw.decode('ascii')
        return [{'va': hex(va), 'text': text} for va, text in sorted(found.items())]

    def xrefs(self, targets):
        targets = set(targets)
        out = []
        rip = re.compile(rb'[\x40-\x4f]?(?:[\x8d\x8b\x89\x83\x81\x80\xff]|\x0f[\xb6\xb7\xbe\xbf])[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....', re.DOTALL)
        direct = re.compile(rb'[\xe8\xe9]....', re.DOTALL)
        for sec in self.sections:
            if not sec.Characteristics & 0x20000000:
                continue
            data = self.data[sec.PointerToRawData:sec.PointerToRawData + sec.SizeOfRawData]
            base = self.base + sec.VirtualAddress
            # Lookahead permits overlapping candidates; instruction boundaries are not assumed.
            for pattern, length, disp_at, kind in ((rip, 7, 3, 'rip'), (direct, 5, 1, 'branch')):
                overlap = re.compile(b'(?=(' + pattern.pattern + b'))', re.DOTALL)
                for m in overlap.finditer(data):
                    at = base + m.start()
                    ins = None
                    if kind == 'rip':
                        candidate = m.group(1)
                        opcode_index = 1 if 0x40 <= candidate[0] <= 0x4f else 0
                        opcode = candidate[opcode_index]
                        extra = 1 if opcode in (0x80, 0x83) else 4 if opcode == 0x81 else 0
                        end = at + len(candidate) + extra
                        dst = end + struct.unpack_from('<i', candidate, len(candidate) - 4)[0]
                        if dst not in targets:
                            continue
                        ins = next(self.md.disasm(data[m.start():m.start() + 15], at), None)
                        if ins is None:
                            continue
                        dests = [ins.address + ins.size + op.mem.disp for op in ins.operands
                                 if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP]
                        if not dests:
                            continue
                        dst = dests[0]
                    else:
                        dst = at + length + struct.unpack_from('<i', data, m.start() + disp_at)[0]
                    if dst not in targets:
                        continue
                    bounds = self.bounds(at)
                    ins = ins or next(self.md.disasm(data[m.start():m.start() + 15], at), None)
                    out.append({'from': hex(at), 'to': hex(dst), 'kind': kind,
                                'function': [hex(v) for v in bounds] if bounds else None,
                                'instruction': f'{ins.mnemonic} {ins.op_str}' if ins else '?'})
        for target in targets:
            for pos in self.hits(struct.pack('<Q', target)):
                va = self.va(pos)
                if va:
                    out.append({'from': hex(va), 'to': hex(target), 'kind': 'pointer'})
        return out

    def disasm(self, va, size, function=False):
        bounds = self.bounds(va)
        if function and bounds:
            va = bounds[0]
            size = min(size, bounds[1] - va)
        result = []
        for ins in self.md.disasm(self.read(va, size), va):
            refs = []
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    target = ins.address + ins.size + op.mem.disp
                    text = self.text(target)
                    refs.append(hex(target) + (' ' + repr(text) if text else ''))
            result.append(f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}' +
                          (' ; ' + ' | '.join(refs) if refs else ''))
        return {'requested_function': [hex(v) for v in bounds] if bounds else None,
                'start': hex(va), 'limit_bytes': size, 'instructions': result}

    def rtti(self, name):
        out = []
        for pos in self.hits(name.encode() + b'\0'):
            td = self.va(pos - 16)
            if td is None:
                continue
            for ref in self.hits(struct.pack('<I', td - self.base)):
                if ref < 12:
                    continue
                col = self.va(ref - 12)
                if col is None:
                    continue
                sig, offset, cd, typ, chd, own = struct.unpack_from('<6I', self.data, ref - 12)
                if sig != 1 or own != col - self.base:
                    continue
                for ptr in self.hits(struct.pack('<Q', col)):
                    vt = self.va(ptr + 8)
                    if vt is None:
                        continue
                    methods = []
                    for idx in range(320):
                        raw = self.read(vt + idx * 8, 8)
                        if len(raw) != 8:
                            break
                        fn = struct.unpack('<Q', raw)[0]
                        if not any(s.Characteristics & 0x20000000 and
                                   self.base + s.VirtualAddress <= fn < self.base + s.VirtualAddress + s.SizeOfRawData
                                   for s in self.sections):
                            break
                        methods.append(hex(fn))
                    out.append({'type': hex(td), 'col': hex(col), 'offset': offset,
                                'vtable': hex(vt), 'methods': methods})
        return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', default=r'C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe')
    p.add_argument('--survey', action='store_true')
    p.add_argument('--strings', nargs='+')
    p.add_argument('--xref', nargs='+', type=lambda x: int(x, 0))
    p.add_argument('--va', nargs='+', type=lambda x: int(x, 0))
    p.add_argument('--bytes', type=int, default=2048)
    p.add_argument('--function', action='store_true')
    p.add_argument('--rtti', nargs='+')
    p.add_argument('--out', type=Path, help='Optional evidence JSON; parent must already exist')
    p.add_argument('--quiet', action='store_true', help='Print only saved artifact path')
    p.add_argument('--hex', nargs='+', type=lambda x: int(x, 0), help='Read raw bytes at VAs')
    args = p.parse_args()
    inspector = Inspector(args.exe)
    try:
        result = {}
        if args.survey:
            result['survey'] = inspector.survey()
        if args.strings:
            result['strings'] = inspector.strings(args.strings)
        if args.xref:
            result['xref_candidates'] = inspector.xrefs(args.xref)
        if args.va:
            result['disassembly'] = [inspector.disasm(va, args.bytes, args.function) for va in args.va]
        if args.rtti:
            result['rtti'] = {name: inspector.rtti(name) for name in args.rtti}
        if args.hex:
            result['raw'] = {hex(va): inspector.read(va, args.bytes).hex(' ') for va in args.hex}
        text = json.dumps(result, indent=2, ensure_ascii=True)
        if args.out:
            with args.out.open('x', encoding='utf-8') as f:
                f.write(text + '\n')
        print(str(args.out) if args.quiet and args.out else text)
    finally:
        inspector.close()


if __name__ == '__main__':
    main()
