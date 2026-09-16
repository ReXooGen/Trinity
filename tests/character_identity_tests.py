"""Regression for the actual TU2.02 inventory identity classifier.

Keys/type IDs are captured in scratch/dye-target-analysis.md. Engine memory is
fixture memory; the production classifier body and native reader run unchanged.
"""
from pathlib import Path
import unittest
from dye_editor_tests import ROOT, declaration, compile_and_run


class CharacterIdentityTests(unittest.TestCase):
    def test_same_source_pointer_does_not_authorize_an_npc(self):
        source = (ROOT / "src/game/dye.cpp").read_text(encoding="utf-8")
        identity = declaration(source, "struct DyeItemIdentity") + ";"
        matching = declaration(source, "uintptr_t MatchingDyeEntry(")
        code = r'''
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "mem/safe_memory.h"
namespace trinity::game {
using namespace trinity::mem;
static int wearer = -1;
namespace core { struct Version { int revision = 2850; }; static Version GetGameVersion() { return {}; } }
struct Inventory { static int IdentifyCharacterFromComp(uintptr_t) { return wearer; } };
static uintptr_t entry = 0;
static uintptr_t FindEntryByTag(uintptr_t, uint16_t) { return entry; }
IDENTITY
MATCHING
}
using namespace trinity::game;
int main() {
    auto* b=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x3000,MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN,PAGE_READWRITE));
    if (!b) return 10;
    const uintptr_t comp=reinterpret_cast<uintptr_t>(b), owner=comp+0x1000;
    entry=comp+0x2000;
    WritePtr(comp+8,owner); Write16(entry+8,4404); Write64(entry,uint64_t(-1));
    DyeItemIdentity item{}; item.character=1; item.tag=4; item.type=4404; item.instance=-1;
    item.sourceComp=comp; item.sourceOwner=owner; item.sourceEntry=entry;
    wearer=-1;
    if (MatchingDyeEntry(comp,item)) { fputs("NPC accepted because source pointer and instance=-1 still match\n",stderr); return 1; }
    wearer=2;
    if (MatchingDyeEntry(comp,item)) { fputs("Oongka source accepted for Damiane request\n",stderr); return 2; }
    wearer=1;
    if (MatchingDyeEntry(comp,item)!=entry) { fputs("same genuine wearer rejected\n",stderr); return 3; }
    item.mode=1; wearer=-1;
    if (MatchingDyeEntry(comp,item)!=entry) { fputs("protagonist guard incorrectly disabled mount path\n",stderr); return 4; }
    VirtualFree(b,0,MEM_RELEASE);
    puts("Dye source guard: unknown/NPC/wrong protagonist denied even at the same source pointer.");
}
'''.replace('IDENTITY', identity).replace('MATCHING', matching)
        result = compile_and_run(code)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())

    def test_npc_gear_cannot_determine_protagonist_identity(self):
        source = (ROOT / "src/game/inventory.cpp").read_text(encoding="utf-8")
        classifier = declaration(source, "static int IdentifyCharacterIdentity(")
        helper = '#include "game/native_character_identity.h"' if (ROOT / "src/game/native_character_identity.h").exists() else ''
        code = r'''
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include "mem/safe_memory.h"
HELPER
namespace trinity::game {
using namespace trinity::mem;
struct EquipTableDesc { uintptr_t array = 0; uint32_t count = 0; uintptr_t stride = 0xD0; bool valid = false; };
static EquipTableDesc ReadNativeEquipmentTable(uintptr_t comp) {
    uintptr_t desc = 0, array = 0; uint32_t count = 0;
    if (!ReadPtr(comp+0x90,&desc) || !ReadPtr(desc+8,&array) || !Read32(desc+0x10,&count)) return {};
    return {array,count,0xD0,true};
}
namespace core { struct Version { int revision=2850; }; static Version GetGameVersion() { return {}; } }
static uintptr_t g_characterTableGlobal = 0;
static const char* ItemKey(uint16_t tid) {
    switch (tid) {
    case 6353: return "Nairaden_OneHandMace";
    case 5304: return "Bolton_PlateArmor_Helm";
    case 4404: return "Katerm_Leather_Armor";
    case 4341: return "Matazu_Leather_Gloves";
    case 4494: return "Studded_Leather_Boots";
    case 6250: return "Lantern";
    case 6258: return "Torch";
    case 6382: return "Demian_OneHandRapier";
    case 6425: return "Damian_OneHandShield";
    case 6564: return "Damian_TwoHandPistol";
    default: return "SharedArmor";
    }
}
static bool KeyForType(uint16_t tid, char* out, size_t size) { strcpy_s(out,size,ItemKey(tid)); return true; }
struct Inventory { static bool NameForTypeId(uint16_t tid,char* out,size_t size) { return KeyForType(tid,out,size); } };
CLASSIFIER
}
using namespace trinity::game;
static void Put(uintptr_t p, uintptr_t value) { memcpy(reinterpret_cast<void*>(p),&value,8); }
static void Put32(uintptr_t p,uint32_t value) { memcpy(reinterpret_cast<void*>(p),&value,4); }
static void Put16(uintptr_t p,uint16_t value) { memcpy(reinterpret_cast<void*>(p),&value,2); }
int main() {
    auto* b = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x20000,MEM_COMMIT|MEM_RESERVE|MEM_TOP_DOWN,PAGE_READWRITE));
    if (!b) return 10;
    const uintptr_t base=reinterpret_cast<uintptr_t>(b);
    const uintptr_t comp=base,owner=base+0x1000,sub=base+0x2000,status=base+0x3000;
    const uintptr_t table=base+0x4000,defs=base+0x5000,def=base+0x15000,str=base+0x16000,chars=base+0x17000;
    const uintptr_t entries=base+0x18000,desc=base+0x19000,global=base+0x1A000;
    Put(comp+8,owner); Put(owner+0x68,sub); Put(sub+0x38,comp); Put(sub+0x20,status); Put(status+8,owner);
    Put(global,table); g_characterTableGlobal=global; Put32(table+8,7250); Put(table+0x58,defs);
    Put(def+8,str); Put(str,chars);
    Put(comp+0x90,desc); Put(desc+8,entries);
    auto prepare=[&](uint16_t row,const char* key,const std::vector<uint16_t>& items) {
        Put16(status+0x30,row); Put(defs+uintptr_t{row}*8,def); strcpy_s(reinterpret_cast<char*>(chars),128,key);
        Put32(desc+0x10,static_cast<uint32_t>(items.size()));
        for(size_t i=0;i<items.size();++i) Put16(entries+i*0xD0+8,items[i]);
    };
    const std::vector<uint16_t> npc{6353,5304,4404,4341,4494,6250,6258};
    prepare(5810,"NHM_Hernand_Soldiers_OneHandMace_53208",npc);
    auto before=std::vector<uint8_t>(b,b+0x20000);
    const int npcId=IdentifyCharacterIdentity(comp);
    if (npcId != -1) { fprintf(stderr,"NPC with helmet type5304 was classified as protagonist %d\n",npcId); return 1; }
    if (memcmp(b,before.data(),before.size())) { fputs("identity lookup modified native input\n",stderr); return 2; }
    for (const char* npcKey : {"Damian_Clone","Kliff_AI","Oongka_Bandit","","Kliff_Clone"}) {
        prepare(4,npcKey,{6382});
        if (IdentifyCharacterIdentity(comp)!=-1) { fprintf(stderr,"non-playable key accepted: %s\n",npcKey); return 3; }
    }
    for (int who=0;who<3;++who) {
        const char* names[]={"Kliff","Damian","Oongka"};
        for (const auto& gear : {std::vector<uint16_t>{6564,6382,6425},std::vector<uint16_t>{6382,6425,6564},npc}) {
            // Deliberately move the row: native key, not a hardcoded 0/3/5, is identity.
            prepare(static_cast<uint16_t>(7000+who),names[who],gear);
            if (IdentifyCharacterIdentity(comp)!=who) { fputs("identity depends on gear order/type instead of native key\n",stderr); return 4; }
        }
    }
    prepare(3,"Damian",{6382});
    Put32(owner+0x50,1); // coincidental legacy PartyIndex must not select Kliff
    if (IdentifyCharacterIdentity(comp)!=1) return 5;
    Put(status+8,owner+8);
    if (IdentifyCharacterIdentity(comp)!=-1) { fputs("wrong status-owner backlink accepted\n",stderr); return 6; }
    Put(status+8,owner); Put32(table+8,3);
    if (IdentifyCharacterIdentity(comp)!=-1) { fputs("out-of-range native character row accepted\n",stderr); return 7; }
    Put32(table+8,7250); Put(table+0x58,0);
    if (IdentifyCharacterIdentity(comp)!=-1) { fputs("missing native catalog fell back to gear\n",stderr); return 8; }
    puts("Native character identity: NPC denied, exact protagonists stable across equipment order, invalid links/catalog fail closed.");
    VirtualFree(b,0,MEM_RELEASE);
}
'''.replace('HELPER',helper).replace('CLASSIFIER',classifier)
        result=compile_and_run(code)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
