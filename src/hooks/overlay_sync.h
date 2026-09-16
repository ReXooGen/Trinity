#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <cstdint>

namespace trinity::hooks
{
    // Upload buffers and allocators rotate together by submitted overlay count,
    // never by the swapchain's (possibly repeated / FG-managed) buffer index.
    inline constexpr unsigned kOverlayFramesInFlight = 4;
    inline unsigned OverlaySubmissionSlot(uint64_t submitted)
    {
        return static_cast<unsigned>(submitted % kOverlayFramesInFlight);
    }

    inline bool WaitForOverlayFence(ID3D12Fence* fence, HANDLE event, uint64_t value)
    {
        if (!value) return true;
        if (!fence || !event) return false;
        const uint64_t completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) return false; // device removed, not completion
        if (completed >= value) return true;
        if (FAILED(fence->SetEventOnCompletion(value, event))) return false;
        if (WaitForSingleObject(event, 1000) != WAIT_OBJECT_0) return false;
        const uint64_t after = fence->GetCompletedValue();
        return after != UINT64_MAX && after >= value;
    }
}
