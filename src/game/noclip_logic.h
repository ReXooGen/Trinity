#pragma once

#include <cmath>

namespace trinity::game
{
    // --- No Clip ------------------------------------------------------------
    // No Clip does not touch the collision system at all. It advances the Havok
    // character proxy's position along the direction the player is already
    // pushing, through the same write path map-marker teleport uses: write
    // position to +0x90 and +0x1A0, zero the desired velocity (+0xC0) and the
    // integrator's frame velocity (+0xD0).
    //
    // The write lands in hkMoveUpdate AFTER the engine's own integrator has run
    // for the frame, so it is the last word on the proxy's position that tick -
    // the collision sweep's correction is overwritten and the proxy ends up
    // inside geometry, which it then keeps doing tick after tick until it is
    // through. That is the whole mechanism.
    //
    // The direction is the game's OWN camera-relative movement intent, read from
    // +0xC0 before the integrator consumes it, so WASD / stick keep working
    // exactly as they normally do and there is no second input path to keep in
    // sync with the engine. Vertical movement reuses the Free Flight binds.
    //
    // Every rejection below is deliberate. If the inputs are not provably safe
    // this returns valid = false and the caller leaves the proxy completely
    // untouched, which is what makes the feature inert rather than destructive
    // when the engine hands us something unexpected.
    struct NoClipStepInput
    {
        // Proxy position (+0x90), world space: x, y = up, z.
        float currentX = 0.0f;
        float currentY = 0.0f;
        float currentZ = 0.0f;

        // Desired velocity (+0xC0) - the game's camera-relative movement intent.
        // Same world basis: [0] = x, [1] = y (up), [2] = z.
        float intentX = 0.0f;
        float intentY = 0.0f;
        float intentZ = 0.0f;

        // Free Flight's ascend / descend binds, reused for vertical noclip.
        bool up = false;
        bool down = false;

        float speedPerSecond = 0.0f; // user setting
        float dtSeconds = 0.0f;      // this movement tick

        // The engine's broadphase overflows above ~35 m/s for a gliding
        // character (teleport.cpp kMaxSafeFlightSpeed) - noclip inherits the
        // same ceiling rather than being a licence to exceed it.
        float maxSpeedPerSecond = 35.0f;

        // Intent below this is treated as "the player is not pushing a
        // direction" - stick drift and idle micro-jitter must not move anyone.
        float minIntent = 0.05f;

        // A frame hitch must never become a teleport: the step is computed from
        // a clamped dt, so a 2-second stall advances the player by one normal
        // tick's worth, not 70 metres into unstreamed terrain.
        float maxDtSeconds = 0.1f;
    };

    struct NoClipStepResult
    {
        bool  valid = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    inline bool NoClipFinite3(float x, float y, float z)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    inline NoClipStepResult ComputeNoClipStep(const NoClipStepInput& in)
    {
        NoClipStepResult out{};

        if (!NoClipFinite3(in.currentX, in.currentY, in.currentZ) ||
            !NoClipFinite3(in.intentX, in.intentY, in.intentZ))
            return out;

        if (!std::isfinite(in.speedPerSecond) || in.speedPerSecond <= 0.0f)
            return out;
        if (!std::isfinite(in.dtSeconds) || in.dtSeconds <= 0.0f)
            return out;
        if (!std::isfinite(in.maxSpeedPerSecond) || in.maxSpeedPerSecond <= 0.0f)
            return out;

        float dt = in.dtSeconds;
        if (in.maxDtSeconds > 0.0f && dt > in.maxDtSeconds)
            dt = in.maxDtSeconds;

        float speed = in.speedPerSecond;
        if (speed > in.maxSpeedPerSecond)
            speed = in.maxSpeedPerSecond;

        // Horizontal heading is the player's own movement intent; vertical is
        // the Free Flight binds. A zero intent with no vertical held means the
        // player is not asking to move, so we do nothing at all.
        float dx = in.intentX;
        float dz = in.intentZ;
        const float horizSq = dx * dx + dz * dz;

        float dy = 0.0f;
        if (in.up && !in.down)
            dy = 1.0f;
        else if (in.down && !in.up)
            dy = -1.0f;

        const float minIntent = (in.minIntent > 0.0f) ? in.minIntent : 0.0f;
        const bool hasHoriz = horizSq > (minIntent * minIntent);
        if (!hasHoriz && dy == 0.0f)
            return out;

        if (hasHoriz)
        {
            const float invLen = 1.0f / std::sqrt(horizSq);
            dx *= invLen;
            dz *= invLen;
        }
        else
        {
            dx = 0.0f;
            dz = 0.0f;
        }

        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(len) || len <= 0.0f)
            return out;

        const float advance = speed * dt;
        const float nx = in.currentX + (dx / len) * advance;
        const float ny = in.currentY + (dy / len) * advance;
        const float nz = in.currentZ + (dz / len) * advance;

        if (!NoClipFinite3(nx, ny, nz))
            return out;

        out.valid = true;
        out.x = nx;
        out.y = ny;
        out.z = nz;
        return out;
    }
}
