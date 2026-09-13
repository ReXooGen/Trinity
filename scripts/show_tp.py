import subprocess

def show(rev, file):
    cmd = ['git', 'show', f'{rev}:{file}']
    return subprocess.check_output(cmd, text=True, encoding='utf-8', errors='replace')

tp = show('d33107b2b208fea0615e9b7095a2944fc4b15650', 'src/game/teleport.cpp')

lines = tp.splitlines()
for i, line in enumerate(lines):
    if 'InitMarkerSubsystem' in line or 'TeleportToMarker' in line or 'InstallMarkerHook' in line:
        print(f"L{i+1}: {line}")
