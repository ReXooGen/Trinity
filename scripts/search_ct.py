import re

path = r'c:\GABUT\apalah\Cheat_Engine\Crimson Desert Enhanced Ultimate CT (2.1) 3209 Latest 2026-09-06T23-45Z LVZD4oiLG\Crimson Desert Ultimate Table.CT'
with open(path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Find Lua scripts or Assembler scripts
scripts = re.findall(r'<LuaScript>([\s\S]*?)</LuaScript>', text)
print("Lua scripts count:", len(scripts))
for i, s in enumerate(scripts):
    print(f"--- Lua Script {i} ---")
    for line in s.splitlines():
        if any(w in line.lower() for w in ['teleport', 'pin', 'marker', 'dest', 'map', 'waypoint', 'coord']):
            print("  ", line)

entries = re.findall(r'<CheatEntry>([\s\S]*?)</CheatEntry>', text)
print("Cheat entries count:", len(entries))
for e in entries:
    if any(k in e.lower() for k in ['teleport', 'marker', 'map pin']):
        desc = re.search(r'<Description>"(.*?)"</Description>', e)
        desc_str = desc.group(1) if desc else "No desc"
        print("Entry:", desc_str)
        # Check for AutoAssemblerScript
        aas = re.findall(r'<AssemblerScript>([\s\S]*?)</AssemblerScript>', e)
        for s in aas:
            print("  [AssemblerScript]:")
            for line in s.splitlines()[:20]:
                print("   ", line)
