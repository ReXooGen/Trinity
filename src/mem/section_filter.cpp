#include "section_filter.h"

#include <cstring>

namespace trinity::mem
{
    bool ShouldScanSection(const char* name, uint32_t characteristics)
    {
        constexpr uint32_t kMemExecute = 0x20000000u; // IMAGE_SCN_MEM_EXECUTE
        return !name || std::strcmp(name, ".debug") != 0 ||
               (characteristics & kMemExecute) != 0;
    }
}
