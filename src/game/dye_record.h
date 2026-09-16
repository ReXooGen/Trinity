#pragma once
#include <cstdint>
#include <cstring>

namespace trinity::game
{
    // TU 2.02 native upsert 1423558AE..1423558EB copies exactly bytes 0..12
    // when replacing a channel. Comparing the untouched storage tail 13..15
    // with the requested record can therefore cause endless replay.
    inline bool SameDyePayload(const uint8_t* a, const uint8_t* b)
    {
        return std::memcmp(a, b, 13) == 0;
    }
    inline uint32_t FirstDyeChannel(uint32_t mask)
    {
        return mask & (~mask + 1u);
    }

    inline bool IsClearDyeRecord(const uint8_t* rec)
    {
        return rec[4] == 0xFF && rec[5] == 0xFF && !rec[7] && !rec[8] && !rec[9] &&
            !rec[10] && (rec[11] & 0x80) && !rec[12];
    }

    enum DyeRetouchField : unsigned { DyeRetouchMaterial = 1, DyeRetouchCondition = 2 };

    // Preserve each existing zone's own color, group, alpha and other payload.
    // Missing/explicitly cleared zones have no color to preserve: do not invent
    // a black/white override merely because the material control was touched.
    inline uint32_t BuildDyeRetouchRecords(const uint8_t (&source)[12][16], uint32_t sourceMask,
                                          int channel, unsigned fields, uint16_t material, uint8_t condition,
                                          uint8_t (&out)[12][16])
    {
        std::memset(out, 0, sizeof(out));
        if (channel < -1 || channel >= 12 || !fields ||
            (fields & ~(DyeRetouchMaterial | DyeRetouchCondition)) || (sourceMask & ~0xFFFu) ||
            ((fields & DyeRetouchCondition) && condition > 127)) return 0;
        const uint32_t selected = sourceMask & (channel < 0 ? 0xFFFu : 1u << channel);
        uint32_t mask = 0;
        for (unsigned ch = 0; ch < 12; ++ch)
        {
            if (!(selected & (1u << ch)) || source[ch][6] != ch || IsClearDyeRecord(source[ch])) continue;
            std::memcpy(out[ch], source[ch], 13);
            if (fields & DyeRetouchMaterial)
            {
                out[ch][4] = static_cast<uint8_t>(material);
                out[ch][5] = static_cast<uint8_t>(material >> 8);
            }
            if (fields & DyeRetouchCondition) out[ch][11] = condition;
            mask |= 1u << ch;
        }
        return mask;
    }
}
