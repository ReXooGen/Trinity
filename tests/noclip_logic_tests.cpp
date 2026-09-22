// Unit tests for No Clip's per-tick step maths (src/game/noclip_logic.h).
//
// These cover the rules that keep No Clip inert instead of destructive: it must
// refuse to move anyone when the player holds no direction, when the proxy's
// numbers are not finite, when dt is a frame hitch (or nonsense), and when the
// requested speed exceeds the engine's broadphase ceiling. None of this needs
// the game running - the hook in teleport.cpp is a thin wrapper around exactly
// these decisions.
#include "game/noclip_logic.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }

    namespace game = trinity::game;

    game::NoClipStepInput Base()
    {
        game::NoClipStepInput in{};
        in.currentX = 100.0f;
        in.currentY = 50.0f;
        in.currentZ = -20.0f;
        in.speedPerSecond = 8.0f;
        in.dtSeconds = 1.0f / 60.0f;
        return in;
    }

    bool Near(float a, float b, float tol = 1e-4f)
    {
        return std::fabs(a - b) <= tol;
    }
}

int main()
{
    using namespace trinity::game;

    // --- Idle is truly idle -------------------------------------------------
    // The single most important property: with no direction held, No Clip must
    // not touch the proxy at all. This is what makes the feature safe to leave
    // enabled while the player stands still.
    {
        const NoClipStepResult r = ComputeNoClipStep(Base());
        Expect(!r.valid, "no intent and no vertical bind must produce no step");
    }
    {
        NoClipStepInput in = Base();
        in.intentX = 0.02f;  // stick drift
        in.intentZ = -0.01f;
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(!r.valid, "sub-threshold intent (stick drift) must produce no step");
    }

    // --- Direction comes from the game's own movement intent ----------------
    {
        NoClipStepInput in = Base();
        in.intentX = 5.0f;
        in.intentZ = 0.0f;
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(r.valid, "a clear horizontal intent must produce a step");
        const float expected = 8.0f * (1.0f / 60.0f);
        Expect(Near(r.x - in.currentX, expected), "forward intent advances along +x by speed*dt");
        Expect(Near(r.y, in.currentY), "horizontal-only intent must not change height");
        Expect(Near(r.z, in.currentZ), "horizontal-only intent must not change z");
    }
    {
        // A diagonal heading is normalised: the step LENGTH is speed*dt, so a
        // diagonal cannot move ~41% faster than a cardinal direction.
        NoClipStepInput in = Base();
        in.intentX = 3.0f;
        in.intentZ = -4.0f; // deliberately unequal magnitudes
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(r.valid, "a diagonal intent must produce a step");
        const float dx = r.x - in.currentX;
        const float dy = r.y - in.currentY;
        const float dz = r.z - in.currentZ;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        Expect(Near(len, 8.0f * (1.0f / 60.0f), 1e-3f),
               "step length must equal speed*dt regardless of intent magnitude");
        Expect(Near(dx / len, 0.6f, 1e-3f) && Near(dz / len, -0.8f, 1e-3f),
               "diagonal heading must be normalised, preserving direction");
    }

    // --- Vertical reuses the Free Flight binds ------------------------------
    {
        NoClipStepInput in = Base();
        in.up = true;
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(r.valid, "holding ascend with no horizontal intent must still step (hover climb)");
        Expect(Near(r.x, in.currentX) && Near(r.z, in.currentZ),
               "a pure vertical step must not drift horizontally");
        Expect(r.y > in.currentY, "ascend must increase height");

        in.up = false;
        in.down = true;
        const NoClipStepResult down = ComputeNoClipStep(in);
        Expect(down.valid && down.y < in.currentY, "descend must decrease height");

        // Both held is contradictory input, not a direction: the game's own
        // Free Flight treats that as "neither", and so must this.
        in.up = true;
        const NoClipStepResult both = ComputeNoClipStep(in);
        Expect(!both.valid, "ascend and descend held together must cancel to no step");
    }

    // --- Speed ceiling ------------------------------------------------------
    {
        NoClipStepInput in = Base();
        in.intentX = 1.0f;
        in.speedPerSecond = 500.0f; // far beyond anything the engine survives
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(r.valid, "an over-limit speed must still step, just clamped");
        const float advance = r.x - in.currentX;
        Expect(Near(advance, 35.0f * (1.0f / 60.0f), 1e-3f),
               "speed must be clamped to the engine broadphase ceiling of 35 m/s");
    }

    // --- Frame hitch must never become a teleport ---------------------------
    {
        NoClipStepInput in = Base();
        in.intentX = 1.0f;
        in.dtSeconds = 2.0f; // a two-second stall
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(r.valid, "a long frame still steps");
        const float advance = r.x - in.currentX;
        Expect(Near(advance, 8.0f * 0.1f, 1e-3f),
               "dt must be clamped to maxDtSeconds so a hitch cannot fling the player");
    }

    // --- Rejected inputs ----------------------------------------------------
    {
        NoClipStepInput in = Base();
        in.intentX = 1.0f;
        in.speedPerSecond = 0.0f;
        Expect(!ComputeNoClipStep(in).valid, "a zero speed must produce no step");

        in = Base();
        in.intentX = 1.0f;
        in.speedPerSecond = -5.0f;
        Expect(!ComputeNoClipStep(in).valid, "a negative speed must produce no step");

        in = Base();
        in.intentX = 1.0f;
        in.dtSeconds = 0.0f;
        Expect(!ComputeNoClipStep(in).valid, "a zero dt must produce no step");

        in = Base();
        in.intentX = std::numeric_limits<float>::quiet_NaN();
        Expect(!ComputeNoClipStep(in).valid, "NaN intent must produce no step");

        in = Base();
        in.intentX = 1.0f;
        in.currentY = std::numeric_limits<float>::infinity();
        Expect(!ComputeNoClipStep(in).valid, "a non-finite proxy position must produce no step");

        in = Base();
        in.intentX = std::numeric_limits<float>::max();
        in.intentZ = std::numeric_limits<float>::max();
        const NoClipStepResult r = ComputeNoClipStep(in);
        Expect(!r.valid || NoClipFinite3(r.x, r.y, r.z),
               "an enormous intent must never yield a non-finite destination");
    }

    if (failures == 0)
        std::printf("noclip_logic: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
