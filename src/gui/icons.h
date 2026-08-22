#pragma once

#include <imgui.h>

struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12GraphicsCommandList;

namespace trinity::ui
{
    // Logical icons Trinity draws, sourced from the game's own UI atlases
    // (loaded at startup via the pak reader). Tab glyphs come from
    // cd_icon_common_00; keyboard/controller prompt glyphs from the two
    // cd_icon_keyguide_* atlases. See the icon-pipeline notes for provenance.
    enum class Icon
    {
        None = 0,
        // Tab categories
        TabPlayer, TabTravel, TabItems, TabWorld, TabSystem,
        // Keyboard prompts
        KeyNav, KeyUp, KeyDown, KeyLeft, KeyRight,
        KeyEnter, KeyBackspace, KeyEsc, KeyTab, KeyDel, KeyQ, KeyE,
        // Controller prompts
        PadA, PadB, PadX, PadY, PadLB, PadRB, PadDpad,
        Count
    };

    // Loads atlases and uploads their textures. Call once, right after ImGui's
    // DX12 backend is initialised. `firstSlot` is the first free descriptor in
    // the (shader-visible) SRV heap - ImGui owns slot 0, so pass 1 - and
    // `slotCount` the heap's total capacity (slots past the fixed atlases are
    // handed to lazily-loaded item icons). Silently no-ops the whole icon
    // system if the game paks can't be read.
    void IconsInit(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap,
                   unsigned srvIncrement, unsigned firstSlot, unsigned slotCount);
    void IconsShutdown();

    // Records the one-time texture uploads (copy + state barrier) into the
    // overlay's own command list. Must be called once, while `list` is open,
    // BEFORE any icon is sampled - the copies then run in-order ahead of the
    // ImGui draws in the same list. No-op after the first successful call.
    void IconsRecordUploads(ID3D12GraphicsCommandList* list);

    // True once at least one atlas texture is live.
    bool IconsReady();

    // Aspect-correct on-screen size for `ic` at the given pixel height.
    ImVec2 IconSize(Icon ic, float height);

    // Blits `ic` into the rect, multiplied by `tint` (use alpha to dim). No-op
    // if the system isn't ready or `ic` is None.
    void DrawIcon(ImDrawList* dl, Icon ic, ImVec2 pMin, ImVec2 pMax,
                  ImU32 tint = IM_COL32_WHITE);

    // Convenience: draw left-aligned at (x, yCenter) at height `h`; returns the
    // drawn width so callers can advance a cursor.
    float DrawIconH(ImDrawList* dl, Icon ic, float x, float yCenter, float h,
                    ImU32 tint = IM_COL32_WHITE);

    // --- Item icons ---------------------------------------------------------
    // The game's own inventory art (one DDS per icon in pak chunk 0012,
    // ui/texture/icon), loaded lazily on first draw. `sprite` is the icon name
    // the GAME itself stores for the thing being drawn - hand it whatever
    // Inventory::GetItem / CategoryIcon returned ("ItemIcon_Prefab_cd_phm_02_sword_0039",
    // "ItemIcon_ItemGroup_twohand_weapon"); the file name is derived from it.
    // So this works for item icons and category icons alike.
    //
    // Render thread only - the deferred-upload path records each new texture's
    // GPU copy on the overlay command list ahead of the ImGui draws, so an icon
    // is safe to sample the same frame it is requested.

    // Blits the icon into the rect, loading it on first use. Returns false
    // (drawing nothing) if there is no sprite name, the pak read failed, or the
    // descriptor-heap budget for item icons is exhausted. A miss is normal -
    // some items genuinely have no icon - so callers should treat false as
    // "leave the box empty", not as an error.
    bool DrawItemIcon(ImDrawList* dl, const char* sprite, ImVec2 pMin,
                      ImVec2 pMax, ImU32 tint = IM_COL32_WHITE);
}
