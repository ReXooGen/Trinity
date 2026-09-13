import pefile
import re
import struct

EXE_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe"

print("=" * 80)
print("TRINITY TU 2.01 (REVISION 2760) BINARY AUDIT")
print("=" * 80)

pe = pefile.PE(EXE_PATH, fast_load=True)
data2 = [s for s in pe.sections if s.Name.decode(errors='ignore').startswith('.data2')][0]
raw = data2.get_data()
base_va = pe.OPTIONAL_HEADER.ImageBase + data2.VirtualAddress

print(f"Target Section .data2: VA=0x{base_va:016X}, Size={len(raw)} bytes")

def scan(pat_str):
    parts = pat_str.strip().split()
    b_parts = []
    for p in parts:
        if p in ('?', '??'):
            b_parts.append(b'.')
        else:
            b_parts.append(re.escape(bytes([int(p, 16)])))
    regex = re.compile(b''.join(b_parts), re.DOTALL)
    return [base_va + m.start() for m in regex.finditer(raw)]

sigs = [
    ("DyeApplyBatch (TU 2.01)", "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 58 EE FF FF"),
    ("DyeUpsert (TU 2.01)", "48 8B 41 78 4C 8D 49 78 45 8B 51 08 41 8B CA 48 C1 E1 04"),
    ("DyeVisualSet (TU 2.01)", "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 45 0F B7 F1"),
    ("DyeVisualClear (TU 2.01)", "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 45 0F B6 E9 45 0F B7 F0 48"),
    ("TrItemValueCtor (TU 2.01)", "48 89 5C 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 4C 8B F2 4C 8B E1 48 C7 01 FF FF FF FF 0F B7 02 66 89 41 08"),
    ("InvHolderInsert (Authentic)", "48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 20 FE FF FF 48 81 EC E0 02 00 00"),
    ("InvCommitPlacement (TU 2.01)", "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 F3 FF FF 48 81 EC 40 0D 00 00"),
    ("InvCommit (TU 2.01)", "48 89 5C 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D0 FC FF FF 48 81 EC"),
    ("InvGetItemQty", "66 89 54 24 10 53 57 48 83 EC 28 0F B7 DA"),
    ("SetDestinationMarker (TU 2.01)", "48 83 79 28 00 C5 FB 10 02 C5 FB 11 81 E0 00 00 00 8B 42 08 89 81 E8 00 00 00"),
    ("DyeApplySlot (TU 2.01)", "48 89 5C 24 18 4C 89 4C 24 20 66 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 4D 8B"),
    ("FriendlySetNpc (TU 2.01)", "4C 8B DC 53 55 56 57 41 56 41 57 48 83 EC 68 48 8B FA 48 8B F1 0F B7 42 04"),
]

all_passed = True
for name, pat in sigs:
    hits = scan(pat)
    if len(hits) == 1:
        print(f"[OK]   {name:<30} -> VA: 0x{hits[0]:016X}")
    elif len(hits) > 1:
        print(f"[WARN] {name:<30} -> MULTIPLE HITS ({len(hits)}): {[hex(h) for h in hits]}")
        all_passed = False
    else:
        print(f"[FAIL] {name:<30} -> NOT FOUND")
        all_passed = False

print("=" * 80)
if all_passed:
    print("ALL TARGET SUBSYSTEM SIGNATURES VERIFIED 100% UNIQUE IN .data2!")
else:
    print("WARNING: Some signatures failed verification!")
print("=" * 80)
