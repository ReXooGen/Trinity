#include "icons.h"

#include <Windows.h>
#include <d3d12.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/logger.h"
#include "../core/state.h"
#include "../core/version_detect.h"
#include "../game/pak.h"
#include "ps_atlas_raw.h"

namespace trinity::ui
{
    namespace
    {
        // --- Atlas + per-icon rect table ------------------------------------
        enum AtlasId { A_COMMON = 0, A_KBD = 1, A_PAD = 2, A_PS_PAD = 3, A_COUNT = 4 };

        struct AtlasSrc { const char* dir; const char* file; };
        const AtlasSrc kAtlasSrc[A_COUNT] = {
            { "ui/texture", "cd_icon_common_00.dds"   },
            { "ui/texture", "cd_icon_keyguide_01.dds" },
            { "ui/texture", "cd_icon_keyguide_00.dds" },
            { nullptr, nullptr } // A_PS_PAD is raw bytes
        };

        struct Rect { AtlasId atlas; int x, y, w, h; };

        // Pixel rects within each 1024x1024 atlas (see icon-pipeline notes).
        const Rect kRects[static_cast<int>(Icon::Count)] = {
            /* None        */ { A_COMMON,   0,   0,   0,  0 },
            /* TabPlayer   */ { A_COMMON, 224, 622,  65, 85 },
            /* TabTravel   */ { A_COMMON,  18, 447,  65, 65 },
            /* TabItems    */ { A_COMMON, 123, 746,  60, 83 },
            /* TabWorld    */ { A_COMMON, 621, 939,  24, 43 },
            /* TabSystem   */ { A_COMMON,  13, 645,  79, 78 },
            /* KeyNav      */ { A_KBD,    256, 323,  62, 62 },
            /* KeyUp       */ { A_KBD,    420, 815,  62, 62 },
            /* KeyDown     */ { A_KBD,    502, 323,  62, 62 },
            /* KeyLeft     */ { A_KBD,    502, 487,  62, 62 },
            /* KeyRight    */ { A_KBD,    912, 741,  62, 62 },
            /* KeyEnter    */ { A_KBD,    666, 331,  62, 62 },
            /* KeyBackspace*/ { A_KBD,     12,  12,  98, 58 },
            /* KeyEsc      */ { A_KBD,    378,  90,  98, 58 },
            /* KeyTab      */ { A_KBD,    378, 164,  98, 58 },
            /* KeyDel      */ { A_KBD,    256,  12,  98, 58 },
            /* KeyQ        */ { A_KBD,    584, 741,  62, 62 },
            /* KeyE        */ { A_KBD,    584, 331,  62, 62 },
            /* PadA        */ { A_PAD,    498, 252,  70, 70 },
            /* PadB        */ { A_PAD,    662, 252,  70, 70 },
            /* PadX        */ { A_PAD,     84, 658,  78, 78 },
            /* PadY        */ { A_PAD,    662, 498,  70, 70 },
            /* PadLB       */ { A_PAD,    494, 337,  78, 61 },
            /* PadRB       */ { A_PAD,    412, 747,  78, 61 },
            /* PadDpad     */ { A_PAD,    166,  84,  78, 78 },
        };

        struct Atlas
        {
            ID3D12Resource* tex = nullptr;
            ImTextureID     id  = 0;
            int             w   = 0;
            int             h   = 0;
        };

        // A texture whose GPU copy hasn't been recorded yet.
        struct Pending
        {
            ID3D12Resource*                   tex    = nullptr;
            ID3D12Resource*                   upload = nullptr;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp    = {};
        };

        Atlas                        g_atlas[A_COUNT];
        std::vector<Pending>         g_pending;      // awaiting a command list
        std::vector<ID3D12Resource*> g_uploadKeep;   // upload buffers, freed at shutdown
        bool                         g_ready = false;

        // --- Lazily-loaded item icons ---------------------------------------
        // One texture per icon file, created on first draw and cached for the
        // session, keyed by the icon's file name (several items share one
        // icon, so caching by file dedupes the textures). A failed load is
        // cached too so it is never retried. Render thread only (menu draw +
        // IconsRecordUploads), no locking.
        struct ItemIcon { Atlas atlas; bool ok = false; };
        std::unordered_map<std::string, ItemIcon> g_itemIcons;

        // Device/heap context kept from IconsInit for the lazy uploads. Slots
        // [g_nextSlot, g_slotCount) are the item-icon budget; when it runs out
        // further new icons simply draw as misses (cached as failed).
        ID3D12Device*         g_dev      = nullptr;
        ID3D12DescriptorHeap* g_heap     = nullptr;
        unsigned              g_inc      = 0;
        unsigned              g_nextSlot = 0;
        unsigned              g_slotCount = 0;

        // The DDS pixel format, read rather than assumed. This used to hardcode
        // BC3, which quietly rejected every BC1 icon the game ships: a BC1
        // payload is 8 bytes per block against BC3's 16, so it is HALF the size
        // the BC3 check demanded and failed the "is the payload big enough"
        // test. The symptom was an icon that loaded fine out of the pak and then
        // "failed to load" with no pak error to explain it.
        //
        // Header layout (file offsets): "DDS " magic at 0, DDS_HEADER at 4
        // (124 bytes), so dwHeight = 12, dwWidth = 16, and DDS_PIXELFORMAT sits
        // at 76 with its dwFourCC at 84. A "DX10" fourCC means a 20-byte
        // DDS_HEADER_DXT10 follows at 128 (its DXGI format first), pushing the
        // payload to 148.
        struct DdsFormat { DXGI_FORMAT fmt; unsigned blockBytes; size_t dataOffset; };

        // Human-readable header summary, for when a load fails. The point is to
        // stop GUESSING at why: the icon-pipeline notes say every item icon is
        // a uniform 256x256 DXT5, so anything that fails is by definition not
        // what we think it is - and the log should say what it actually is.
        void DescribeDDS(const std::vector<uint8_t>& dds, char* out, size_t n)
        {
            if (dds.size() < 4 || memcmp(dds.data(), "DDS ", 4) != 0)
            { snprintf(out, n, "not a DDS (%zu bytes)", dds.size()); return; }
            if (dds.size() < 128) { snprintf(out, n, "truncated (%zu bytes)", dds.size()); return; }
            uint32_t h = 0, w = 0, mips = 0, fourCC = 0, pfFlags = 0, bitCount = 0;
            memcpy(&h, &dds[12], 4);  memcpy(&w, &dds[16], 4);
            memcpy(&mips, &dds[28], 4);
            memcpy(&pfFlags, &dds[80], 4);
            memcpy(&fourCC, &dds[84], 4);
            memcpy(&bitCount, &dds[88], 4);
            char cc[5] = {};
            memcpy(cc, &fourCC, 4);
            for (int i = 0; i < 4; ++i)
                if (cc[i] < 32 || cc[i] > 126) cc[i] = '?';
            snprintf(out, n, "%ux%u mips=%u fourCC='%s' pfFlags=0x%X bpp=%u payload=%zu bytes",
                     w, h, mips, cc, pfFlags, bitCount, dds.size() > 128 ? dds.size() - 128 : 0);
        }

        bool ParseDdsFormat(const std::vector<uint8_t>& dds, DdsFormat& out)
        {
            if (dds.size() < 128 || memcmp(dds.data(), "DDS ", 4) != 0) return false;
            uint32_t fourCC = 0;
            memcpy(&fourCC, &dds[84], 4);
            const auto cc = [](const char* s) {
                uint32_t v; memcpy(&v, s, 4); return v;
            };
            out.dataOffset = 128;
            if (fourCC == cc("DXT1")) { out.fmt = DXGI_FORMAT_BC1_UNORM; out.blockBytes = 8;  return true; }
            if (fourCC == cc("DXT3")) { out.fmt = DXGI_FORMAT_BC2_UNORM; out.blockBytes = 16; return true; }
            if (fourCC == cc("DXT5")) { out.fmt = DXGI_FORMAT_BC3_UNORM; out.blockBytes = 16; return true; }
            if (fourCC == cc("DX10"))
            {
                if (dds.size() < 148) return false;
                uint32_t dxgi = 0;
                memcpy(&dxgi, &dds[128], 4);
                out.dataOffset = 148;
                switch (dxgi)
                {
                case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
                    out.fmt = DXGI_FORMAT_BC1_UNORM; out.blockBytes = 8;  return true;
                case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
                    out.fmt = DXGI_FORMAT_BC2_UNORM; out.blockBytes = 16; return true;
                case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
                    out.fmt = DXGI_FORMAT_BC3_UNORM; out.blockBytes = 16; return true;
                case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
                    out.fmt = DXGI_FORMAT_BC7_UNORM; out.blockBytes = 16; return true;
                default: return false; // uncompressed / exotic: not worth supporting
                }
            }
            return false;
        }
        bool UploadRGBA(ID3D12Device* dev, ID3D12DescriptorHeap* heap,
                        unsigned inc, unsigned slot,
                        uint32_t width, uint32_t height,
                        const uint8_t* pixels, Atlas& out)
        {
            D3D12_HEAP_PROPERTIES defHeap = {}; defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_HEAP_PROPERTIES upHeap  = {}; upHeap.Type  = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC td = {};
            td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width            = width;
            td.Height           = height;
            td.DepthOrArraySize = 1;
            td.MipLevels        = 1;
            td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            ID3D12Resource* tex = nullptr;
            if (FAILED(dev->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &td,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
                return false;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
            UINT   numRows  = 0;
            UINT64 rowBytes = 0, uploadSize = 0;
            dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowBytes, &uploadSize);

            D3D12_RESOURCE_DESC bd = {};
            bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width            = uploadSize;
            bd.Height           = 1;
            bd.DepthOrArraySize = 1;
            bd.MipLevels        = 1;
            bd.Format           = DXGI_FORMAT_UNKNOWN;
            bd.SampleDesc.Count = 1;
            bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ID3D12Resource* upload = nullptr;
            if (FAILED(dev->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
            {
                tex->Release();
                return false;
            }

            uint8_t* mapped = nullptr;
            D3D12_RANGE noRead = { 0, 0 };
            if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped))))
            {
                upload->Release(); tex->Release();
                return false;
            }
            // For RGBA8, each pixel is 4 bytes. Source row width is width * 4.
            const size_t srcRowBytes = static_cast<size_t>(width) * 4;
            for (UINT r = 0; r < numRows; ++r)
                memcpy(mapped + fp.Offset + static_cast<size_t>(r) * fp.Footprint.RowPitch,
                       pixels + static_cast<size_t>(r) * srcRowBytes,
                       srcRowBytes);
            upload->Unmap(0, nullptr);

            g_pending.push_back({ tex, upload, fp });

            D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels           = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += static_cast<SIZE_T>(slot) * inc;
            dev->CreateShaderResourceView(tex, &sd, cpuHandle);

            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += static_cast<UINT64>(slot) * inc;

            out.tex = tex;
            out.id = (ImTextureID)gpuHandle.ptr;
            out.w  = width;
            out.h  = height;
            return true;
        }

        // Create a default-heap block-compressed texture from a DDS, fill an
        // upload buffer, create its SRV at `slot`, and queue the GPU copy for
        // later recording on the overlay command list (see IconsRecordUploads).
        // No private command queue is created - that removed the device on some
        // drivers.
        bool UploadDDS(ID3D12Device* dev, ID3D12DescriptorHeap* heap,
                       unsigned inc, unsigned slot,
                       const std::vector<uint8_t>& dds, Atlas& out)
        {
            DdsFormat df{};
            if (!ParseDdsFormat(dds, df)) return false;
            uint32_t height, width;
            memcpy(&height, &dds[12], 4);
            memcpy(&width,  &dds[16], 4);
            if (!width || !height || dds.size() <= df.dataOffset) return false;
            const uint8_t* pixels = dds.data() + df.dataOffset;
            const size_t   pixLen = dds.size() - df.dataOffset;
            // Block-compressed: one 4x4 block per blockBytes. Sanity-check that
            // the top mip is actually present (extra mips after it are fine -
            // we only ever upload level 0).
            const size_t expected = (static_cast<size_t>((width + 3) / 4)) *
                                    ((height + 3) / 4) * df.blockBytes;
            if (pixLen < expected)
                return false;

            D3D12_HEAP_PROPERTIES defHeap = {}; defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_HEAP_PROPERTIES upHeap  = {}; upHeap.Type  = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC td = {};
            td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width            = width;
            td.Height           = height;
            td.DepthOrArraySize = 1;
            td.MipLevels        = 1;
            td.Format           = df.fmt;
            td.SampleDesc.Count = 1;
            td.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            ID3D12Resource* tex = nullptr;
            if (FAILED(dev->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &td,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
                return false;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
            UINT   numRows  = 0;
            UINT64 rowBytes = 0, uploadSize = 0;
            dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, &numRows, &rowBytes, &uploadSize);

            D3D12_RESOURCE_DESC bd = {};
            bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width            = uploadSize;
            bd.Height           = 1;
            bd.DepthOrArraySize = 1;
            bd.MipLevels        = 1;
            bd.Format           = DXGI_FORMAT_UNKNOWN;
            bd.SampleDesc.Count = 1;
            bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ID3D12Resource* upload = nullptr;
            if (FAILED(dev->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &bd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
            {
                tex->Release();
                return false;
            }

            // Copy the block-rows into the (256-aligned) upload footprint.
            uint8_t* mapped = nullptr;
            D3D12_RANGE noRead = { 0, 0 };
            if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped))))
            {
                upload->Release(); tex->Release();
                return false;
            }
            for (UINT r = 0; r < numRows; ++r)
                memcpy(mapped + fp.Offset + static_cast<size_t>(r) * fp.Footprint.RowPitch,
                       pixels + static_cast<size_t>(r) * rowBytes,
                       static_cast<size_t>(rowBytes));
            upload->Unmap(0, nullptr);

            // Defer the GPU copy: it will be recorded onto the overlay's own
            // command list on the first frame (see IconsRecordUploads). The
            // texture stays in COPY_DEST until then; it is not sampled before
            // the copy because the copy is recorded ahead of the ImGui draws.
            g_pending.push_back({ tex, upload, fp });

            // SRV in the shared heap slot -> ImTextureID is its GPU handle.
            D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format                        = df.fmt;
            sd.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels           = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
            cpu.ptr += static_cast<SIZE_T>(slot) * inc;
            dev->CreateShaderResourceView(tex, &sd, cpu);

            D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
            gpu.ptr += static_cast<UINT64>(slot) * inc;

            out.tex = tex;
            out.id  = (ImTextureID)gpu.ptr; // GPU descriptor handle -> ImGui texture id
            out.w   = static_cast<int>(width);
            out.h   = static_cast<int>(height);
            return true;
        }
    } // namespace

    void IconsInit(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap,
                   unsigned srvIncrement, unsigned firstSlot, unsigned slotCount)
    {
        if (g_ready || !device || !srvHeap)
            return;
        if (!game::pak::HaveGameRoot())
        {
            LOG("icons: game paks unavailable - menu will use text-only hints.");
            return;
        }

        int loaded = 0;
        for (int a = 0; a < A_COUNT; ++a)
        {
            if (a == A_PS_PAD)
            {
                if (UploadRGBA(device, srvHeap, srvIncrement, firstSlot + a,
                               ps_atlas_width, ps_atlas_height, ps_atlas_rgba, g_atlas[a]))
                    ++loaded;
                else
                    LOG_ERR("icons: failed to upload PS atlas.");
                continue;
            }

            std::vector<uint8_t> dds;
            if (!game::pak::ReadFile(12, kAtlasSrc[a].dir, kAtlasSrc[a].file, dds))
            {
                LOG_ERR("icons: failed to read %s.", kAtlasSrc[a].file);
                continue;
            }
            if (UploadDDS(device, srvHeap, srvIncrement, firstSlot + a, dds, g_atlas[a]))
                ++loaded;
            else
                LOG_ERR("icons: failed to upload %s.", kAtlasSrc[a].file);
        }

        // Item icons take every slot after the fixed atlases.
        g_dev       = device;
        g_heap      = srvHeap;
        g_inc       = srvIncrement;
        g_nextSlot  = firstSlot + A_COUNT;
        g_slotCount = slotCount;

        g_ready = (loaded > 0);
    }

    void IconsRecordUploads(ID3D12GraphicsCommandList* list)
    {
        if (!list || g_pending.empty())
            return;

        for (const Pending& p : g_pending)
        {
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource        = p.tex;
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource       = p.upload;
            src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = p.fp;

            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER b = {};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource   = p.tex;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            list->ResourceBarrier(1, &b);

            // Keep the upload buffer alive for the rest of the session; the copy
            // reads from it on the GPU and we don't track the frame fence here.
            g_uploadKeep.push_back(p.upload);
        }
        g_pending.clear();
    }

    void IconsShutdown()
    {
        for (auto& a : g_atlas)
        {
            if (a.tex) { a.tex->Release(); a.tex = nullptr; }
            a.id = 0;
        }
        for (auto& [stem, ic] : g_itemIcons)
            if (ic.atlas.tex) ic.atlas.tex->Release();
        g_itemIcons.clear();
        for (auto* u : g_uploadKeep) if (u) u->Release();
        g_uploadKeep.clear();
        for (auto& p : g_pending) { if (p.upload) p.upload->Release(); if (p.tex) p.tex->Release(); }
        g_pending.clear();
        g_dev  = nullptr;
        g_heap = nullptr;
        g_ready = false;
    }

    bool IconsReady() { return g_ready; }

    ImVec2 IconSize(Icon ic, float height)
    {
        const int i = static_cast<int>(ic);
        if (i <= 0 || i >= static_cast<int>(Icon::Count) || height <= 0.0f)
            return ImVec2(0, 0);

        if (State::Get().playstationIcons && ic >= Icon::PadA && ic <= Icon::PadDpad)
        {
            // All PS icons in the raw atlas are 64x64 (aspect 1.0)
            return ImVec2(height, height);
        }

        const Rect& r = kRects[i];
        const float aspect = r.h > 0 ? static_cast<float>(r.w) / static_cast<float>(r.h) : 1.0f;
        return ImVec2(height * aspect, height);
    }

    void DrawIcon(ImDrawList* dl, Icon ic, ImVec2 pMin, ImVec2 pMax, ImU32 tint)
    {
        const int i = static_cast<int>(ic);
        if (!g_ready || !dl || i <= 0 || i >= static_cast<int>(Icon::Count))
            return;

        if (State::Get().playstationIcons && ic >= Icon::PadA && ic <= Icon::PadDpad)
        {
            const Atlas& a = g_atlas[A_PS_PAD];
            if (!a.tex || a.w <= 0 || a.h <= 0) return;
            
            // Map PadA..PadDpad to 0..6
            int psIndex = i - static_cast<int>(Icon::PadA);
            float x0 = static_cast<float>(psIndex * 64) / a.w;
            float x1 = static_cast<float>(psIndex * 64 + 64) / a.w;
            float y0 = 0.0f;
            float y1 = 1.0f; // 64 / 64
            dl->AddImage(a.id, pMin, pMax, ImVec2(x0, y0), ImVec2(x1, y1), tint);
            return;
        }

        const Rect&  r = kRects[i];
        const Atlas& a = g_atlas[r.atlas];
        if (!a.tex || a.w <= 0 || a.h <= 0)
            return;
        const ImVec2 uv0(static_cast<float>(r.x) / a.w, static_cast<float>(r.y) / a.h);
        const ImVec2 uv1(static_cast<float>(r.x + r.w) / a.w, static_cast<float>(r.y + r.h) / a.h);
        dl->AddImage(a.id, pMin, pMax, uv0, uv1, tint);
    }

    float DrawIconH(ImDrawList* dl, Icon ic, float x, float yCenter, float h, ImU32 tint)
    {
        const ImVec2 sz = IconSize(ic, h);
        if (sz.x <= 0.0f) return 0.0f;
        DrawIcon(dl, ic, ImVec2(x, yCenter - sz.y * 0.5f), ImVec2(x + sz.x, yCenter + sz.y * 0.5f), tint);
        return sz.x;
    }

    // --- Item icons ---------------------------------------------------------
    // A sprite name maps to its pak file by lowercasing it and appending
    // ".dds" - the names carry inconsistent casing in the game's own data
    // ("ItemIcon_Prefab_..." vs "itemIcon_..."), and the pak reader matches
    // exactly, so the fold is required, not cosmetic.
    namespace
    {
        void IconFileName(const char* sprite, char* out, size_t n)
        {
            size_t i = 0;
            for (; sprite[i] && i + 5 < n; ++i)
                out[i] = static_cast<char>(tolower(static_cast<unsigned char>(sprite[i])));
            snprintf(out + i, n - i, ".dds");
        }
    }

    bool DrawItemIcon(ImDrawList* dl, const char* sprite, ImVec2 pMin,
                      ImVec2 pMax, ImU32 tint)
    {
        if (!g_ready || !dl || !sprite || !sprite[0] || !g_dev)
            return false;

        char file[160];
        IconFileName(sprite, file, sizeof(file));

        auto it = g_itemIcons.find(file);
        if (it == g_itemIcons.end())
        {
            // First request: load from the pak and queue the GPU upload. The
            // entry is inserted either way so a miss is never retried.
            ItemIcon& ic = g_itemIcons[file];
            if (g_nextSlot < g_slotCount)
            {
                // A missing .dds is NORMAL and says nothing is wrong: the item
                // table names a sprite for thousands of items and plenty are
                // simply not shipped (placeholders like "CreateIcon", cut
                // content). The row just draws without an icon. Only shout when
                // the file EXISTS and we could not decode it - that is our bug,
                // not the game's data (it is how the BC1-rejected-as-BC3 bug
                // surfaced: "failed to load" with no pak error to explain it).
                std::vector<uint8_t> dds;
                if (!game::pak::ReadFile(12, "ui/texture/icon", file, dds, /*optional=*/true))
                {
                    // no icon for this item - nothing to report
                }
                else if (UploadDDS(g_dev, g_heap, g_inc, g_nextSlot, dds, ic.atlas))
                {
                    ++g_nextSlot;
                    ic.ok = true;
                }
                else
                {
                    char what[160];
                    DescribeDDS(dds, what, sizeof(what));
                    LOG_WARN("icons: item icon %s is present but could not be decoded: %s",
                             file, what);
                }
            }
            else
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LOG_WARN("icons: item-icon descriptor budget exhausted (%u slots) - "
                             "further icons will draw blank for the rest of the session. "
                             "Raise kSrvHeapSlots (dx12_hook.cpp) or add eviction.",
                             g_slotCount);
                }
            }
            it = g_itemIcons.find(file);
        }

        if (!it->second.ok)
            return false;
        dl->AddImage(it->second.atlas.id, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), tint);
        return true;
    }
}
