#pragma once

namespace trinity::game
{
    // PE 1.0.0.2760 reads argument 2 as a C-string pointer immediately after
    // entry. Keeping it pointer-sized is load-bearing: declaring it uint32_t
    // truncates 64-bit game addresses when the detour forwards to the original.
    using RegisterCrimeEvent_t = void(__fastcall*)(void* dispatcher,
                                                   const char* eventName,
                                                   void* eventData,
                                                   void* eventContext);
}
