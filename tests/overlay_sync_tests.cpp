#include "hooks/overlay_sync.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace trinity::hooks;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void Ok(HRESULT hr, const char* message) { Check(SUCCEEDED(hr), message); }

int main()
{
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    try
    {
        Check(event != nullptr, "event creation failed");
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        ComPtr<ID3D12Device> device;
        Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory failed");
        Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP unavailable");
        Ok(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "WARP D3D12 failed");
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC q{};
        Ok(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue creation failed");
        ComPtr<ID3D12Fence> fence, gate;
        Ok(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence creation failed");
        Ok(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "gate creation failed");
        ComPtr<ID3D12CommandAllocator> allocators[kOverlayFramesInFlight];
        ComPtr<ID3D12Resource> uploads[kOverlayFramesInFlight], readback;
        uint64_t retired[kOverlayFramesInFlight]{};
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = 1024; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        for (unsigned i = 0; i < kOverlayFramesInFlight; ++i)
        {
            Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])), "allocator creation failed");
            Ok(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr, IID_PPV_ARGS(&uploads[i])), "upload creation failed");
        }
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        Ok(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                           nullptr, IID_PPV_ARGS(&readback)), "readback creation failed");
        ComPtr<ID3D12GraphicsCommandList> list;
        Ok(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(), nullptr, IID_PPV_ARGS(&list)), "list creation failed");
        Ok(list->Close(), "initial close failed");

        // Hold GPU work until the ring wraps. All first four submissions must
        // survive: the old busy-back-buffer early-return would omit frames.
        Ok(queue->Wait(gate.Get(), 1), "queue gate failed");
        bool waitPassed = false;
        constexpr unsigned frames = 12;
        const unsigned backBuffers[frames] = {0, 0, 1, 0, 1, 1, 0, 2, 0, 2, 1, 0};
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            const unsigned slot = OverlaySubmissionSlot(frame);
            (void)backBuffers[frame]; // RT selection never selects an upload/allocator
            Check(slot == frame % kOverlayFramesInFlight, "submission slot mismatch");
            if (frame == kOverlayFramesInFlight)
            {
                Check(fence->GetCompletedValue() < retired[slot], "GPU gate did not hold reuse");
                std::thread release([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    gate->Signal(1);
                });
                waitPassed = WaitForOverlayFence(fence.Get(), event, retired[slot]);
                release.join();
                Check(waitPassed, "delayed fence did not retire");
            }
            else Check(WaitForOverlayFence(fence.Get(), event, retired[slot]), "ring fence wait failed");
            Ok(allocators[slot]->Reset(), "allocator reset before retirement");
            Ok(list->Reset(allocators[slot].Get(), nullptr), "list reset failed");
            void* data = nullptr;
            D3D12_RANGE noRead{0, 0};
            Ok(uploads[slot]->Map(0, &noRead, &data), "upload map failed");
            const uint32_t value = 0x12340000u + frame;
            memcpy(data, &value, sizeof(value));
            uploads[slot]->Unmap(0, nullptr);
            list->CopyBufferRegion(readback.Get(), frame * sizeof(value), uploads[slot].Get(), 0, sizeof(value));
            Ok(list->Close(), "list close failed");
            ID3D12CommandList* work[] = {list.Get()};
            queue->ExecuteCommandLists(1, work);
            Ok(queue->Signal(fence.Get(), frame + 1), "queue signal failed");
            retired[slot] = frame + 1;
        }
        Check(waitPassed && WaitForOverlayFence(fence.Get(), event, frames), "final retirement failed");
        void* data = nullptr;
        D3D12_RANGE readRange{0, frames * sizeof(uint32_t)};
        Ok(readback->Map(0, &readRange, &data), "readback map failed");
        bool intact = true;
        for (unsigned i = 0; i < frames; ++i)
            intact &= static_cast<uint32_t*>(data)[i] == 0x12340000u + i;
        readback->Unmap(0, nullptr);
        Check(intact, "missing submission or overwritten in-flight upload data");
        Check(!WaitForOverlayFence(nullptr, event, 1), "missing fence accepted");
        Check(!WaitForOverlayFence(fence.Get(), nullptr, frames + 1), "missing wait event accepted");
        SetEvent(event); // stale event does not prove the requested fence retired
        Check(!WaitForOverlayFence(gate.Get(), event, 2), "stale event accepted as GPU completion");
        Ok(gate->Signal(2), "release stale-event test gate");
        puts("WARP: all 12 delayed submissions preserved; allocator/upload ring retired before reuse.");
        CloseHandle(event);
        return 0;
    }
    catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); if (event) CloseHandle(event); return 1; }
}
