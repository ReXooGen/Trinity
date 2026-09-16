#include "game/dye_record.h"
#include <cstdio>
using namespace trinity::game;
int main()
{
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
    puts("Dye payload changes detected; storage-tail differences ignored; partial replay covers every channel exactly once.");
    return 0;
}
