#include "game/dye_record.h"
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
using namespace trinity::game;
static void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static void TestMaterialRetouch()
{
    uint8_t source[12][16]{}, out[12][16]{}, original[12][16]{};
    for (unsigned ch = 0; ch < 12; ++ch)
    {
        for (unsigned b = 0; b < 16; ++b) source[ch][b] = static_cast<uint8_t>(ch * 17 + b);
        source[ch][6] = static_cast<uint8_t>(ch);
        source[ch][11] = static_cast<uint8_t>(ch * 9);
    }
    // An explicit natural/clear zone is not recolored by a material-only edit.
    memset(source[6], 0, 16);
    source[6][4] = source[6][5] = source[6][11] = 0xFF;
    source[6][6] = 6;
    memcpy(original, source, sizeof(source));
    const uint32_t present = (1u << 0) | (1u << 3) | (1u << 6) | (1u << 11);
    const uint32_t dyed = present & ~(1u << 6);
    Check(BuildDyeRetouchRecords(source, present, -1, DyeRetouchMaterial, 10, 0, out) == dyed,
          "all-zone material edit skipped dyed zones or included cleared zones");
    for (unsigned ch = 0; ch < 12; ++ch)
    {
        for (unsigned b = 0; b < 16; ++b)
        {
            const uint8_t expected = !(dyed & (1u << ch)) || b >= 13 ? 0 :
                b == 4 ? 10 : b == 5 ? 0 : source[ch][b];
            Check(out[ch][b] == expected, "material retouch changed color/condition/group or invented an absent record");
        }
    }
    Check(!memcmp(source, original, sizeof(source)), "retouch mutated captured source");

    Check(BuildDyeRetouchRecords(source, present, 3, DyeRetouchCondition, 1, 127, out) == (1u << 3),
          "single-zone condition selection failed");
    for (unsigned b = 0; b < 13; ++b)
        Check(out[3][b] == (b == 11 ? 127 : source[3][b]), "condition retouch changed raw material/color");
    Check(out[0][7] == 0 && out[11][7] == 0, "single-zone edit leaked into other zones");
    Check(BuildDyeRetouchRecords(source, present, -1, DyeRetouchMaterial | DyeRetouchCondition,
                                0xFFFF, 0, out) == dyed, "combined retouch failed");
    for (unsigned ch : {0u, 3u, 11u})
        Check(out[ch][4] == 0xFF && out[ch][5] == 0xFF && out[ch][11] == 0 &&
              out[ch][7] == source[ch][7] && !IsClearDyeRecord(out[ch]),
              "natural material became template 1 or removed the dye");

    uint8_t black[16]{};
    black[4] = black[5] = black[11] = 0xFF;
    black[10] = 0xFF;
    Check(!IsClearDyeRecord(black), "opaque black with natural material mistaken for clear");
    Check(BuildDyeRetouchRecords(source, present, 6, DyeRetouchMaterial, 2, 0, out) == 0,
          "clear record acquired a material override");
    Check(BuildDyeRetouchRecords(source, present, 5, DyeRetouchMaterial, 2, 0, out) == 0,
          "absent zone acquired a material override");
    Check(BuildDyeRetouchRecords(source, 0, -1, DyeRetouchMaterial, 2, 0, out) == 0,
          "empty dye vector acquired fabricated color");
    Check(BuildDyeRetouchRecords(source, present, 12, DyeRetouchMaterial, 2, 0, out) == 0,
          "out-of-range channel accepted");
    Check(BuildDyeRetouchRecords(source, present, -1, DyeRetouchCondition, 2, 128, out) == 0,
          "condition high-bit sentinel accepted as normal wear");
}
int main()
{
    try { TestMaterialRetouch(); }
    catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 5; }
    uint8_t a[16]{}, b[16]{};
    b[13] = 4; b[14] = 0xA5; b[15] = 0x5A;
    if (!SameDyePayload(a, b)) return 1;
    for (unsigned i = 0; i < 13; ++i)
    {
        b[i] = 1;
        if (SameDyePayload(a, b)) return 2;
        b[i] = 0;
    }
    for (uint32_t mask = 0; mask < 4096; ++mask)
    {
        uint32_t remaining = mask, completed = 0;
        while (remaining)
        {
            const uint32_t next = FirstDyeChannel(remaining);
            if (!next || (next & (next - 1)) || !(next & remaining) || (next & completed)) return 3;
            completed |= next; remaining &= ~next;
        }
        if (completed != mask) return 4;
    }
    puts("Dye payload/partial replay passed; material and condition retouch preserve each zone, natural clears and raw payloads.");
    return 0;
}
