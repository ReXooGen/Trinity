#include "game/travel_logic.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

int main()
{
    using namespace trinity::game;

    int sceneId = 6;
    NativeTravelCall call{};
    Expect(PrepareNativeTravelCall(true, sceneId, 142, &call),
           "a resolved trigger accepts a non-negative scene and node");
    Expect(call.context == nullptr,
           "PE 2944 dispatcher ignores RCX and must receive the explicit null context");
    Expect(call.sceneId == 6 && call.nodeIndex == 142,
           "scene and node must retain their native argument order");
    Expect(call.travelMode == 0,
           "PE 2944 dispatcher requires R9D=0 for the ordinary fast-travel flow");

    Expect(!PrepareNativeTravelCall(false, sceneId, 142, &call),
           "an unresolved trigger must fail closed");
    int invalidScene = -1;
    int invalidNode = -1;
    Expect(!PrepareNativeTravelCall(true, invalidScene, 142, &call),
           "a negative scene must be rejected");
    Expect(!PrepareNativeTravelCall(true, sceneId, invalidNode, &call),
           "a negative node must be rejected");
    Expect(!PrepareNativeTravelCall(true, sceneId, 142, nullptr),
           "a missing call-output storage must be rejected");

    return failures == 0 ? 0 : 1;
}
