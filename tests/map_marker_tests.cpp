#include "game/map_marker.h"
#include "game/marker_teleport_logic.h"

#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    constexpr uintptr_t global = 0x100000, ui = 0x200000, destination = 0x300000;
    uintptr_t currentUi = ui, currentDestination = destination;
    trinity::game::MapMarkerPosition current{-10691.016602f, 0.0f, -3722.970459f};
    bool readable = true, replaceDuringRead = false;
    int failures = 0;

    struct TeleportMemory
    {
        struct Write { uintptr_t address; trinity::game::MapMarkerPosition value; };
        std::vector<Write> writes;
        trinity::game::MapMarkerPosition position{};
        bool rejectWrites = false;
        bool corruptReadback = false;
    };

    bool WriteTeleportPosition(void* context, uintptr_t address,
                               const trinity::game::MapMarkerPosition& value)
    {
        auto& memory = *static_cast<TeleportMemory*>(context);
        if (memory.rejectWrites) return false;
        memory.writes.push_back({address, value});
        if ((address & 0xFFF) == 0x090)
            memory.position = value;
        return true;
    }

    bool ReadTeleportPosition(void* context, uintptr_t,
                              trinity::game::MapMarkerPosition& value)
    {
        auto& memory = *static_cast<TeleportMemory*>(context);
        value = memory.position;
        if (memory.corruptReadback) value.x += 100.0f;
        return true;
    }

    bool ReadPointer(uintptr_t address, uintptr_t* out)
    {
        if (address == global) { *out = currentUi; return true; }
        if (address == ui + 0xA8) { *out = currentDestination; return true; }
        return false;
    }

    bool ReadPosition(uintptr_t address, float* out)
    {
        if (!readable || address != destination + 0x20) return false;
        out[0] = current.x; out[1] = current.y; out[2] = current.z;
        if (replaceDuringRead) currentDestination = 0x400000;
        return true;
    }

    void Expect(bool condition, const char* message)
    {
        if (!condition) { std::printf("FAIL: %s\n", message); ++failures; }
    }
}

int main()
{
    using namespace trinity::game;
    MapMarkerPosition out{};
    const auto read = [&] { return ReadCurrentMapMarker(global, ReadPointer, ReadPosition, out); };
    Expect(read() && out.x == current.x && out.z == current.z,
           "existing TU 2.01.00 map destination is readable without a capture-hook event");
    current = {-10940.327148f, 0.0f, -3814.648682f};
    Expect(read() && out.x == current.x && out.z == current.z,
           "moving the marker replaces the previous coordinates; zero altitude is valid");
    current = {};
    Expect(!read(), "removing the map destination must not return a cached point");
    current = {1.0f, std::numeric_limits<float>::quiet_NaN(), 2.0f};
    Expect(!read(), "non-finite destination is rejected");
    current = {1.1e9f, 0.0f, 2.0f};
    Expect(!read(), "out-of-range destination is rejected");
    current = {1.0f, 0.0f, 2.0f};
    readable = false;
    Expect(!read(), "unreadable destination is rejected");
    readable = true;
    currentUi = 0;
    Expect(!read(), "missing UI during loading is rejected");
    currentUi = ui;
    currentDestination = 0;
    Expect(!read(), "missing destination state is rejected");
    currentDestination = destination;
    replaceDuringRead = true;
    Expect(read(), "a valid destination remains usable when the game replaces its object after reading");

    constexpr uintptr_t owner = 0x500000, markerPlayer = 0x600000;
    const MapMarkerPosition target{125.0f, 640.0f, -80.0f};
    TeleportMemory memory{};
    Expect(ApplyMarkerTeleportDestination(owner, markerPlayer, target, &memory,
                                           &WriteTeleportPosition, &ReadTeleportPosition),
           "teleport apply reports success only after destination readback matches");
    Expect(memory.writes.size() == 6,
           "teleport apply writes both player positions and clears desired and actual velocity");
    if (memory.writes.size() == 6)
    {
        Expect(memory.writes[0].address == owner + 0x90 &&
               memory.writes[1].address == owner + 0x1A0,
               "move owner destination writes use the two live position fields");
        Expect(memory.writes[2].address == owner + 0xC0 &&
               memory.writes[3].address == owner + 0xD0 &&
               memory.writes[2].value.x == 0.0f && memory.writes[2].value.y == 0.0f &&
               memory.writes[2].value.z == 0.0f && memory.writes[3].value.x == 0.0f &&
               memory.writes[3].value.y == 0.0f && memory.writes[3].value.z == 0.0f,
               "teleport apply clears desired velocity and integrator velocity");
    }

    memory = {};
    memory.corruptReadback = true;
    Expect(!ApplyMarkerTeleportDestination(owner, 0, target, &memory,
                                            &WriteTeleportPosition, &ReadTeleportPosition),
           "teleport apply rejects a write that does not remain at the requested destination");
    memory = {};
    memory.rejectWrites = true;
    Expect(!ApplyMarkerTeleportDestination(owner, 0, target, &memory,
                                            &WriteTeleportPosition, &ReadTeleportPosition),
           "teleport apply reports failed writes instead of a false success");
    if (!failures) std::puts("map marker tests passed");
    return failures ? 1 : 0;
}
