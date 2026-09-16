"""Compile the real dye UI functions against deterministic UI/game boundaries.

This exercises stale page navigation without a game process. It does not claim
that material/shader pixels have been tested. The source function bodies and
SlotInfo contract are taken verbatim from production, not reimplemented here.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def declaration(source, signature):
    start = source.index(signature)
    pos = source.index("{", start)
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compile_and_run(code):
    with tempfile.TemporaryDirectory(prefix="trinity-dye-ui-") as directory:
        directory = Path(directory)
        source, exe = directory / "fixture.cpp", directory / "fixture.exe"
        source.write_text(code, encoding="utf-8")
        flags = f'cl.exe /nologo /std:c++17 /EHsc /I"{ROOT / "src"}" "{source}" /Fe:"{exe}"'
        compiler = shutil.which("cl.exe")
        if compiler:
            command = flags
        else:
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
            if not vs:
                raise RuntimeError("MSVC C++ build tools are required for the dye UI regression")
            vcvars = Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"
            command = f'call "{vcvars}" >nul && {flags}'
        batch = directory / "build.cmd"
        batch.write_text("@echo off\n" + command + "\n", encoding="utf-8")
        build = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=directory,
                               text=True, capture_output=True, timeout=90)
        if build.returncode:
            raise RuntimeError("Fixture did not compile:\n" + build.stdout + build.stderr)
        return subprocess.run([str(exe)], cwd=directory, text=True, capture_output=True, timeout=15)


class DyeEditorTests(unittest.TestCase):
    def test_custom_page_does_not_retarget_after_equipment_changes(self):
        menu = (ROOT / "src/gui/menu.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/game/dye.h").read_text(encoding="utf-8")
        slot = declaration(header, "struct SlotInfo") + ";"
        channel = declaration(header, "struct Channel") + ";"
        # Copy only the actual dye editor globals, not other menu subsystems.
        globals_start = menu.index("static uint16_t s_dyeTag")
        globals_end = menu.index("// Poll the queued apply", globals_start)
        globals_text = menu[globals_start:globals_end]
        if "s_dyeTarget" not in globals_text:
            globals_text += "\nstatic game::Dye::SlotInfo s_dyeTarget{};\n"
        helper = declaration(menu, "static bool SelectedDyeSlot(") if (
            "static bool SelectedDyeSlot(" in menu and "static bool SelectedDyeSlot(" not in globals_text) else ""
        sender = declaration(menu, "static void SendDye(")
        editor = declaration(menu, "static void RenderDyeCustom(")
        seed = ""
        mutations = [("typeId", "4404", "4405"), ("instanceId", "101", "102")]
        # The baseline has no source stamp. Once present, exercise every field.
        stamped = {
            "characterIndex": ("1", "2"), "targetMode": ("0", "1"), "mountIndex": ("0", "1"),
            "controlledOwner": ("0x100000", "0x200000"), "worldRoot": ("0x300000", "0x400000"),
            "sourceComp": ("0x500000", "0x600000"), "sourceEntry": ("0x700000", "0x800000"),
            "sourceOwner": ("0x900000", "0xA00000"), "selectionGeneration": ("3", "4"),
            "worldGeneration": ("5", "6"),
        }
        for field, (before, after) in stamped.items():
            if field in slot:
                seed += f"original.{field} = {before};\n"
                mutations.append((field, before, after))
        cases = ""
        for field, before, after in mutations:
            cases += f'''
    current = original; current.{field} = {after}; applyCalls = 0; previewCalls = 0;
    RenderDyeCustom();
    if (applyCalls) {{ fprintf(stderr, "stale dye page sent color to replacement: {field}\\n"); return 1; }}
    if (previewCalls) {{ fprintf(stderr, "stale page displayed replacement item: {field}\\n"); return 2; }}
'''
        code = r'''
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "game/dye_data.h"
#define LOC(x) x
namespace game = trinity::game;
namespace trinity::game {
class Dye { public:
SLOT_DECL
CHANNEL_DECL
    static int SlotCount();
    static bool GetSlot(int, SlotInfo*);
    static bool Ready() { return true; }
    static void CancelRetouch() {}
    static bool GetChannel(uint16_t, int, Channel*, const SlotInfo* = nullptr) { return false; }
    static int GetTargetMode() { return 0; }
    static int GetActiveCharacter() { return 1; }
    static int GetActiveMount() { return 0; }
    static bool Apply(uint16_t, int, const Channel&, const SlotInfo* = nullptr);
};
}
static game::Dye::SlotInfo current{};
static int applyCalls = 0, previewCalls = 0;
int game::Dye::SlotCount() { return 1; }
bool game::Dye::GetSlot(int i, SlotInfo* out) { if (i != 0) return false; *out = current; return true; }
bool game::Dye::Apply(uint16_t, int, const Channel&, const SlotInfo*) { ++applyCalls; return true; }
namespace ui {
    static void Begin(const char* = nullptr) {}
    static void End() {}
    template<class... T> static void Toast(const char*, T...) {}
    template<class... T> static bool Option(const char*, T...) { return false; }
    template<class... T> static bool IntOption(const char*, T...) { return false; }
    static int SwatchRow(const char* label, const uint32_t*, int, int*, int, const char*, int = -1) {
        return strcmp(label, "Apply This Color") == 0 ? 0 : -1;
    }
}
GLOBALS
HELPER
static void UpdateDyeTooltip(const game::Dye::SlotInfo&, uint32_t, int, bool = false) { ++previewCalls; }
static void SendDyeAllEquipped(uint32_t, int, int, int) {}
SENDER
EDITOR
int main() {
    game::Dye::SlotInfo original{};
    original.tag = 4; original.typeId = 4404; original.instanceId = 101; original.dyeable = true;
    strcpy(original.itemName, "Selected armor");
SEED
    s_dyeTag = original.tag; strcpy(s_dyeItem, original.itemName); s_dyeTarget = original;
    current = original;
    RenderDyeCustom();
    if (applyCalls != 1 || previewCalls != 1) { fputs("unchanged item cannot be dyed\n", stderr); return 3; }
CASES
    current = original; current.dyeCount = 12; applyCalls = previewCalls = 0;
    RenderDyeCustom();
    if (applyCalls != 1 || previewCalls != 1) { fputs("color-only snapshot change invalidated original item\n", stderr); return 4; }
    puts("Dye custom editor: original item usable; replacement source/identity rejected before preview/apply.");
}
'''
        for token, replacement in [("SLOT_DECL", slot), ("CHANNEL_DECL", channel), ("GLOBALS", globals_text),
                                    ("HELPER", helper), ("SENDER", sender), ("EDITOR", editor),
                                    ("SEED", seed), ("CASES", cases)]:
            code = code.replace(token, replacement)
        result = compile_and_run(code)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
