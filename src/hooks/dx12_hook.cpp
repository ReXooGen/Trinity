#include "dx12_hook.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <vector>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "input.h"
#include "xinput_hook.h"
#include "hdr_composite_shader.h"
#include "../core/logger.h"
#include "../core/settings.h"
#include "../core/state.h"
#include "../game/player.h"
#include "../game/teleport.h"
#include "../gui/framework.h"
#include "../gui/icons.h"
#include "../gui/menu.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace trinity::hooks
{
    // --- Original function pointers -----------------------------------------
    using Present_t         = HRESULT (WINAPI*)(IDXGISwapChain3*, UINT, UINT);
    using ResizeBuffers_t   = HRESULT (WINAPI*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ExecuteCmdLists_t = void    (WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
    using SetColorSpace1_t  = HRESULT (WINAPI*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);

    static Present_t         oPresent             = nullptr;
    static ResizeBuffers_t   oResizeBuffers       = nullptr;
    static ExecuteCmdLists_t oExecuteCommandLists = nullptr;
    static SetColorSpace1_t  oSetColorSpace1      = nullptr;

    // --- Frame Generation (DLSS-G) support ----------------------------------
    // To draw over DLSS Frame Generation we must render on the swapchain the
    // GAME presents to - the writable, pre-interpolation one - NOT the native
    // swapchain Streamline pushes finished frames through (its buffers are
    // read-only to us => DXGI_ERROR_ACCESS_DENIED, device removed).
    //
    // The proven approach (OptiScaler / ReShade): WRAP, don't detour. We return a
    // WrappedIDXGISwapChain to the game that forwards every method to the real
    // (Streamline-proxy) swapchain but intercepts Present/Present1 to draw the
    // overlay first. The wrapper is handed out by a VirtualProtect vtable-slot
    // patch of the factory's CreateSwapChain*/CreateSwapChainForHwnd - NOT a
    // MinHook detour of the DXGI exports (that deadlocked startup and fought
    // Streamline's own interposer). See InstallSwapChainCreationPatch.
    using CSFH_t = HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

    static CSFH_t oFactoryCreateSwapChainForHwnd = nullptr;

    // Set once a swapchain has been wrapped: the wrapper now does all overlay
    // drawing, so the native Present byte-hook must stop drawing (its buffers are
    // Streamline's read-only finished frames). Never cleared for the session.
    static bool g_wrapperActive = false;

    // Reentrancy guard for swapchain creation. When the game calls our patched
    // CreateSwapChainForHwnd, the real implementation (Streamline's interposer)
    // can itself create swapchains through the SAME class vtable slot we patched -
    // and it does exactly this every time Frame Generation is toggled, since SL is
    // spec-required to tear down and recreate the swapchain on an FG on/off. Only
    // the OUTERMOST call (the one the game made) may be wrapped; nested internal
    // ones are Streamline's own plumbing and must pass straight through, or we
    // recurse into the interposer mid-(re)creation and hang / remove the device.
    // This is the crash that appears "after a couple loads": the first swapchain
    // wraps cleanly, then an FG toggle re-enters us during SL's rebuild.
    static thread_local bool t_inSwapChainCreate = false;

    // The window our wrapped (main) swapchain belongs to. The engine and Streamline
    // can spin up auxiliary swapchains on other windows; we only ever want the one
    // the game renders the world into. Locked on the first successful wrap so later
    // auxiliary creations are ignored.
    static HWND g_wrappedHwnd = nullptr;

    // --- Rendering resources ------------------------------------------------
    struct FrameContext
    {
        ID3D12CommandAllocator*     commandAllocator = nullptr;
        ID3D12Resource*             renderTarget     = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle        = {};
        UINT64                      fenceValue       = 0; // GPU signal for this frame's overlay work
    };

    static ID3D12Device*              g_device      = nullptr;
    static ID3D12DescriptorHeap*      g_rtvHeap     = nullptr;
    static ID3D12DescriptorHeap*      g_srvHeap     = nullptr;
    static ID3D12GraphicsCommandList* g_commandList = nullptr;
    // SRV heap capacity: slot 0 ImGui font, slots 1-3 the fixed UI atlases,
    // the rest a session budget for lazily-loaded per-item icons (~64KB of
    // GPU memory each; see icons.cpp).
    //
    // Raised from 512 for Inventory -> Add Item: that browses the game's WHOLE
    // item catalog rather than just what you carry, so the number of distinct
    // icons a session can touch went from "a few hundred at most" to thousands.
    // Icons are loaded on demand and never evicted, so the old budget ran out
    // partway through browsing and every icon after it - including the ones in
    // the Editor - silently drew blank for the rest of the session.
    //
    // The heap itself is cheap (a descriptor is 32 bytes; this is ~128KB). The
    // real cost is one texture per icon actually looked at, so a session that
    // never opens Add Item pays nothing extra. If this is ever exhausted again
    // the answer is LRU eviction, not another bump.
    static constexpr UINT             kSrvHeapSlots = 4096;
    // The most-recent DIRECT queue confirmed to live on the swapchain's device
    // (i.e. the present queue). Published ONLY from hkExecuteCommandLists while
    // the queue argument is guaranteed alive, and AddRef'd - we never keep
    // un-refcounted queue pointers (the engine destroys temporary queues during
    // loading; a cached raw pointer is a use-after-free).
    static ID3D12CommandQueue*        g_presentQueue   = nullptr; // guarded by g_queueLock, AddRef'd
    static CRITICAL_SECTION           g_queueLock;
    static bool                       g_queueLockReady = false;
    // Set once we have the AUTHORITATIVE present queue - the one passed to
    // CreateSwapChainForHwnd, which for D3D12 IS the swapchain's present queue and
    // owns the back buffers. Once pinned, the ExecuteCommandLists heuristic must not
    // replace it: under Multi Frame Generation the busiest DIRECT queue there is
    // Streamline's present pacer, and submitting our overlay on the pacer queue is
    // rejected (ACCESS_DENIED -> device removed). This was the instant-crash cause.
    static bool                       g_presentQueuePinned = false;
    static ID3D12Fence*               g_fence          = nullptr;
    static HANDLE                     g_fenceEvent     = nullptr;
    static UINT64                     g_fenceValue     = 0;
    static std::vector<FrameContext>  g_frames;
    static UINT                       g_bufferCount    = 0;
    // The swapchain our render targets currently describe. Compared by identity
    // each Present so we can rebuild when Streamline (DLSS Frame Generation)
    // silently swaps the native swapchain out from under us - see
    // ReconcileSwapChain. Raw pointer: used ONLY for the identity compare,
    // never dereferenced when stale.
    static IDXGISwapChain3*           g_swapChain      = nullptr;
    static HWND                       g_hwnd           = nullptr;
    // Full back-buffer signature our render targets currently describe. A video
    // settings change (resolution, HDR) resizes the buffers IN PLACE - same
    // swapchain pointer, often the same count - so pointer+count alone can't tell
    // us the buffers moved. Track width/height/format too and rebuild on any
    // change, or our RTVs keep pointing at freed buffers => ACCESS_DENIED.
    static UINT                       g_scWidth        = 0;
    static UINT                       g_scHeight       = 0;
    static DXGI_FORMAT                g_scFormat       = DXGI_FORMAT_UNKNOWN;
    static bool                       g_imguiReady     = false;
    static bool                       g_initFailed     = false;
    static bool                       g_renderDisabled = false;

    // --- HDR-aware overlay compositing ---------------------------------------
    // ImGui always renders into an SDR (R8G8B8A8_UNORM, plain sRGB-gamma
    // numeric) offscreen target - decoupled from whatever pixel format/color
    // space the real back buffer is actually in. A small composite pass then
    // re-encodes that image into the back buffer's ACTUAL color space (scRGB
    // linear or HDR10 PQ) and alpha-blends it in. Without this an HDR back
    // buffer reads our plain 0-1 UI colors as if they were scene-referred /
    // PQ-encoded values directly, and the menu comes out blown-out and
    // oversaturated - the bug this whole section exists to fix.
    static ID3D12Resource*             g_offscreenTex     = nullptr;
    static ID3D12DescriptorHeap*       g_offscreenRtvHeap = nullptr;
    static D3D12_CPU_DESCRIPTOR_HANDLE g_offscreenRtv     = {};
    static UINT                        g_offscreenW       = 0;
    static UINT                        g_offscreenH       = 0;

    // Fixed SRV heap slot for the offscreen texture, reserved right after the
    // ImGui font (slot 0). icons.cpp's fixed atlases + item-icon budget are
    // pushed one slot later to make room (see the IconsInit call below).
    static constexpr UINT kOffscreenSrvSlot = 1;

    static ID3D12RootSignature* g_compositeRootSig = nullptr;
    static ID3D12PipelineState* g_compositePSO     = nullptr;
    static DXGI_FORMAT          g_compositeFormat  = DXGI_FORMAT_UNKNOWN;

    // SDR reference white targeted inside the HDR range (ITU-R BT.2408's
    // recommended "graphics white" for HDR overlays/subtitles). Not the peak
    // brightness - just how bright plain white UI text reads next to the
    // game's own HDR highlights.
    static constexpr float kPaperWhiteNits = 203.0f;

    // The color space the REAL swap chain is currently presenting in. DXGI has
    // no "get current color space" query - the only way to know it is to watch
    // every IDXGISwapChain3::SetColorSpace1 call the engine makes (native path
    // hkSetColorSpace1, wrapped path WrappedIDXGISwapChain::SetColorSpace1).
    // Defaults to plain SDR so nothing changes for players without HDR enabled,
    // or before the engine has told us otherwise.
    static DXGI_COLOR_SPACE_TYPE g_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    // 0 = SDR passthrough (byte-identical to drawing straight on the back
    // buffer), 1 = scRGB linear, 2 = HDR10 / ST.2084 PQ. Any other color space
    // (e.g. wide-gamut SDR) falls back to passthrough rather than guessing.
    static UINT CompositeModeForColorSpace(DXGI_COLOR_SPACE_TYPE cs)
    {
        switch (cs)
        {
            case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:    return 1;
            case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return 2;
            default:                                         return 0;
        }
    }

    static void OnColorSpaceChanged(DXGI_COLOR_SPACE_TYPE cs)
    {
        if (cs == g_colorSpace) return;
        g_colorSpace = cs;
        const char* name = CompositeModeForColorSpace(cs) == 2 ? "HDR10 (PQ)"
                          : CompositeModeForColorSpace(cs) == 1 ? "scRGB (linear)"
                          : "SDR";
        LOG("Swapchain color space changed to %s (%d) - overlay compositing %s.",
            name, static_cast<int>(cs),
            CompositeModeForColorSpace(cs) != 0 ? "HDR-adjusted" : "unadjusted");
    }

    // ------------------------------------------------------------------------
    static void CleanupRenderTargets()
    {
        for (auto& f : g_frames)
        {
            if (f.renderTarget) { f.renderTarget->Release(); f.renderTarget = nullptr; }
        }
    }

    static bool CreateRenderTargets(IDXGISwapChain3* swapChain)
    {
        const UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < g_bufferCount; ++i)
        {
            ID3D12Resource* buffer = nullptr;
            if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer))))
                return false;

            g_device->CreateRenderTargetView(buffer, nullptr, handle);
            g_frames[i].renderTarget = buffer;
            g_frames[i].rtvHandle    = handle;
            handle.ptr += rtvSize;
        }
        return true;
    }

    static void ReleaseOffscreenTarget()
    {
        if (g_offscreenTex)     { g_offscreenTex->Release();     g_offscreenTex     = nullptr; }
        if (g_offscreenRtvHeap) { g_offscreenRtvHeap->Release(); g_offscreenRtvHeap = nullptr; }
        g_offscreenW = 0;
        g_offscreenH = 0;
    }

    // (Re)creates the offscreen SDR target ImGui renders into, sized to match
    // the real back buffer. A no-op when the size hasn't changed. Requires
    // g_srvHeap to already exist (its SRV lands at the reserved fixed slot).
    static bool CreateOffscreenTarget(UINT width, UINT height)
    {
        if (g_offscreenTex && width == g_offscreenW && height == g_offscreenH)
            return true;

        ReleaseOffscreenTarget();
        if (width == 0 || height == 0)
            return false;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width;
        desc.Height           = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = desc.Format; // Color left {0,0,0,0} - matches the per-frame clear.

        // Explicit initial state, not COMMON - this is a fresh resource we own
        // outright, and DrawOverlay's first barrier transitions FROM this state.
        if (FAILED(g_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                IID_PPV_ARGS(&g_offscreenTex))))
            return false;
        g_offscreenTex->SetName(L"TrinityOverlayOffscreen");

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_offscreenRtvHeap))))
        {
            ReleaseOffscreenTarget();
            return false;
        }
        g_offscreenRtv = g_offscreenRtvHeap->GetCPUDescriptorHandleForHeapStart();
        g_device->CreateRenderTargetView(g_offscreenTex, nullptr, g_offscreenRtv);

        const UINT inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srv = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srv.ptr += static_cast<SIZE_T>(kOffscreenSrvSlot) * inc;
        g_device->CreateShaderResourceView(g_offscreenTex, nullptr, srv);

        g_offscreenW = width;
        g_offscreenH = height;
        return true;
    }

    // Builds the composite pass's PSO for the given back-buffer format (cheap:
    // no textures, no font atlas - just a tiny fullscreen-triangle pipeline) and
    // caches it. Rebuilds only when the format actually changes (HDR toggle,
    // which reallocates the swap chain with a different back-buffer format).
    // The root signature never depends on format, so it's created once and reused.
    static bool CreateCompositePipeline(DXGI_FORMAT rtvFormat)
    {
        if (!g_compositeRootSig)
        {
            D3D12_DESCRIPTOR_RANGE srvRange = {};
            srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors                     = 1;
            srvRange.BaseShaderRegister                 = 0;
            srvRange.OffsetInDescriptorsFromTableStart  = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2] = {};
            params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues  = 4; // uint mode; float paperWhiteNits; float2 pad;
            params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

            params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges  = 1;
            params[1].DescriptorTable.pDescriptorRanges    = &srvRange;
            params[1].ShaderVisibility                     = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.NumParameters     = 2;
            rsDesc.pParameters       = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers   = &sampler;

            ID3DBlob* sig = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT serialized = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
            if (err) err->Release();
            if (FAILED(serialized))
                return false;

            const HRESULT created = g_device->CreateRootSignature(
                0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_compositeRootSig));
            sig->Release();
            if (FAILED(created))
                return false;
        }

        if (g_compositePSO) { g_compositePSO->Release(); g_compositePSO = nullptr; }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = g_compositeRootSig;
        pso.VS = { g_hdrCompositeVS, sizeof(g_hdrCompositeVS) };
        pso.PS = { g_hdrCompositePS, sizeof(g_hdrCompositePS) };
        // Same blend equation ImGui itself uses - straight (non-premultiplied)
        // alpha, so compositing the offscreen sprite reproduces exactly what
        // drawing ImGui directly onto the back buffer would have (mode 0).
        pso.BlendState.RenderTarget[0].BlendEnable    = TRUE;
        pso.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.SampleMask                      = 0xFFFFFFFFu;
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable   = FALSE;
        pso.DepthStencilState.StencilEnable = FALSE;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets      = 1;
        pso.RTVFormats[0]         = rtvFormat;
        pso.SampleDesc.Count      = 1;

        return SUCCEEDED(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_compositePSO)));
    }

    static bool EnsureCompositePipeline(DXGI_FORMAT rtvFormat)
    {
        if (g_compositePSO && rtvFormat == g_compositeFormat)
            return true;
        if (!CreateCompositePipeline(rtvFormat))
            return false;
        g_compositeFormat = rtvFormat;
        return true;
    }

    // Block until our last overlay submit has retired so it is safe to release
    // the resources it referenced. Cheap when nothing is in flight.
    static void WaitForOverlayIdle()
    {
        if (g_fence && g_fenceValue && g_fenceEvent &&
            g_fence->GetCompletedValue() < g_fenceValue)
        {
            g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, 1000);
        }
    }

    // Rebuild the RTV heap and per-frame command allocators for a new back-buffer
    // count. The caller must have flushed our overlay work first (WaitForOverlayIdle)
    // and must call CreateRenderTargets afterwards to repopulate the views.
    static bool ResizeFrameResources(UINT newCount)
    {
        CleanupRenderTargets();
        for (auto& f : g_frames)
            if (f.commandAllocator) { f.commandAllocator->Release(); f.commandAllocator = nullptr; }
        if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }

        g_bufferCount = newCount;
        g_frames.clear();
        g_frames.resize(newCount);

        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d.NumDescriptors = newCount;
        d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_rtvHeap))))
        {
            LOG_ERR("ResizeFrameResources: RTV heap (%u) creation failed.", newCount);
            return false;
        }

        for (UINT i = 0; i < newCount; ++i)
        {
            if (FAILED(g_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&g_frames[i].commandAllocator))))
            {
                LOG_ERR("ResizeFrameResources: command allocator %u creation failed.", i);
                return false;
            }
        }
        return true;
    }

    // Keep our render targets pinned to whatever swapchain the game is actually
    // presenting. Streamline's DLSS Frame Generation destroys and recreates the
    // native swapchain when FG is toggled - a new pointer, and usually a larger
    // back-buffer count - WITHOUT routing through ResizeBuffers. Left unhandled,
    // our cached RTVs point at freed buffers and GetCurrentBackBufferIndex() can
    // run past g_frames => device removed. Detect a change by the full back-buffer
    // signature (identity, count, size, format) and rebuild in place. Returns false
    // (skip this frame) if the rebuild fails.
    static bool ReconcileSwapChain(IDXGISwapChain3* swapChain)
    {
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (FAILED(swapChain->GetDesc(&desc)))
            return false;

        const UINT        newW   = desc.BufferDesc.Width;
        const UINT        newH   = desc.BufferDesc.Height;
        const DXGI_FORMAT newFmt = desc.BufferDesc.Format;

        // Compare the FULL back-buffer signature, not just pointer + count. A video
        // settings change (resolution, HDR on/off) reallocates the buffers in place
        // with the same swapchain and often the same count - detecting only
        // pointer/count leaves us drawing into freed buffers (ACCESS_DENIED). This
        // is what crashed on entering / applying game settings.
        if (swapChain == g_swapChain && desc.BufferCount == g_bufferCount &&
            newW == g_scWidth && newH == g_scHeight && newFmt == g_scFormat)
            return true; // unchanged - the common path

        // A different D3D device would invalidate our heaps/fence entirely; that
        // never happens for a DLSS-G toggle (Streamline reuses the device), so
        // rather than attempt a full teardown here we just skip the frame.
        ID3D12Device* dev = nullptr;
        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&dev))))
        {
            const bool sameDevice = (dev == g_device);
            dev->Release();
            if (!sameDevice)
            {
                static bool s_warned = false;
                if (!s_warned) { s_warned = true; LOG_ERR("Swapchain device changed - overlay paused this frame."); }
                return false;
            }
        }

        // Our last submit referenced the old buffers/allocators - retire it first.
        WaitForOverlayIdle();

        if (desc.BufferCount != g_bufferCount)
        {
            if (!ResizeFrameResources(desc.BufferCount))
                return false;
        }
        else
        {
            CleanupRenderTargets(); // same count, fresh buffers
        }

        g_hwnd      = desc.OutputWindow;
        g_swapChain = swapChain;
        g_scWidth   = newW;
        g_scHeight  = newH;
        g_scFormat  = newFmt;

        if (!CreateRenderTargets(swapChain))
        {
            LOG_ERR("ReconcileSwapChain: render target rebuild failed.");
            return false;
        }

        // ImGui's own DX12 backend is baked for a FIXED SDR format (see
        // InitImGui) and never needs rebuilding here - only the offscreen
        // target it draws into has to track the back buffer's size.
        if (!CreateOffscreenTarget(g_scWidth, g_scHeight))
        {
            LOG_ERR("ReconcileSwapChain: offscreen target rebuild failed.");
            return false;
        }

        LOG("Swapchain reconciled (%ux%u, %u buffers, fmt %d) - in-place reconfigure.",
            g_scWidth, g_scHeight, g_bufferCount, static_cast<int>(g_scFormat));
        return true;
    }

    static bool InitImGui(IDXGISwapChain3* swapChain)
    {
        // We only get here in the process that actually presents - open the
        // console now and flush the buffered startup logs into it, and claim
        // Trinity.ini so the launcher's copy of the ASI can never save over us.
        Logger::EnableConsole(State::Get().showConsole, State::Get().fileLogging);
        Settings::ClaimOwnership();

        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device))))
        {
            LOG_ERR("InitImGui: swapChain->GetDevice failed.");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc = {};
        swapChain->GetDesc(&desc);
        g_hwnd        = desc.OutputWindow;
        g_bufferCount = desc.BufferCount;
        g_scWidth     = desc.BufferDesc.Width;
        g_scHeight    = desc.BufferDesc.Height;
        g_scFormat    = desc.BufferDesc.Format;
        g_frames.clear();
        g_frames.resize(g_bufferCount);

        // NOTE: no queue selection here. Queues seen before init may already be
        // destroyed (the engine creates temporary devices/queues during loading),
        // so touching them is a use-after-free. The present queue is captured
        // live in hkExecuteCommandLists once g_device is known; DrawOverlay
        // simply skips frames until that has happened (typically 1 frame).

        // Shader-visible SRV heap: slot 0 is the ImGui font atlas, slots 1+
        // our own icon-atlas textures, then the per-item icon budget.
        {
            D3D12_DESCRIPTOR_HEAP_DESC d = {};
            d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            d.NumDescriptors = kSrvHeapSlots;
            d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_srvHeap))))
            {
                LOG_ERR("InitImGui: SRV heap creation failed.");
                return false;
            }
        }

        // RTV heap, one descriptor per back buffer.
        {
            D3D12_DESCRIPTOR_HEAP_DESC d = {};
            d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d.NumDescriptors = g_bufferCount;
            d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_rtvHeap))))
            {
                LOG_ERR("InitImGui: RTV heap creation failed.");
                return false;
            }
        }

        // One command allocator per frame + a single command list.
        for (UINT i = 0; i < g_bufferCount; ++i)
        {
            if (FAILED(g_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&g_frames[i].commandAllocator))))
            {
                LOG_ERR("InitImGui: command allocator %u creation failed.", i);
                return false;
            }
        }

        if (FAILED(g_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_frames[0].commandAllocator, nullptr,
                IID_PPV_ARGS(&g_commandList))))
        {
            LOG_ERR("InitImGui: command list creation failed.");
            return false;
        }
        g_commandList->Close();
        // Name it so DRED (DumpDred) can tell a fault in OUR overlay submit apart
        // from a Streamline / engine command list.
        g_commandList->SetName(L"TrinityOverlayCmdList");

        // Fence so we never reset an allocator the GPU is still using.
        if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        {
            LOG_ERR("InitImGui: fence creation failed.");
            return false;
        }
        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent)
        {
            LOG_ERR("InitImGui: fence event creation failed.");
            return false;
        }

        if (!CreateRenderTargets(swapChain))
        {
            LOG_ERR("InitImGui: render target creation failed.");
            return false;
        }

        if (!CreateOffscreenTarget(g_scWidth, g_scHeight))
        {
            LOG_ERR("InitImGui: offscreen target creation failed.");
            return false;
        }

        // ImGui.
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't litter the game folder

        ui::InitStyle((static_cast<float>(desc.BufferDesc.Height) / 1080.0f) * State::Get().menuScale);

        ImGui_ImplWin32_Init(g_hwnd);
        // Always a fixed SDR format, independent of the real back buffer's
        // format/color space - see the "HDR-aware overlay compositing" comment
        // above. The composite pass (EnsureCompositePipeline) is what actually
        // targets g_scFormat.
        ImGui_ImplDX12_Init(
            g_device, g_bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, g_srvHeap,
            g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            g_srvHeap->GetGPUDescriptorHandleForHeapStart());

        // Load the game's UI icon atlases into the SRV heap (slots 2+; slot 0 is
        // the ImGui font, slot 1 is reserved for the HDR composite offscreen
        // SRV - see kOffscreenSrvSlot). Best effort: if the paks can't be read
        // the menu just falls back to text.
        {
            const UINT inc = g_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ui::IconsInit(g_device, g_srvHeap, inc, 2, kSrvHeapSlots);
        }

        input::Init(g_hwnd);

        g_swapChain = swapChain; // baseline for ReconcileSwapChain

        LOG_OK("Overlay ready - %ux%u, %u back buffers.",
               desc.BufferDesc.Width, desc.BufferDesc.Height, g_bufferCount);
        return true;
    }

    // --- Hooks --------------------------------------------------------------
    // Adopt q as the queue we submit overlay work on. authoritative=true is for the
    // queue passed to CreateSwapChainForHwnd - for D3D12 that first argument IS the
    // swapchain's present queue, the one that owns the back buffers - which we pin so
    // the ExecuteCommandLists heuristic can never later swap in Streamline's MFG
    // present-pacer queue (submitting there is rejected -> device removed).
    static void PublishPresentQueue(ID3D12CommandQueue* q, bool authoritative)
    {
        if (!q || !g_queueLockReady) return;
        EnterCriticalSection(&g_queueLock);
        if (authoritative || !g_presentQueuePinned)
        {
            if (q != g_presentQueue)
            {
                q->AddRef();
                if (g_presentQueue) g_presentQueue->Release();
                g_presentQueue = q;
            }
            if (authoritative && !g_presentQueuePinned)
            {
                g_presentQueuePinned = true;
                LOG("Present queue pinned from swapchain creation - owns the back buffers.");
            }
        }
        LeaveCriticalSection(&g_queueLock);
    }

    static void WINAPI hkExecuteCommandLists(
        ID3D12CommandQueue* queue, UINT numLists, ID3D12CommandList* const* lists)
    {
        // Fallback queue discovery ONLY until the authoritative present queue is
        // pinned from swapchain creation. Once pinned, never touch it here - under
        // MFG the busiest DIRECT queue is Streamline's pacer, not ours.
        if (!g_presentQueuePinned && g_queueLockReady && g_device && queue &&
            queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            EnterCriticalSection(&g_queueLock);
            if (queue != g_presentQueue)
            {
                ID3D12Device* d = nullptr;
                if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&d))))
                {
                    if (d == g_device)
                    {
                        // New most-recent DIRECT queue on our device: take a ref,
                        // drop the ref on the previous one.
                        queue->AddRef();
                        if (g_presentQueue)
                            g_presentQueue->Release();
                        g_presentQueue = queue;

                        static bool s_logged = false;
                        if (!s_logged)
                        {
                            s_logged = true;
                            LOG("Present queue captured (heuristic - creation queue not yet seen).");
                        }
                    }
                    d->Release();
                }
            }
            LeaveCriticalSection(&g_queueLock);
        }

        oExecuteCommandLists(queue, numLists, lists);
    }

    // Records + submits the overlay. Kept as its own function so hkPresent can
    // wrap the call in __try/__except without object-unwinding conflicts.
    static void DrawOverlay(IDXGISwapChain3* swapChain)
    {
        // Pin our render targets to the live swapchain before touching them.
        // A DLSS Frame Generation toggle silently swaps it; if the rebuild
        // fails, sit this frame out rather than draw into freed buffers.
        if (!ReconcileSwapChain(swapChain))
            return;

        if (!EnsureCompositePipeline(g_scFormat))
            return;

        // Hold our own ref on the present queue for the duration of this frame
        // so a concurrent republish can't pull it out from under us. Not
        // captured yet? Skip - it arrives within a frame via
        // hkExecuteCommandLists.
        ID3D12CommandQueue* submitQueue = nullptr;
        EnterCriticalSection(&g_queueLock);
        if (g_presentQueue)
        {
            submitQueue = g_presentQueue;
            submitQueue->AddRef();
        }
        LeaveCriticalSection(&g_queueLock);
        if (!submitQueue)
            return;

        if (ui::g_needFontRebuild)
        {
            // Wait for all in-flight frames to finish on the GPU before destroying the old font texture
            for (auto& f : g_frames)
            {
                if (f.fenceValue != 0 && g_fence->GetCompletedValue() < f.fenceValue)
                {
                    g_fence->SetEventOnCompletion(f.fenceValue, g_fenceEvent);
                    WaitForSingleObject(g_fenceEvent, INFINITE);
                }
            }
            ImGui_ImplDX12_InvalidateDeviceObjects();
            ui::InitStyle((static_cast<float>(g_scHeight) / 1080.0f) * State::Get().menuScale);
            ImGui_ImplDX12_CreateDeviceObjects();
            ui::g_needFontRebuild = false;
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        gui::Render();

        ImGui::Render();

        const UINT idx = swapChain->GetCurrentBackBufferIndex();
        if (idx >= g_frames.size() || !g_frames[idx].renderTarget)
        {
            // Live index outran our (just-reconciled) frame array - never index
            // out of bounds. Should not happen after ReconcileSwapChain, but a
            // freed device is worse than a dropped overlay frame.
            submitQueue->Release();
            return;
        }
        FrameContext& frame = g_frames[idx];

        if (frame.fenceValue != 0 && g_fence->GetCompletedValue() < frame.fenceValue)
        {
            g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, 1000);
        }

        frame.commandAllocator->Reset();
        g_commandList->Reset(frame.commandAllocator, nullptr);

        // --- Pass 1: ImGui draws into the offscreen SDR target -------------
        D3D12_RESOURCE_BARRIER offscreenBarrier = {};
        offscreenBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        offscreenBarrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        offscreenBarrier.Transition.pResource   = g_offscreenTex;
        offscreenBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        offscreenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        offscreenBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &offscreenBarrier);

        const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_commandList->OMSetRenderTargets(1, &g_offscreenRtv, FALSE, nullptr);
        g_commandList->ClearRenderTargetView(g_offscreenRtv, transparent, 0, nullptr);
        g_commandList->SetDescriptorHeaps(1, &g_srvHeap);

        // Record any pending icon-texture uploads ahead of the ImGui draws
        // (same list, in order) so a texture requested while building this
        // frame's draw data - the startup atlases on frame one, lazily loaded
        // item icons any time after - is safe to sample this same frame.
        ui::IconsRecordUploads(g_commandList);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList);

        offscreenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        offscreenBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g_commandList->ResourceBarrier(1, &offscreenBarrier);

        // --- Pass 2: composite the offscreen image onto the real back buffer,
        // re-encoding for its actual color space (see CreateCompositePipeline).
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = frame.renderTarget;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &barrier);
        g_commandList->OMSetRenderTargets(1, &frame.rtvHandle, FALSE, nullptr);

        const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(g_scWidth), static_cast<float>(g_scHeight), 0.0f, 1.0f };
        const D3D12_RECT     scissor  = { 0, 0, static_cast<LONG>(g_scWidth), static_cast<LONG>(g_scHeight) };
        g_commandList->RSSetViewports(1, &viewport);
        g_commandList->RSSetScissorRects(1, &scissor);

        g_commandList->SetGraphicsRootSignature(g_compositeRootSig);
        g_commandList->SetPipelineState(g_compositePSO);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const struct { UINT mode; float paperWhiteNits; float pad[2]; } params =
            { CompositeModeForColorSpace(g_colorSpace), kPaperWhiteNits, { 0.0f, 0.0f } };
        g_commandList->SetGraphicsRoot32BitConstants(0, 4, &params, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE offscreenSrvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
        offscreenSrvGpu.ptr += static_cast<UINT64>(kOffscreenSrvSlot) *
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_commandList->SetGraphicsRootDescriptorTable(1, offscreenSrvGpu);

        g_commandList->DrawInstanced(3, 1, 0, 0);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);

        if (FAILED(g_commandList->Close()))
        {
            submitQueue->Release();
            return;
        }

        ID3D12CommandList* toExec[] = { g_commandList };
        submitQueue->ExecuteCommandLists(1, toExec);
        submitQueue->Signal(g_fence, ++g_fenceValue);
        frame.fenceValue = g_fenceValue;
        submitQueue->Release();
    }

    // Init-if-needed and draw the overlay into swapChain's current back buffer.
    // Returns whether a frame was actually submitted (for the post-present device
    // check). isGameFacing is false for native presents while a wrapper owns the
    // drawing - those are Streamline's read-only finished frames, left untouched.
    static bool RenderOverlay(IDXGISwapChain3* swapChain, bool isGameFacing)
    {
        if (!isGameFacing)
            return false;

        if (!g_imguiReady && !g_initFailed)
        {
            // Init needs only the swapchain (device comes from it). The present
            // queue is captured separately by hkExecuteCommandLists.
            if (InitImGui(swapChain))
                g_imguiReady = true;
            else
            {
                g_initFailed = true;
                LOG_ERR("Overlay init failed - Trinity is disabled for this session.");
            }
        }

        if (!g_imguiReady || g_renderDisabled)
            return false;

        // Keep character and mount stamina locked full on every frame, even when standing completely motionless
        game::Player::RefreshSelf();

        // Hook the game's XInput module once it has loaded (no-op thereafter) so
        // controller input is blocked from the game while the menu is up.
        hooks::EnsureXInputHooks();

        State& st = State::Get();
        if (ui::PollMenuToggle())
            st.menuOpen = !st.menuOpen;

        // Marker Teleport hotkey (polled when menu and text captures are not active)
        if (!st.menuOpen && !st.textCapture && !st.rebindCapture)
        {
            bool kbdHit = false;
            if (st.markerTeleportKeyVk != 0)
            {
                static bool s_markerKeyWasDown = false;
                const bool isDown = (GetAsyncKeyState(st.markerTeleportKeyVk) & 0x8000) != 0;
                if (isDown && !s_markerKeyWasDown)
                    kbdHit = true;
                s_markerKeyWasDown = isDown;
            }

            bool padHit = false;
            if (st.markerTeleportPadMask != 0)
            {
                static bool s_markerPadWasDown = false;
                const unsigned int pad = ui::PadButtonsWithTriggers();
                const bool isDown = (pad & st.markerTeleportPadMask) == st.markerTeleportPadMask;
                if (isDown && !s_markerPadWasDown)
                    padHit = true;
                s_markerPadWasDown = isDown;
            }

            if (kbdHit || padHit)
            {
                const auto res = game::Teleport::TeleportToMarker(st.markerFallbackHeight);
                switch (res)
                {
                case game::Teleport::MarkerStatus::Success:
                    ui::Toast("Teleported to destination");
                    break;
                case game::Teleport::MarkerStatus::NoMarker:
                    ui::Toast("No destination found on map");
                    break;
                case game::Teleport::MarkerStatus::NoPlayer:
                    ui::Toast("Player not ready");
                    break;
                case game::Teleport::MarkerStatus::InvalidCoordinates:
                    ui::Toast("Invalid destination coordinates");
                    break;
                case game::Teleport::MarkerStatus::UnsafeContext:
                    ui::Toast("Unsafe destination context");
                    break;
                default:
                    ui::Toast("Destination teleport failed");
                    break;
                }
            }
        }

        if (!gui::WantsDraw())
        {
            ImGui::GetIO().MouseDrawCursor = false;
            return false;
        }

        bool drew = false;
        __try
        {
            DrawOverlay(swapChain);
            drew = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LOG_ERR("Overlay crashed (exception 0x%08X) - overlay disabled, game continues.",
                    GetExceptionCode());
            g_renderDisabled = true;
        }
        return drew;
    }

    // On a device removal, dump DRED (Device Removed Extended Data): the GPU
    // breadcrumb trail (which queue/command list was mid-execution) and the page
    // fault (the faulting virtual address + the resource that owned it, if named).
    // This is what turns an opaque ACCESS_DENIED under DLSS-G Frame Generation into
    // "OUR command list faulted writing back buffer X" vs "a Streamline-owned
    // resource" - the difference decides the real fix. Requires DRED to have been
    // armed before device creation (EnableDredIfAvailable, from InstallDX12Hooks).
    static void DumpDred()
    {
        if (!g_device) return;

        ID3D12DeviceRemovedExtendedData* dred = nullptr;
        if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dred))) || !dred)
        {
            LOG_ERR("DRED: interface unavailable on this device - fault cause not captured.");
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc = {};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc)) && bc.pHeadAutoBreadcrumbNode)
        {
            int n = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* node = bc.pHeadAutoBreadcrumbNode;
                 node && n < 8; node = node->pNext, ++n)
            {
                const UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                LOG_ERR("DRED breadcrumb[%d]: queue='%s' list='%s' completed %u/%u ops",
                        n,
                        node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "?",
                        node->pCommandListDebugNameA  ? node->pCommandListDebugNameA  : "?",
                        last, node->BreadcrumbCount);
            }
        }
        else
        {
            // No GPU breadcrumbs => the removal was NOT a GPU-side command failure.
            // Points at a DXGI/driver-level rejection (e.g. writing a read-only
            // shared back buffer during an FG swapchain reconfigure), not our submit.
            LOG_ERR("DRED: no GPU breadcrumbs - removal was not a GPU command fault.");
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT pf = {};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA)
        {
            LOG_ERR("DRED page fault VA=0x%llx", static_cast<unsigned long long>(pf.PageFaultVA));
            for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadExistingAllocationNode; a; a = a->pNext)
                LOG_ERR("  live alloc at fault: '%s'", a->ObjectNameA ? a->ObjectNameA : "?");
            for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadRecentFreedAllocationNode; a; a = a->pNext)
                LOG_ERR("  recently-freed alloc: '%s'", a->ObjectNameA ? a->ObjectNameA : "?");
        }
        else
        {
            LOG_ERR("DRED: no page fault recorded - not a bad-address access.");
        }

        dred->Release();
    }

    // Fail safe: if our overlay work removed the device, stop rendering instead
    // of spamming a dead GPU.
    static void PostPresentDeviceCheck(bool drew)
    {
        if (drew && g_device)
        {
            const HRESULT removed = g_device->GetDeviceRemovedReason();
            if (removed != S_OK)
            {
                LOG_ERR("Device removed (0x%08X) after overlay submit - overlay disabled.", removed);
                DumpDred();
                g_renderDisabled = true;
            }
        }
    }

    // Shared ResizeBuffers handling: retire our work + drop views, let the real
    // resize happen (caller), then rebuild from the post-resize truth.
    static void PreResizeCleanup()
    {
        if (!g_imguiReady) return;
        WaitForOverlayIdle();
        CleanupRenderTargets();
    }
    static void PostResizeRebuild(IDXGISwapChain3* swapChain)
    {
        if (!g_imguiReady) return;
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (SUCCEEDED(swapChain->GetDesc(&desc)))
        {
            if (desc.BufferCount != g_bufferCount)
                ResizeFrameResources(desc.BufferCount);
            g_hwnd      = desc.OutputWindow;
            g_swapChain = swapChain;
            g_scWidth   = desc.BufferDesc.Width;
            g_scHeight  = desc.BufferDesc.Height;
            g_scFormat  = desc.BufferDesc.Format;
        }
        CreateRenderTargets(swapChain);
        // Must follow the g_scWidth/g_scHeight update above (and precede the
        // next ReconcileSwapChain, which would otherwise see the new signature
        // as "unchanged" and never resize the offscreen target to match).
        CreateOffscreenTarget(g_scWidth, g_scHeight);
    }

    // Native-swapchain Present/ResizeBuffers byte-detours (from the dummy vtable).
    // These are the drawing path ONLY when no wrapper is active (wrapping failed,
    // or a legacy no-proxy path). Once a wrapper owns drawing they forward
    // untouched - under DLSS-G the native present is Streamline's read-only frame.
    static HRESULT WINAPI hkPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
    {
        const bool drew = RenderOverlay(swapChain, !g_wrapperActive);
        const HRESULT hr = oPresent(swapChain, syncInterval, flags);
        PostPresentDeviceCheck(drew);
        return hr;
    }

    static HRESULT WINAPI hkResizeBuffers(
        IDXGISwapChain3* swapChain, UINT bufferCount,
        UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        if (!g_imguiReady || g_wrapperActive)
            return oResizeBuffers(swapChain, bufferCount, width, height, format, flags);
        PreResizeCleanup();
        const HRESULT hr = oResizeBuffers(swapChain, bufferCount, width, height, format, flags);
        PostResizeRebuild(swapChain);
        return hr;
    }

    // Only reachable when wrapping failed (see g_wrapperActive) - the wrapper's
    // own SetColorSpace1 override covers the normal case.
    static HRESULT WINAPI hkSetColorSpace1(IDXGISwapChain3* swapChain, DXGI_COLOR_SPACE_TYPE colorSpace)
    {
        const HRESULT hr = oSetColorSpace1(swapChain, colorSpace);
        if (SUCCEEDED(hr)) OnColorSpaceChanged(colorSpace);
        return hr;
    }

    // Log which module an address lives in - tells us from a live test whether the
    // factory we patched is the DLSS-G proxy (sl.*/nvngx*) or native (dxgi.dll).
    static void LogHookedModule(const char* what, void* addr)
    {
        HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &m);
        char path[MAX_PATH] = "?";
        if (m) GetModuleFileNameA(m, path, MAX_PATH);
        const char* base = strrchr(path, '\\');
        LOG("%s @ %p in %s", what, addr, base ? base + 1 : path);
    }

    // --- COM wrapper: draw the overlay before Streamline interpolates --------
    // Forwards every IDXGISwapChain4 method to the real (Streamline-proxy)
    // swapchain, intercepting Present/Present1 to draw the overlay into the
    // writable game-facing back buffer first, and ResizeBuffers* to rebuild RTVs.
    class WrappedIDXGISwapChain final : public IDXGISwapChain4
    {
    public:
        explicit WrappedIDXGISwapChain(IDXGISwapChain4* inner) : m_inner(inner) {}

        // IUnknown ----------------------------------------------------------
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown)               ||
                riid == __uuidof(IDXGIObject)            ||
                riid == __uuidof(IDXGIDeviceSubObject)   ||
                riid == __uuidof(IDXGISwapChain)         ||
                riid == __uuidof(IDXGISwapChain1)        ||
                riid == __uuidof(IDXGISwapChain2)        ||
                riid == __uuidof(IDXGISwapChain3)        ||
                riid == __uuidof(IDXGISwapChain4))
            {
                AddRef();
                *ppv = static_cast<IDXGISwapChain4*>(this);
                return S_OK;
            }
            return m_inner->QueryInterface(riid, ppv);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = InterlockedDecrement(&m_ref);
            if (r == 0) { m_inner->Release(); delete this; }
            return r;
        }

        // IDXGIObject -------------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID n, UINT s, const void* d) override { return m_inner->SetPrivateData(n, s, d); }
        HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID n, const IUnknown* p) override { return m_inner->SetPrivateDataInterface(n, p); }
        HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID n, UINT* s, void* d) override { return m_inner->GetPrivateData(n, s, d); }
        HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return m_inner->GetParent(riid, pp); }

        // IDXGIDeviceSubObject ---------------------------------------------
        HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** pp) override { return m_inner->GetDevice(riid, pp); }

        // IDXGISwapChain ----------------------------------------------------
        HRESULT STDMETHODCALLTYPE Present(UINT syncInterval, UINT flags) override
        {
            const bool drew = RenderOverlay(m_inner, true);
            const HRESULT hr = m_inner->Present(syncInterval, flags);
            PostPresentDeviceCheck(drew);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE GetBuffer(UINT i, REFIID riid, void** pp) override { return m_inner->GetBuffer(i, riid, pp); }
        HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fs, IDXGIOutput* t) override { return m_inner->SetFullscreenState(fs, t); }
        HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fs, IDXGIOutput** t) override { return m_inner->GetFullscreenState(fs, t); }
        HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override { return m_inner->GetDesc(d); }
        HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl) override
        {
            PreResizeCleanup();
            const HRESULT hr = m_inner->ResizeBuffers(bc, w, h, f, fl);
            PostResizeRebuild(m_inner);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* p) override { return m_inner->ResizeTarget(p); }
        HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** pp) override { return m_inner->GetContainingOutput(pp); }
        HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { return m_inner->GetFrameStatistics(p); }
        HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* p) override { return m_inner->GetLastPresentCount(p); }

        // IDXGISwapChain1 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* p) override { return m_inner->GetDesc1(p); }
        HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p) override { return m_inner->GetFullscreenDesc(p); }
        HRESULT STDMETHODCALLTYPE GetHwnd(HWND* p) override { return m_inner->GetHwnd(p); }
        HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** pp) override { return m_inner->GetCoreWindow(riid, pp); }
        HRESULT STDMETHODCALLTYPE Present1(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* pp) override
        {
            const bool drew = RenderOverlay(m_inner, true);
            const HRESULT hr = m_inner->Present1(syncInterval, flags, pp);
            PostPresentDeviceCheck(drew);
            return hr;
        }
        BOOL    STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return m_inner->IsTemporaryMonoSupported(); }
        HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** pp) override { return m_inner->GetRestrictToOutput(pp); }
        HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* p) override { return m_inner->SetBackgroundColor(p); }
        HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* p) override { return m_inner->GetBackgroundColor(p); }
        HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override { return m_inner->SetRotation(r); }
        HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* p) override { return m_inner->GetRotation(p); }

        // IDXGISwapChain2 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w, UINT h) override { return m_inner->SetSourceSize(w, h); }
        HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w, UINT* h) override { return m_inner->GetSourceSize(w, h); }
        HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT m) override { return m_inner->SetMaximumFrameLatency(m); }
        HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* m) override { return m_inner->GetMaximumFrameLatency(m); }
        HANDLE  STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return m_inner->GetFrameLatencyWaitableObject(); }
        HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* p) override { return m_inner->SetMatrixTransform(p); }
        HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* p) override { return m_inner->GetMatrixTransform(p); }

        // IDXGISwapChain3 ---------------------------------------------------
        UINT    STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return m_inner->GetCurrentBackBufferIndex(); }
        HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE c, UINT* s) override { return m_inner->CheckColorSpaceSupport(c, s); }
        HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE c) override
        {
            const HRESULT hr = m_inner->SetColorSpace1(c);
            if (SUCCEEDED(hr)) OnColorSpaceChanged(c);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* nodeMask, IUnknown* const* pQueues) override
        {
            PreResizeCleanup();
            const HRESULT hr = m_inner->ResizeBuffers1(bc, w, h, f, fl, nodeMask, pQueues);
            PostResizeRebuild(m_inner);
            return hr;
        }

        // IDXGISwapChain4 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d) override { return m_inner->SetHDRMetaData(t, s, d); }

    private:
        IDXGISwapChain4* m_inner;
        LONG             m_ref = 1;
    };

    // Replace *pp (the real swapchain the game just created) with a wrapper that
    // owns it, so every Present routes through us first. hwnd is the window the
    // swapchain was created for - used to ignore auxiliary windows.
    static void WrapSwapChain(IDXGISwapChain1** pp, HWND hwnd)
    {
        if (!pp || !*pp) return;

        // Skip tiny probe/overlay swapchains (matches OptiScaler) - never wrap our
        // own or a capability-probe surface.
        DXGI_SWAP_CHAIN_DESC1 d1 = {};
        if (SUCCEEDED((*pp)->GetDesc1(&d1)) && (d1.Width < 100 || d1.Height < 100))
            return;

        // Once we have locked onto the game's main window, ignore swapchains on any
        // other window: those are auxiliary surfaces (the engine/Streamline create
        // them) and wrapping them buys nothing while risking a fight with SL's own
        // bookkeeping.
        if (g_wrappedHwnd && hwnd && hwnd != g_wrappedHwnd)
            return;

        IDXGISwapChain4* inner = nullptr;
        if (FAILED((*pp)->QueryInterface(IID_PPV_ARGS(&inner))) || !inner)
            return; // need full IDXGISwapChain4 to wrap safely; else leave native

        // Transfer the caller's ref to the wrapper: QI added a ref (inner), drop
        // the raw ref, hand back the wrapper (ref=1) which owns inner.
        (*pp)->Release();
        auto* wrapper = new WrappedIDXGISwapChain(inner);
        *pp = static_cast<IDXGISwapChain1*>(wrapper);

        if (hwnd) g_wrappedHwnd = hwnd;

        // Log the layer on EVERY wrap - the pre-FG chain and the FG-active re-wrap
        // can be different objects. inner->Present in sl.*/nvngx/amd*/xess = a FG
        // proxy (correct, pre-interpolation, writable). dxgi.dll = native chain.
        // BufferCount jumping (e.g. to 6) + Flags reveal when MFG has taken over.
        void** innerVt = *reinterpret_cast<void***>(inner);
        LogHookedModule("Wrapped swapchain Present", innerVt[8]);

        if (!g_wrapperActive)
        {
            g_wrapperActive = true;
            LOG_OK("Swapchain wrapped - overlay composites before Frame Generation (%ux%u, %u buffers, flags 0x%X).",
                   d1.Width, d1.Height, d1.BufferCount, d1.Flags);
        }
        else
        {
            // A later recreation (FG toggle, resolution/settings change): expected.
            LOG("Swapchain re-wrapped after recreation (%ux%u, %u buffers, flags 0x%X).",
                d1.Width, d1.Height, d1.BufferCount, d1.Flags);
        }
    }

    // Our replacement for the factory's CreateSwapChainForHwnd vtable slot. Calls
    // the real slot (saved), then wraps the result. Not a MinHook detour - see
    // InstallSwapChainCreationPatch.
    static HRESULT STDMETHODCALLTYPE hkFactoryCreateSwapChainForHwnd(
        IDXGIFactory2* self, IUnknown* device, HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fsDesc,
        IDXGIOutput* restrictOut, IDXGISwapChain1** ppSwapChain)
    {
        // Only the outermost (game-issued) creation is wrapped. If we are already
        // inside a creation on this thread, this is Streamline recreating the chain
        // through the same patched slot during an FG toggle - let it complete
        // untouched, otherwise we recurse into the interposer mid-rebuild.
        const bool nested = t_inSwapChainCreate;
        t_inSwapChainCreate = true;
        const HRESULT hr = oFactoryCreateSwapChainForHwnd(self, device, hwnd, desc, fsDesc, restrictOut, ppSwapChain);
        if (!nested)
        {
            t_inSwapChainCreate = false;
            if (SUCCEEDED(hr))
            {
                // For D3D12 the first argument is the swapchain's present command
                // queue - the queue that owns the back buffers. Pin it as the queue
                // we submit overlay work on, so we never submit on Streamline's MFG
                // pacer queue (which is rejected). Re-pins on each recreation too.
                ID3D12CommandQueue* pq = nullptr;
                if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&pq))) && pq)
                {
                    PublishPresentQueue(pq, true);
                    pq->Release();
                }
                WrapSwapChain(ppSwapChain, hwnd);
            }
        }
        return hr;
    }

    // VirtualProtect-patch one vtable slot. Reentrancy-safe (no MinHook, no thread
    // suspend), so it is safe to leave installed while the game runs.
    static bool PatchVtableSlot(void** vtable, int index, void* newFn, void** origOut)
    {
        DWORD old = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
            return false;
        *origOut = vtable[index];
        vtable[index] = newFn;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
        return true;
    }

    // Patch the factory CLASS's CreateSwapChainForHwnd slot (slot 15) so every
    // swapchain the game creates comes back wrapped. We read the vtable from a
    // dummy factory of the same class the game will use (a Streamline proxy factory
    // when FG is present, since we go through the same DXGI entry points). No
    // export detour, no reentrant MinHook -> cannot deadlock startup or fight
    // Streamline's interposer. The patched vtable lives in the DLL and persists
    // after the dummy factory is released.
    static void InstallSwapChainCreationPatch()
    {
        IDXGIFactory2* factory = nullptr;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory)
        {
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory)
            {
                LOG_ERR("FG: no factory to patch - overlay-over-FrameGen disabled.");
                return;
            }
        }

        void** vt = *reinterpret_cast<void***>(factory);
        LogHookedModule("Factory CreateSwapChainForHwnd (patching)", vt[15]);
        if (PatchVtableSlot(vt, 15, reinterpret_cast<void*>(&hkFactoryCreateSwapChainForHwnd),
                            reinterpret_cast<void**>(&oFactoryCreateSwapChainForHwnd)))
            LOG("FG: CreateSwapChainForHwnd patched - new swapchains will be wrapped.");
        else
            LOG_ERR("FG: VirtualProtect of CreateSwapChainForHwnd slot failed.");

        factory->Release();
    }

    // Arm DRED so a later device removal is diagnosable. MUST run before the game
    // creates its D3D12 device - we do, because the ASI loader (winmm.dll) injects
    // us at process start, well ahead of the engine's renderer init. Global process
    // setting: applies to the device the game subsequently creates. Cheap; the
    // breadcrumb ring adds negligible overhead. See DumpDred.
    static void EnableDredIfAvailable()
    {
        ID3D12DeviceRemovedExtendedDataSettings* dred = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))) && dred)
        {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->Release();
            LOG("DRED armed - a device removal will report GPU breadcrumbs + page faults.");
        }
        else
        {
            LOG("DRED unavailable on this system - device-removed causes stay opaque.");
        }
    }

    // --- Bootstrap: dummy device to locate the vtable slots -----------------
    static bool GetVTableAddresses(void*& presentAddr, void*& resizeAddr, void*& execAddr, void*& colorSpaceAddr)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TrinityDummyWnd";
        RegisterClassExW(&wc);

        HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd)
        {
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        bool ok = false;
        ID3D12Device*              device  = nullptr;
        ID3D12CommandQueue*        queue   = nullptr;
        ID3D12CommandAllocator*    alloc   = nullptr;
        ID3D12GraphicsCommandList* list    = nullptr;
        IDXGIFactory4*             factory = nullptr;
        IDXGISwapChain1*           swap1   = nullptr;
        IDXGISwapChain3*           swap3   = nullptr;

        do
        {
            if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
                break;

            D3D12_COMMAND_QUEUE_DESC qd = {};
            qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
                break;

            if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
                break;
            if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))))
                break;

            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                break;

            DXGI_SWAP_CHAIN_DESC1 scd = {};
            scd.BufferCount      = 2;
            scd.Width            = 100;
            scd.Height           = 100;
            scd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.SampleDesc.Count = 1;
            scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            scd.AlphaMode        = DXGI_ALPHA_MODE_UNSPECIFIED;

            if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr, nullptr, &swap1)))
                break;
            if (FAILED(swap1->QueryInterface(IID_PPV_ARGS(&swap3))))
                break;

            // vtable[8]  IDXGISwapChain::Present
            // vtable[13] IDXGISwapChain::ResizeBuffers
            // vtable[38] IDXGISwapChain3::SetColorSpace1
            void** swapVtbl = *reinterpret_cast<void***>(swap3);
            presentAddr    = swapVtbl[8];
            resizeAddr     = swapVtbl[13];
            colorSpaceAddr = swapVtbl[38];

            // vtable[10] ID3D12CommandQueue::ExecuteCommandLists
            void** queueVtbl = *reinterpret_cast<void***>(queue);
            execAddr = queueVtbl[10];

            ok = true;
        } while (false);

        if (swap3)   swap3->Release();
        if (swap1)   swap1->Release();
        if (factory) factory->Release();
        if (list)    list->Release();
        if (alloc)   alloc->Release();
        if (queue)   queue->Release();
        if (device)  device->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return ok;
    }

    bool InstallDX12Hooks()
    {
        void* presentAddr    = nullptr;
        void* resizeAddr     = nullptr;
        void* execAddr       = nullptr;
        void* colorSpaceAddr = nullptr;

        if (!GetVTableAddresses(presentAddr, resizeAddr, execAddr, colorSpaceAddr))
        {
            LOG_ERR("Failed to resolve DX12 vtable addresses.");
            return false;
        }

        // Must be ready before the ExecuteCommandLists hook can fire.
        InitializeCriticalSection(&g_queueLock);
        g_queueLockReady = true;

        if (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent)) != MH_OK ||
            MH_CreateHook(resizeAddr,  &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK ||
            MH_CreateHook(execAddr,    &hkExecuteCommandLists, reinterpret_cast<void**>(&oExecuteCommandLists)) != MH_OK ||
            MH_CreateHook(colorSpaceAddr, &hkSetColorSpace1, reinterpret_cast<void**>(&oSetColorSpace1)) != MH_OK)
        {
            LOG_ERR("MH_CreateHook failed.");
            return false;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        {
            LOG_ERR("MH_EnableHook failed.");
            return false;
        }

        // DLSS-G / Frame Generation: patch the factory's CreateSwapChainForHwnd
        // slot so the game's swapchain comes back wrapped, letting the overlay
        // draw into the writable game-facing buffer before Streamline interpolates.
        // Done AFTER the dummy swapchain above is gone so it is never wrapped.
        // Best-effort: if it fails the native present hook still draws (non-FG).
        InstallSwapChainCreationPatch();

        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        LOG("DX12 hooks installed (%s, pid %lu).", exeName, GetCurrentProcessId());
        return true;
    }

    void RemoveDX12Hooks()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        input::Shutdown();
        hooks::RemoveXInputHooks();

        if (g_imguiReady)
        {
            ui::IconsShutdown();
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_imguiReady = false;
        }

        CleanupRenderTargets();
        for (auto& f : g_frames)
            if (f.commandAllocator) { f.commandAllocator->Release(); f.commandAllocator = nullptr; }
        g_frames.clear();
        g_swapChain   = nullptr;
        g_bufferCount = 0;
        g_scWidth     = 0;
        g_scHeight    = 0;
        g_scFormat    = DXGI_FORMAT_UNKNOWN;

        ReleaseOffscreenTarget();
        if (g_compositePSO)     { g_compositePSO->Release();     g_compositePSO     = nullptr; }
        if (g_compositeRootSig) { g_compositeRootSig->Release(); g_compositeRootSig = nullptr; }
        g_compositeFormat = DXGI_FORMAT_UNKNOWN;
        g_colorSpace      = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

        if (g_queueLockReady)
        {
            EnterCriticalSection(&g_queueLock);
            if (g_presentQueue) { g_presentQueue->Release(); g_presentQueue = nullptr; }
            g_presentQueuePinned = false;
            LeaveCriticalSection(&g_queueLock);
            g_queueLockReady = false;
            DeleteCriticalSection(&g_queueLock);
        }

        if (g_fenceEvent)  { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
        if (g_fence)       { g_fence->Release();       g_fence = nullptr; }
        if (g_commandList) { g_commandList->Release(); g_commandList = nullptr; }
        if (g_srvHeap)     { g_srvHeap->Release();     g_srvHeap = nullptr; }
        if (g_rtvHeap)     { g_rtvHeap->Release();     g_rtvHeap = nullptr; }
        if (g_device)      { g_device->Release();      g_device = nullptr; }
    }
}
