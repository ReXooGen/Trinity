#include "menu.h"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Windows.h>
#include <Xinput.h>

#include "framework.h"
#include "widgets.h"
#include "../core/settings.h"
#include "../core/state.h"
#include "../core/text.h"
#include "../core/logger.h"
#include "../core/localization.h"
#include "../game/player.h"
#include "../game/teleport.h"
#include "../game/inventory.h"
#include "../game/world.h"
#include "../game/dye.h"
#include "../game/dye_data.h" // the game's dye families / preset shades (generated)
#include "../game/equipment.h"
#include "../game/friendly.h"
#include "../game/item_names.h"
#include "../core/version_detect.h"

namespace trinity::gui
{
    // Top-level sections. Always visible as the tab strip; Q/E/Tab or LB/RB
    // jump between them from anywhere, so nothing is ever more than a press
    // or two away.
    static const char* const kTabs[] = { "PLAYER", "INVENTORY", "TRAVEL", "WORLD", "SYSTEM" };
    enum Tab { TabPlayer, TabInventory, TabTravel, TabWorld, TabSystem, TabCount };

    static const char* KeyName(int vk);

    bool WantsDraw()
    {
        const State& st = State::Get();
        return st.menuOpen || st.showFps || ui::ToastsActive();
    }

    static void DrawFpsCounter()
    {
        ImGuiIO&    io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        char buf[64];
        // While Free Flight is on, light "FLY" whenever a direction is actively
        // driving your height (a fly key/button held while airborne), so it's
        // obvious when the controls have taken over. Harmless otherwise - FPS.
        if (State::Get().freeFlight)
        {
            const bool fly = game::Teleport::GetFlightEngaged();
            snprintf(buf, sizeof(buf), "%.0f FPS%s", io.Framerate, fly ? "  FLY" : "");
        }
        else
            snprintf(buf, sizeof(buf), "%.0f FPS", io.Framerate);

        const float  sz  = ImGui::GetFontSize();
        const ImVec2 ts  = ImGui::GetFont()->CalcTextSizeA(sz, 3.402823466e+38f, 0.0f, buf);
        const float  pad = sz * 0.4f;
        const ImVec2 mn(io.DisplaySize.x - ts.x - pad * 2.0f - sz, sz);
        const ImVec2 mx(mn.x + ts.x + pad * 2.0f, mn.y + ts.y + pad * 2.0f);

        dl->AddRectFilled(mn, mx, IM_COL32(7, 7, 9, 220));
        dl->AddText(ImGui::GetFont(), sz, ImVec2(mn.x + pad, mn.y + pad),
                    IM_COL32(214, 36, 56, 255), buf);
    }

    // --- Tab pages -----------------------------------------------------------

    // --- Mount & Horse Options -----------------------------------------------
    // PLAYER -> Mount & Horse Options
    // Manages gear dyeing and customization for mounts and horses.
    static void RenderMountOptions()
    {
        ui::Begin(LOC("Mount & Horse Options"));

        if (ui::Submenu(LOC("Mount Equipment Dye"), "dyeslots",
                    LOC("Recolor and customize equipment on your active horse or mount.")))
        {
            game::Dye::SetTargetMode(1);
        }

        ui::End();
    }

    // --- Combat & Gameplay Options --------------------------------------------
    // PLAYER -> Combat & Gameplay Options
    // Manages One-Hit Kill, God Mode, Durability, and damage multipliers.
    static void RenderCombatOptions()
    {
        State& st = State::Get();
        ui::Begin(LOC("Combat & Gameplay Options"));

        bool changed = false;
        changed |= ui::Toggle(LOC("One-Hit Kill"), &st.oneHitKill,
                   LOC("Instantly eliminates any enemy or boss with a single strike (1,000x damage multiplier)."));
        changed |= ui::Toggle(LOC("God Mode"), &st.godMode,
                   game::Player::Ready()
                        ? LOC("Keeps your health full constantly.")
                        : LOC("Keeps your health full. Load into the game world first."));
        changed |= ui::Toggle(LOC("Infinite Item Durability"), &st.infDurability,
                   LOC("Equipped weapons, shields, and armor never degrade (permanently locked at 100% max durability)."));
        changed |= ui::Toggle(LOC("No Fall Damage"), &st.noFallDamage,
                   LOC("Completely negates fall damage from high drops, cliffs, and Sky Arrival teleport."));
        changed |= ui::Toggle(LOC("Infinite Stamina & Mount"), &st.infStamina,
                   LOC("Sprint, dodge, climb, and gallop on horses/mounts with unlimited stamina."));
        changed |= ui::Toggle(LOC("Infinite Spirit"), &st.infSpirit,
                   LOC("Keeps your spirit / special ability gauge full."));
        changed |= ui::FloatOption(LOC("Outgoing Damage"), &st.dmgOutMult, 0.0f, 20.0f, 0.25f, 1.0f, "%.2fx",
                        LOC("Adjusts how much damage you deal to enemies."));
        changed |= ui::FloatOption(LOC("Incoming Damage"), &st.dmgInMult, 0.0f, 10.0f, 0.25f, 1.0f, "%.2fx",
                        LOC("Adjusts how much damage you take from attacks."));

        if (changed && st.autoSave)
            Settings::Save();
        ui::End();
    }

    static void RenderPlayer()
    {
        State& st = State::Get();
        ui::Begin();

        ui::Submenu(LOC("Combat & Gameplay Options"), "combat_options",
                    LOC("One-Hit Kill, God Mode, Durability, and damage multipliers."));

        if (ui::Submenu(LOC("Dye Equipment"), "dyeslots",
                    game::Dye::Ready()
                        ? LOC("Recolor your equipped gear.")
                        : LOC("Recolor your equipped gear. Load into the world first.")))
        {
            game::Dye::SetTargetMode(0);
        }

        ui::Submenu(LOC("Edit Equipment"), "equipslots",
                    game::Equipment::Ready()
                        ? LOC("Refine your gear and socket abyss gears into it.")
                        : LOC("Refine and socket your gear. Load into the world first."));

        ui::Submenu(LOC("Mount & Horse Options"), "mount_options",
                    LOC("Stamina, gear customization, and summon options for mounts and horses."));

        bool changed = false;
        changed |= ui::ToggleFloat(LOC("Super Run"), &st.superRun, &st.superRunMult, 1.0f, 10.0f, 0.25f, 2.0f, "%.2fx",
                        LOC("Move faster than normal."));
        changed |= ui::ToggleFloat(LOC("Super Jump"), &st.superJump, &st.superJumpMult, 1.0f, 10.0f, 0.25f, 2.0f, "%.2fx",
                        LOC("Jump higher than normal."));
        changed |= ui::ToggleFloat(LOC("Free Flight"), &st.freeFlight, &st.flightSpeed, 1.0f, 40.0f, 1.0f, 8.0f, "%.0f",
                        LOC("While airborne, hold Caps Lock / RB to rise or Ctrl / Right Trigger to sink. Let go and normal physics resume - jumps and aerial attacks are untouched."));
        changed |= ui::ToggleFloat(LOC("Trust Multiplier"), &st.trustMult, &st.trustMultVal, 1.0f, 25.0f, 0.25f, 3.0f, "%.2fx",
                        game::Friendly::Ready()
                            ? LOC("Gifting NPCs or feeding animals builds trust faster.")
                            : LOC("Gifting NPCs or feeding animals builds trust faster. Unavailable right now."));

        if (changed && st.autoSave)
            Settings::Save();
        ui::End();
    }

    // --- Dye editor -----------------------------------------------------------
    // PLAYER -> Dye Equipment -> (equipped piece) -> preset swatches, with
    // custom RGB one level down. Applies through the game's own dyehouse
    // client path (see game/dye.h), so a picked color renders instantly and
    // persists like a real dye job - which is why the presets need no
    // separate Apply step: the swatch IS the apply.
    //
    // The presets are the dyehouse's own palette (dye_data.h): ten families,
    // each 9 neutral tones + a 10x10 shade grid, exactly what the in-game
    // dye UI offers. The family key is written into the record so the game's
    // own UI files the color correctly; custom RGB reuses the same path with
    // no palette restriction.

    static uint16_t s_dyeTag = 0;    // selected slot's engine tag
    static char s_dyeItem[64];       // its item name - the edit pages' title
    static int s_dyeChan   = 0;      // 0 = all zones, 1..12 = one zone
    static int s_dyeFamily = 0;      // index into kDyeFamilies
    static int s_dyeR = 200, s_dyeG = 30, s_dyeB = 40; // the custom mix
    static int s_dyeMat    = 0;      // 0 = natural, 1..10 = engine material template
    static int s_dyeRepair = 100;    // 100 = pristine .. 0 = battle-worn
    static int s_dyeCursor[1 + game::kDyeGridRows]; // swatch focus per grid row

    // Poll the queued apply for its one-shot outcome (Status is read-and-clear).
    static void ReportPendingDye()
    {
        const game::Dye::OpState s = game::Dye::Status();
        if (s == game::Dye::OpState::Done)
            ui::Toast("Dye applied");
        else if (s == game::Dye::OpState::Failed)
            ui::Toast("Could not dye that - see the log");
    }

    static void SendDye(uint32_t familyKey, int r, int g, int b)
    {
        game::Dye::Channel c{};
        c.groupKey   = familyKey;
        c.r          = static_cast<uint8_t>(r);
        c.g          = static_cast<uint8_t>(g);
        c.b          = static_cast<uint8_t>(b);
        c.materialId = (s_dyeMat == 0) ? uint16_t(0xFFFF) : static_cast<uint16_t>(s_dyeMat);
        c.repair     = static_cast<uint8_t>(((100 - s_dyeRepair) * 127) / 100);
        if (game::Dye::Apply(s_dyeTag, s_dyeChan - 1, c))
            ui::Toast("Applying dye...");
        // The custom rows follow whatever was applied last, so the Custom
        // Color page always opens on the color the item just got.
        s_dyeR = r; s_dyeG = g; s_dyeB = b;
    }

    // Material / condition edits on an already-dyed zone re-apply that zone's
    // own color with the new settings, so scrubbing the value previews live.
    // Debounced (the queue takes one request at a time), single-zone only: an
    // "all zones" retouch would repaint every zone with zone 1's color.
    static bool      s_dyeRetouch   = false;
    static ULONGLONG s_dyeRetouchAt = 0;

    static void PumpDyeRetouch()
    {
        if (!s_dyeRetouch || GetTickCount64() - s_dyeRetouchAt < 350)
            return;
        if (s_dyeChan == 0) { s_dyeRetouch = false; return; }

        game::Dye::Channel cc{};
        if (!game::Dye::GetChannel(s_dyeTag, s_dyeChan - 1, &cc))
        {
            // Nothing dyed here yet - the settings ride along with the next
            // color pick instead.
            s_dyeRetouch = false;
            return;
        }
        game::Dye::Channel c{};
        c.groupKey   = cc.groupKey;
        c.r = cc.r; c.g = cc.g; c.b = cc.b;
        c.materialId = (s_dyeMat == 0) ? uint16_t(0xFFFF) : static_cast<uint16_t>(s_dyeMat);
        c.repair     = static_cast<uint8_t>(((100 - s_dyeRepair) * 127) / 100);
        if (game::Dye::Apply(s_dyeTag, s_dyeChan - 1, c))
            s_dyeRetouch = false; // else the queue was busy - retry next frame
    }

    static void RenderDyeSlots()
    {
        ui::Begin();

        const bool isMount = (game::Dye::GetTargetMode() == 1);
        if (isMount)
        {
            static const char* const kMountNames[] = { "Active Mount", "Mount 2", "Mount 3", "Mount 4" };
            int mountIdx = game::Dye::GetActiveMount();
            if (ui::Combo(LOC("Target Mount"), &mountIdx, kMountNames, 4, LOC("Select active horse or mount to dye.")))
            {
                game::Dye::SetActiveMount(mountIdx);
            }
        }
        else
        {
            static const char* const kCharNames[] = { "Kliff", "Damiane", "Oongka" };
            int dyeChar = game::Dye::GetActiveCharacter();
            if (ui::Combo(LOC("Character"), &dyeChar, kCharNames, 3, LOC("Select which character's armor to dye.")))
            {
                game::Dye::SetActiveCharacter(dyeChar);
            }
        }

        if (!game::Dye::Ready())
        {
            if (isMount)
            {
                int mountIdx = game::Dye::GetActiveMount();
                if (mountIdx > 0)
                    ui::Option(LOC("Mount Not Detected in World"),
                               LOC("This mount is not currently spawned or present in the game world."));
                else
                    ui::Option(LOC("No Active Mount Detected"),
                               LOC("Please summon or mount a horse in the game world first."));
            }
            else
            {
                int dyeChar = game::Dye::GetActiveCharacter();
                if (dyeChar > 0)
                    ui::Option(LOC("Character not loaded"),
                               LOC("This companion is not currently loaded in memory."));
                else
                    ui::Option(LOC("Waiting for your equipment..."),
                               LOC("Load into the world - if this persists, change any equipment piece once so the mod can see your gear."));
            }
            ui::End();
            return;
        }

        if (ui::Option(LOC("Inject All Dyes to Save Data"),
                       LOC("Persist all current dye colors directly into the active save data so they remain intact even without mods.")))
        {
            if (game::Dye::InjectAllToSave())
                ui::Toast(LOC("All dyes injected into save data"));
            else
                ui::Toast(LOC("Dye save injection completed"));
        }

        const int n = game::Dye::SlotCount();
        int shown = 0, hidden = 0;
        char hiddenList[200] = "";

        for (int i = 0; i < n; ++i)
        {
            game::Dye::SlotInfo si{};
            if (!game::Dye::GetSlot(i, &si)) continue;

            // Pieces with no dye channels are collected into one summary row
            // below instead of cluttering the list with dead ends.
            if (!si.dyeable)
            {
                const size_t len = strlen(hiddenList);
                snprintf(hiddenList + len, sizeof(hiddenList) - len, "%s%s",
                         hidden ? ", " : "", si.itemName);
                ++hidden;
                continue;
            }
            ++shown;

            char label[160];
            if (si.dyeCount > 0)
                snprintf(label, sizeof(label), "%s - %s  (%u/%d %s)",
                         si.slotName, si.itemName, si.dyeCount, si.maxZones, LOC("zones dyed"));
            else
                snprintf(label, sizeof(label), "%s - %s  (%d %s)",
                         si.slotName, si.itemName, si.maxZones, LOC("zones"));

            if (ui::SubmenuItem(label, si.icon[0] ? si.icon : nullptr, "dyeedit",
                                "Recolor this piece."))
            {
                // A different piece gets fresh pages (selection, scroll); the
                // same piece keeps them, so hopping out and back in is free.
                if (s_dyeTag != si.tag || strcmp(s_dyeItem, si.itemName) != 0)
                {
                    ui::ResetMenu("dyeedit");
                    ui::ResetMenu("dyecustom");
                    s_dyeRetouch = false;
                }
                s_dyeTag = si.tag;
                snprintf(s_dyeItem, sizeof(s_dyeItem), "%s", si.itemName);
            }
        }

        if (n == 0)
            ui::Option("Nothing equipped", "Equip some gear first.");
        else if (shown == 0)
            ui::Option("Nothing dyeable equipped", "None of these pieces can be dyed.");

        if (hidden > 0)
        {
            char label[48];
            snprintf(label, sizeof(label), "Can't be dyed: %d piece%s",
                     hidden, hidden == 1 ? "" : "s");
            char desc[256];
            snprintf(desc, sizeof(desc), "%s", hiddenList);
            ui::Option(label, desc);
        }

        ui::End();
    }

    static void RenderDyeEdit()
    {
        ui::Begin(s_dyeItem[0] ? s_dyeItem : nullptr);

        game::Dye::SlotInfo curSlot{};
        bool haveSlot = false;
        const int nSlots = game::Dye::SlotCount();
        for (int i = 0; i < nSlots; ++i)
        {
            if (game::Dye::GetSlot(i, &curSlot) && curSlot.tag == s_dyeTag)
            {
                haveSlot = true;
                break;
            }
        }
        const int maxZones = 12;

        static const char* const kZoneItems[] = {
            "All zones", "Zone 1", "Zone 2", "Zone 3", "Zone 4", "Zone 5", "Zone 6",
            "Zone 7", "Zone 8", "Zone 9", "Zone 10", "Zone 11", "Zone 12"
        };
        const int comboCount = 1 + maxZones;
        if (s_dyeChan >= comboCount) s_dyeChan = 0;

        static const char* s_famItems[game::kDyeFamilyCount];
        static bool s_famInit = false;
        if (!s_famInit)
        {
            for (int i = 0; i < game::kDyeFamilyCount; ++i)
                s_famItems[i] = game::kDyeFamilies[i].name;
            s_famInit = true;
        }

        ui::Combo(LOC("Dye Zone"), &s_dyeChan, kZoneItems, comboCount,
                  LOC("Which zone of the item to color (Supports Zones 1-12)."));
        ui::Combo("Color Family", &s_dyeFamily, s_famItems, game::kDyeFamilyCount,
                  "Pick a color family to browse its shades below.");

        const game::DyeFamily& fam = game::kDyeFamilies[s_dyeFamily];

        // The zone's current color, marked with a dot on its swatch below.
        game::Dye::Channel cur{};
        const bool haveCur = game::Dye::GetChannel(
            s_dyeTag, (s_dyeChan == 0) ? 0 : s_dyeChan - 1, &cur);

        // Row 0: the family's 9 neutral tones, led by a "remove dye" swatch so
        // the row is 10 wide like the rest and clearing lives right in the
        // palette; rows 1..10: the 10x10 grid, darker down the rows, richer to
        // the right - the dyehouse's own layout. Picking a swatch acts at once.
        for (int row = 0; row <= game::kDyeGridRows; ++row)
        {
            const bool neutral = (row == 0);
            const int  lead    = neutral ? 1 : 0; // the remove swatch
            const int  base    = neutral ? 0
                               : game::kDyeNeutrals + (row - 1) * game::kDyeGridCols;
            const int  shades  = neutral ? game::kDyeNeutrals : game::kDyeGridCols;
            const int  cnt     = lead + shades;

            uint32_t rgb[game::kDyeGridCols] = {};
            int      mark = -1;
            for (int i = 0; i < shades; ++i)
            {
                const game::DyeShadeRGB& sh = fam.shades[base + i];
                rgb[lead + i] = (uint32_t(sh.r) << 16) | (uint32_t(sh.g) << 8) | sh.b;
                if (haveCur && sh.r == cur.r && sh.g == cur.g && sh.b == cur.b)
                    mark = lead + i;
            }

            const int hit = ui::SwatchRow("", rgb, cnt, &s_dyeCursor[row], mark,
                neutral ? "Pick a tone, or the first swatch to remove the dye."
                        : "Pick a color to dye it right away.",
                neutral ? 0 : -1);
            if (hit >= 0)
            {
                if (neutral && hit == 0)
                {
                    if (game::Dye::Clear(s_dyeTag, s_dyeChan - 1))
                        ui::Toast("Removing dye...");
                }
                else
                {
                    const game::DyeShadeRGB& sh = fam.shades[base + hit - lead];
                    SendDye(fam.key, sh.r, sh.g, sh.b);
                }
            }
        }

        ui::Submenu("Custom Color", "dyecustom", "Mix your own color instead of a preset.");

        if (ui::Option("Remove All Dye (Reset)", "Clears all dye from this piece back to its natural default color."))
        {
            if (game::Dye::Clear(s_dyeTag, -1))
                ui::Toast("All dye removed");
        }

        bool touched = false;
        touched |= ui::IntOption("Material", &s_dyeMat, 0, 10, 1, 0,
                      "Swap the fabric or metal look. 0 keeps it natural.");
        touched |= ui::IntOption("Condition %", &s_dyeRepair, 0, 100, 5, 100,
                      "How worn the piece looks. 100 is pristine, 0 is battle-scarred.");
        if (touched && s_dyeChan != 0)
        {
            s_dyeRetouch   = true;
            s_dyeRetouchAt = GetTickCount64();
        }
        PumpDyeRetouch();

        ui::End();
    }

    static void RenderDyeCustom()
    {
        ui::Begin(s_dyeItem[0] ? s_dyeItem : nullptr);

        ui::IntOption("Red",   &s_dyeR, 0, 255, 5, 200, "Red 0-255.");
        ui::IntOption("Green", &s_dyeG, 0, 255, 5, 30,  "Green 0-255.");
        ui::IntOption("Blue",  &s_dyeB, 0, 255, 5, 40,  "Blue 0-255.");

        const uint32_t mix = (uint32_t(s_dyeR) << 16) |
                             (uint32_t(s_dyeG) << 8)  | uint32_t(s_dyeB);
        static int s_applyCursor = 0;
        if (ui::SwatchRow("Apply This Color", &mix, 1, &s_applyCursor, -1,
                          "Dye it with this exact color.") == 0)
            SendDye(game::kDyeFamilies[s_dyeFamily].key, s_dyeR, s_dyeG, s_dyeB);

        if (ui::Option("Load Current", "Load the zone's current color."))
        {
            const int ch = (s_dyeChan == 0) ? 0 : s_dyeChan - 1;
            game::Dye::Channel c{};
            if (game::Dye::GetChannel(s_dyeTag, ch, &c))
            {
                s_dyeR = c.r; s_dyeG = c.g; s_dyeB = c.b;
                s_dyeMat    = (c.materialId == 0xFFFF || c.materialId > 10) ? 0 : c.materialId;
                s_dyeRepair = (c.repair == 0xFF) ? 100 : 100 - (c.repair * 100 + 63) / 127;
                for (int i = 0; i < game::kDyeFamilyCount; ++i)
                    if (game::kDyeFamilies[i].key == c.groupKey) { s_dyeFamily = i; break; }
                ui::Toast("Loaded zone %d", ch + 1);
            }
            else
            {
                ui::Toast("That zone has no dye yet");
            }
        }

        ui::End();
    }

    // --- Equipment editor (abyss-gear sockets) --------------------------------
    // PLAYER -> Edit Equipment -> (equipped piece) -> per-socket gear picker.
    // Adding/clearing a gear writes both realms and persists like dye; unlocking
    // sockets renders this session only (see game/equipment.h).

    static uint16_t s_eqTag = 0xFFFF; // selected piece's engine tag
    static char     s_eqItem[64] = "";// its item name - the edit/picker title
    static int      s_eqSocket = 0;   // socket index the picker is editing
    static char     s_eqFind[48] = "";// gear picker search
    static int      s_eqRefine = 0;   // refinement stepper value (seeded on select)
    static int      s_eqCharFilter = 1; // 0 = All Characters, 1 = Current Character Only, 2 = Kliff, 3 = Damiane, 4 = Oongka
    static int      s_eqCategoryFilter = 1; // 0 = All Categories, 1 = Matching Slot Only, 2 = Weapons, 3 = Shields & Off-Hand, 4 = Armor, 5 = Accessories

    // Locate the live snapshot slot for a tag (SlotCount() rebuilds it first).
    static bool EqSlotForTag(uint16_t tag, game::Equipment::SlotInfo* out)
    {
        const int n = game::Equipment::SlotCount();
        for (int i = 0; i < n; ++i)
            if (game::Equipment::GetSlot(i, out) && out->tag == tag)
                return true;
        return false;
    }

    static void RenderEquipSlots()
    {
        ui::Begin();

        static const char* const kCharNames[] = { "Kliff", "Damiane", "Oongka" };
        int eqChar = game::Equipment::GetActiveCharacter();
        if (ui::Combo(LOC("Character"), &eqChar, kCharNames, 3, LOC("Select which character's equipment to view and edit.")))
        {
            game::Equipment::SetActiveCharacter(eqChar);
        }

        if (!game::Equipment::Ready())
        {
            if (eqChar > 0)
                ui::Option(LOC("Character not loaded"),
                           LOC("This companion is not currently loaded in memory."));
            else
                ui::Option(LOC("Waiting for your equipment..."), LOC("Load into the world first."));
            ui::End();
            return;
        }

        if (ui::Option(LOC("Repair All Gear"), LOC("Restores durability to 100% on all equipped weapons and armor.")))
        {
            int repaired = 0;
            if (game::Equipment::RepairAll(&repaired))
                ui::Toast(LOC("Repaired %d equipped piece%s"), repaired, repaired == 1 ? "" : "s");
            else
                ui::Toast(LOC("No equipped gear to repair"));
        }

        if (ui::Option(LOC("Max Refine All (+10)"), LOC("Sets all equipped gear to refinement level +10.")))
        {
            int refined = 0;
            if (game::Equipment::RefineAll(10, &refined))
                ui::Toast(LOC("Refined %d piece%s to +10"), refined, refined == 1 ? "" : "s");
            else
                ui::Toast(LOC("No equipped gear to refine"));
        }

        if (ui::Option(LOC("Unlock All Sockets"), LOC("Opens all 5 abyss sockets on all equipped weapons and armor.")))
        {
            int unlocked = 0;
            if (game::Equipment::UnlockAllGears(&unlocked))
                ui::Toast(LOC("Unlocked sockets on %d piece%s"), unlocked, unlocked == 1 ? "" : "s");
            else
                ui::Toast(LOC("No equipped gear found"));
        }

        const int n = game::Equipment::SlotCount();
        for (int i = 0; i < n; ++i)
        {
            game::Equipment::SlotInfo si{};
            if (!game::Equipment::GetSlot(i, &si)) continue;

            char label[176];
            if (si.maxSockets > 0)
                snprintf(label, sizeof(label), "%s - %s  (%d/%d %s)",
                         LOC(si.slotName), si.itemName, si.filledCount, si.maxSockets,
                         LOC("sockets used"));
            else
                snprintf(label, sizeof(label), "%s - %s  (%s)",
                         LOC(si.slotName), si.itemName, LOC("no sockets"));

            if (ui::SubmenuItem(label, si.icon[0] ? si.icon : nullptr, "equipedit",
                                LOC("Refine this piece and edit its abyss-gear sockets.")))
            {
                // A different piece gets a fresh picker page.
                if (s_eqTag != si.tag || strcmp(s_eqItem, si.itemName) != 0)
                    ui::ResetMenu("equipgear");
                s_eqTag = si.tag;
                s_eqRefine = si.refineLevel; // seed the stepper from the live level
                snprintf(s_eqItem, sizeof(s_eqItem), "%s", si.itemName);
            }
        }
        if (n == 0)
            ui::Option(LOC("Nothing equipped"), LOC("Equip some gear first."));

        ui::End();
    }

    static void RenderEquipEdit()
    {
        ui::Begin(s_eqItem[0] ? s_eqItem : nullptr);

        game::Equipment::SlotInfo si{};
        if (!EqSlotForTag(s_eqTag, &si))
        {
            ui::Option(LOC("Not equipped"), LOC("This piece is no longer equipped."));
            ui::End();
            return;
        }

        if (si.maxSockets > 0 && si.unlockedCount < si.maxSockets)
        {
            char desc[128];
            snprintf(desc, sizeof(desc), "%s (%d).", LOC("Opens every socket on this piece"), si.maxSockets);
            if (ui::Option(LOC("Unlock all sockets"), desc))
            {
                if (game::Equipment::UnlockAll(si.tag))
                    ui::Toast(LOC("All sockets unlocked"));
                else
                    ui::Toast(LOC("Could not unlock - see the log"));
            }
        }
        if (si.filledCount > 0)
        {
            if (ui::Option(LOC("Clear all sockets"),
                           LOC("Removes every abyss gear from this piece, leaving the sockets open.")))
            {
                if (game::Equipment::ClearAll(si.tag))
                    ui::Toast(LOC("All sockets cleared"));
                else
                    ui::Toast(LOC("Could not clear - see the log"));
            }
        }

        // Refinement (0..10) - applies to every piece, sockets or not, so it sits
        // above the socket-only early-out. Left/Right steps the level; each change
        // writes both realms and persists.
        {
            int lvl = s_eqRefine;
            if (ui::IntOption(LOC("Refinement"), &lvl, 0, game::Equipment::kRefineMax, 1, si.refineLevel,
                              LOC("Refine this piece from 0 to 10.")))
            {
                s_eqRefine = lvl;
                bool p = false;
                if (game::Equipment::SetRefine(si.tag, lvl, &p))
                    ui::Toast(p ? LOC("Refinement set") : LOC("Refinement set (this session)"));
                else
                    ui::Toast(LOC("Could not set refinement - see the log"));
            }
        }

        // Force Equip / Change Piece option (bypasses class, level, and quest story progression locks)
        if (ui::SubmenuItem(LOC("Change Equipment (Bypass Story / Quest Lock)"), nullptr, "equipswap",
                            LOC("Directly replace and equip any weapon, shield, or armor to this slot.")))
        {
            s_eqFind[0] = 0;
            ui::ResetMenu("equipswap");
        }

        if (si.maxSockets == 0)
        {
            ui::Option(LOC("No sockets"), LOC("This equipment type does not support abyss sockets."));
            ui::End();
            return;
        }

        if (!game::Equipment::EditsPersist())
            ui::Option(LOC("Note: not saving yet"),
                       LOC("Your save is still loading - gear edits apply visually but revert on reload until this clears."));

        for (int k = 0; k < si.maxSockets; ++k)
        {
            const game::Equipment::Socket& so = si.sockets[k];
            char label[112];
            if (!so.unlocked)
            {
                snprintf(label, sizeof(label), "%s %d: %s", LOC("Socket"), k + 1, LOC("Locked"));
                if (ui::Option(label, LOC("This socket is locked. Click to unlock all sockets on this piece.")))
                {
                    if (game::Equipment::UnlockAll(si.tag))
                        ui::Toast(LOC("Sockets unlocked!"));
                }
            }
            else
            {
                snprintf(label, sizeof(label), "%s %d: %s", LOC("Socket"), k + 1,
                         so.filled ? so.gearName : LOC("Empty"));
                if (ui::SubmenuItem(label, (so.filled && so.gearIcon[0]) ? so.gearIcon : nullptr,
                                    "equipgear",
                                    so.filled ? LOC("Change or remove this abyss gear.")
                                              : LOC("Add an abyss gear to this socket.")))
                {
                    s_eqSocket = k;
                    s_eqFind[0] = 0;
                    ui::ResetMenu("equipgear");
                }
            }
        }

        ui::End();
    }

    static void RenderEquipSwap()
    {
        char title[112];
        const char* slotName = game::Equipment::SlotNameForTag(s_eqTag);
        snprintf(title, sizeof(title), "%s - %s [%s]", LOC("Equip Item"), s_eqItem, slotName ? LOC(slotName) : "");
        ui::Begin(title);

        // 1. Character Filter
        static const char* const kCharFilters[] = {
            "All Characters",
            "Current Character Only",
            "Kliff Only",
            "Damiane Only",
            "Oongka Only"
        };
        ui::Combo(LOC("Character Filter"), &s_eqCharFilter, kCharFilters, 5,
                  LOC("Filter equipment suitable for this character or show all items."));

        // 2. Category / Slot Filter
        static const char* const kCategoryFilters[] = {
            "All Categories",
            "Matching Slot Only",
            "Weapons",
            "Shields & Off-Hand",
            "Armor",
            "Accessories"
        };
        ui::Combo(LOC("Category Filter"), &s_eqCategoryFilter, kCategoryFilters, 6,
                  LOC("Filter items by weapon, armor, shield, or slot compatibility."));

        ui::Search(s_eqFind, sizeof(s_eqFind), LOC("Find any weapon, shield, or armor to equip."));

        const int activeChar = game::Equipment::GetActiveCharacter();
        int filterChar = -1;
        if (s_eqCharFilter == 1) filterChar = activeChar;
        else if (s_eqCharFilter == 2) filterChar = 0;
        else if (s_eqCharFilter == 3) filterChar = 1;
        else if (s_eqCharFilter == 4) filterChar = 2;

        const int nCats = game::Inventory::CatalogCategoryCount();
        int shown = 0;
        for (int c = 0; c < nCats && shown < 200; ++c)
        {
            const int nItems = game::Inventory::CatalogItemCount(c);
            for (int i = 0; i < nItems && shown < 200; ++i)
            {
                game::Inventory::ItemInfo it{};
                if (!game::Inventory::GetCatalogItem(c, i, &it)) continue;
                if (it.typeId == 0 || it.typeId == 0xFFFF) continue;

                // Character Filter
                if (filterChar >= 0 && !game::Equipment::IsItemForCharacter(filterChar, it.typeId, it.name, it.key))
                    continue;

                // Category & Slot Filter
                if (s_eqCategoryFilter == 1) // Matching Slot Only
                {
                    if (!game::Equipment::IsItemForSlot(s_eqTag, it.typeId, it.name, it.key))
                        continue;
                }
                else if (s_eqCategoryFilter == 2) // Weapons
                {
                    if (!game::Equipment::IsItemForSlot(0, it.typeId, it.name, it.key) &&
                        !game::Equipment::IsItemForSlot(2, it.typeId, it.name, it.key))
                        continue;
                }
                else if (s_eqCategoryFilter == 3) // Shields & Off-Hand
                {
                    if (!game::Equipment::IsItemForSlot(1, it.typeId, it.name, it.key))
                        continue;
                }
                else if (s_eqCategoryFilter == 4) // Armor
                {
                    if (!game::Equipment::IsItemForSlot(3, it.typeId, it.name, it.key) &&
                        !game::Equipment::IsItemForSlot(4, it.typeId, it.name, it.key) &&
                        !game::Equipment::IsItemForSlot(5, it.typeId, it.name, it.key) &&
                        !game::Equipment::IsItemForSlot(6, it.typeId, it.name, it.key))
                        continue;
                }
                else if (s_eqCategoryFilter == 5) // Accessories
                {
                    if (!game::Equipment::IsItemForSlot(7, it.typeId, it.name, it.key))
                        continue;
                }

                // Search Filter
                if (s_eqFind[0] && !ContainsNoCase(it.name, s_eqFind) && !ContainsNoCase(it.key, s_eqFind))
                    continue;

                ++shown;

                char desc[192];
                const char* catName = game::Inventory::CatalogCategoryName(c);
                snprintf(desc, sizeof(desc), "%s [%s]",
                         LOC("Equip to active slot - bypasses quest & class lock"),
                         catName ? LOC(catName) : "");

                if (ui::OptionItem(it.name, it.icon[0] ? it.icon : nullptr, desc))
                {
                    if (game::Equipment::EquipItemToSlot(s_eqTag, it.typeId))
                    {
                        ui::Toast(LOC("Equipped %s!"), it.name);
                        snprintf(s_eqItem, sizeof(s_eqItem), "%s", it.name);
                        ui::PopMenu();
                    }
                    else
                    {
                        ui::Toast(LOC("Could not equip item"));
                    }
                    ui::End();
                    return;
                }
            }
        }

        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("No equipment found with current character/category filter."));
        else if (shown >= 200)
            ui::Option(LOC("More matches..."), LOC("Showing first 200 items - use Search to narrow down."));

        ui::End();
    }

    static void RenderEquipGear()
    {
        char title[112];
        snprintf(title, sizeof(title), "%s - Socket %d", s_eqItem, s_eqSocket + 1);
        ui::Begin(title);

        // Clearing the socket is always the first choice (icon box left empty so
        // it lines up with the gear rows below).
        if (ui::OptionItem("- Empty this socket -", nullptr, "Remove whatever gear is in this socket."))
        {
            bool p = false;
            if (game::Equipment::ClearGear(s_eqTag, s_eqSocket, &p))
            {
                ui::Toast(p ? "Socket cleared" : "Socket cleared (this session)");
                ui::PopMenu();
            }
            else
                ui::Toast("Could not clear - see the log");
            ui::End();
            return;
        }

        const int total = game::Equipment::GearCount();
        if (total == 0)
        {
            ui::Option("No abyss gears found",
                       "The game's Abyss Gear catalog did not resolve.");
            ui::End();
            return;
        }

        ui::Search(s_eqFind, sizeof(s_eqFind), "Find an abyss gear by name.");

        int shown = 0;
        for (int i = 0; i < total && shown < 200; ++i)
        {
            uint16_t    tid  = 0;
            const char* name = nullptr;
            const char* icon = nullptr;
            if (!game::Equipment::GetGear(i, &tid, &name, &icon)) continue;
            if (s_eqFind[0] && !ContainsNoCase(name, s_eqFind)) continue;
            ++shown;

            if (ui::OptionItem(name, (icon && icon[0]) ? icon : nullptr, "Socket this abyss gear."))
            {
                bool p = false;
                if (game::Equipment::AddGear(s_eqTag, s_eqSocket, tid, &p))
                {
                    ui::Toast(p ? "Socketed %s" : "Socketed %s (this session)", name);
                    ui::PopMenu();
                }
                else
                    ui::Toast("Could not socket that - see the log");
                ui::End();
                return;
            }
        }
        if (shown == 0)
            ui::Option("No matches", "No abyss gear is called that.");
        else if (shown >= 200)
            ui::Option("More matches...", "Only the first 200 are listed - keep typing to narrow it down.");

        ui::End();
    }

    static void RenderTimePresets()
    {
        ui::Begin();
        const bool timeReady = game::World::TimeOfDayReady();

        int curDay = 0, curHour = 0, curMin = 0;
        if (game::World::GetCurrentTimeOfDay(&curDay, &curHour, &curMin))
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s: %s %d, %02d:%02d", LOC("Current Time"), LOC("Day"), curDay, curHour, curMin);
            ui::Option(buf, LOC("Current in-game world time."));
        }

        if (ui::Option(LOC("Dawn / Morning (06:00)"), timeReady ? LOC("Set clock and sun to 06:00 AM (Sunrise).") : LOC("Unavailable right now.")))
        {
            if (game::World::SetTimeOfDay(6))
                ui::Toast(LOC("Set time to 06:00 (Dawn)"));
        }

        if (ui::Option(LOC("Midday / Noon (12:00)"), timeReady ? LOC("Set clock and sun to 12:00 PM (Full Daylight).") : LOC("Unavailable right now.")))
        {
            if (game::World::SetTimeOfDay(12))
                ui::Toast(LOC("Set time to 12:00 (Noon)"));
        }

        if (ui::Option(LOC("Sunset / Golden Hour (18:00)"), timeReady ? LOC("Set clock and sun to 06:00 PM (Dusk).") : LOC("Unavailable right now.")))
        {
            if (game::World::SetTimeOfDay(18))
                ui::Toast(LOC("Set time to 18:00 (Sunset)"));
        }

        if (ui::Option(LOC("Midnight / Night (00:00)"), timeReady ? LOC("Set clock and sun to 12:00 AM (Deep Night).") : LOC("Unavailable right now.")))
        {
            if (game::World::SetTimeOfDay(0))
                ui::Toast(LOC("Set time to 00:00 (Midnight)"));
        }

        static int s_customHour = 12;
        if (ui::IntAction(LOC("Set Exact Hour"), &s_customHour, 0, 23, 1, 12, timeReady ? LOC("Set clock directly to this exact hour (0..23).") : LOC("Unavailable right now.")))
        {
            if (game::World::SetTimeOfDay(s_customHour))
                ui::Toast(LOC("Set time to %02d:00"), s_customHour);
        }

        ui::End();
    }

    static void RenderWeatherAtmosphere()
    {
        State& st = State::Get();
        ui::Begin();

        bool changed = false;

        // 1. Weather Presets & Overrides
        static const char* s_weathers[] = {
            "Dynamic (Game Default)",
            "Clear Sky (Sunny)",
            "Overcast (Cloudy)",
            "Rainy (Light Rain)",
            "Thunderstorm (Storm)",
            "Dense Fog / Mist"
        };
        int wIdx = st.weatherPreset;
        if (ui::Combo(LOC("Weather Preset"), &wIdx, s_weathers, 6, LOC("Select and lock active weather atmosphere.")))
        {
            st.weatherPreset = wIdx;
            game::World::SetWeatherPreset(wIdx);
            changed = true;
            ui::Toast(LOC("Weather set to %s"), s_weathers[wIdx]);
        }

        changed |= ui::Toggle(LOC("Clear Distant Fog"), &st.clearDistantFog, LOC("Reduces thick distant fog blur for enhanced scenery and visibility."));
        changed |= ui::Toggle(LOC("Force Clear Sky"), &st.forceClearSky, LOC("Forces constant clear sky with minimal cloud cover."));

        if (ui::Option(LOC("Instant Clear Weather"), LOC("Immediately disperses rain, storms, and heavy fog.")))
        {
            st.forceClearSky = true;
            st.clearDistantFog = true;
            st.weatherPreset = 1;
            game::World::SetWeatherPreset(1);
            game::World::SetClearDistantFog(true);
            changed = true;
            ui::Toast(LOC("Atmosphere cleared"));
        }

        // 2. Precipitation & Storm Intensities
        changed |= ui::FloatOption(LOC("Rain Intensity"), &st.rainIntensity, 0.0f, 5.0f, 0.10f, 0.0f, "%.2f", LOC("Adjust rainfall density and storm clouds."));
        changed |= ui::FloatOption(LOC("Snow Intensity"), &st.snowIntensity, 0.0f, 5.0f, 0.10f, 0.0f, "%.2f", LOC("Adjust snowfall and winter blizzard intensity."));
        changed |= ui::FloatOption(LOC("Dust / Sandstorm"), &st.dustIntensity, 0.0f, 5.0f, 0.10f, 0.0f, "%.2f", LOC("Adjust desert dust and sandstorm particles."));

        // 3. Cloud Layer Controls
        changed |= ui::FloatOption(LOC("Cloud Thickness"), &st.cloudThick, 0.0f, 5.0f, 0.10f, 1.0f, "%.2f", LOC("Controls cloud volume density and sky coverage."));
        changed |= ui::FloatOption(LOC("Cloud Top Altitude"), &st.cloudTop, 0.1f, 3.0f, 0.05f, 1.0f, "%.2f", LOC("Adjusts top elevation ceiling of cloud formations."));
        changed |= ui::FloatOption(LOC("Cloud Base Altitude"), &st.cloudBase, 0.1f, 3.0f, 0.05f, 1.0f, "%.2f", LOC("Adjusts lower boundary elevation of cloud bottoms."));
        changed |= ui::FloatOption(LOC("Cloud Drift Speed"), &st.cloudScrollSpeed, 0.0f, 5.0f, 0.1f, 1.0f, "%.2fx", LOC("Scales motion speed of cloud layer drifting across the sky."));

        // 4. Fog & Atmospheric Depth
        changed |= ui::FloatOption(LOC("Fog Scattering (A)"), &st.fogA, 0.0f, 5.0f, 0.10f, 1.0f, "%.2f", LOC("Adjusts foreground fog density and light scattering."));
        changed |= ui::FloatOption(LOC("Fog Horizon Blend (B)"), &st.fogB, 0.0f, 5.0f, 0.10f, 1.0f, "%.2f", LOC("Adjusts distant horizon fog and mountain mist blend."));

        // 5. Environmental Wind & Turbulence
        changed |= ui::FloatOption(LOC("Wind Speed Multiplier"), &st.windMultiplier, 0.0f, 5.0f, 0.1f, 1.0f, "%.2fx", LOC("Scales ambient wind force and environmental breeze."));
        changed |= ui::FloatOption(LOC("Wind Gust Strength"), &st.windGust, 0.0f, 3.0f, 0.1f, 1.0f, "%.2f", LOC("Controls frequency and intensity of random wind gusts."));
        changed |= ui::FloatOption(LOC("Turbulence Lift"), &st.windTurbLift, 0.0f, 3.0f, 0.1f, 1.0f, "%.2f", LOC("Controls vertical air currents and particle lift."));
        changed |= ui::Toggle(LOC("No Wind"), &st.noWind, LOC("Calms all environmental wind and dust to complete stillness."));

        if (changed && st.autoSave)
            Settings::Save();

        ui::End();
    }

    static void RenderWorld()
    {
        State& st = State::Get();
        ui::Begin();

        const bool ready = game::World::Ready();
        bool changed = false;
        changed |= ui::ToggleFloat(LOC("Game Speed"), &st.gameSpeed, &st.gameSpeedMult, 0.1f, 5.0f, 0.05f, 1.0f, "%.2fx",
                   ready
                       ? LOC("Speeds up or slows down the game.")
                       : LOC("Speeds up or slows down the game. Unavailable right now."));

        const bool timeReady = game::World::TimeOfDayReady();
        changed |= ui::Toggle(LOC("Freeze Time of Day"), &st.timeFrozen,
                   timeReady
                       ? LOC("Holds the clock at the current time; the world keeps running normally.")
                       : LOC("Holds the clock in place. Unavailable right now."));

        // Advance Time (Forward)
        static int s_advHours = 1;
        if (ui::IntAction(LOC("Advance Time (+)"), &s_advHours, 1, 240, 1, 1,
                   timeReady
                       ? LOC("Skips the clock forward by this many hours; time keeps flowing after.")
                       : LOC("Skips the clock forward. Unavailable right now.")))
        {
            if (game::World::AdvanceTimeOfDayHours(s_advHours))
                ui::Toast(LOC("Advanced %d hours"), s_advHours);
        }

        // Rewind Time (Backward)
        static int s_rewHours = 1;
        if (ui::IntAction(LOC("Rewind Time (-)"), &s_rewHours, 1, 240, 1, 1,
                   timeReady
                       ? LOC("Rewinds the clock backward by this many hours; time keeps flowing after.")
                       : LOC("Rewinds the clock backward. Unavailable right now.")))
        {
            if (game::World::AdvanceTimeOfDayHours(-s_rewHours))
                ui::Toast(LOC("Rewound %d hours"), s_rewHours);
        }

        ui::Submenu(LOC("Time of Day Presets"), "world_time_presets", LOC("Set exact time to Morning, Noon, Sunset, or Midnight."));
        ui::Submenu(LOC("Weather & Atmosphere"), "world_weather", LOC("Control weather, sky condition, and remove distance fog."));

        if (changed && st.autoSave)
            Settings::Save();

        ui::End();
    }

    static void RenderTravel()
    {
        State& st = State::Get();
        ui::Begin();

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (game::Teleport::GetLastPosition(&x, &y, &z))
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "X %.2f  Y %.2f  Z %.2f", x, y, z);
            if (ui::Option(buf, LOC("Copy these coordinates to the clipboard.")))
            {
                if (game::Teleport::CopyPositionToClipboard())
                    ui::Toast(LOC("Coordinates copied to clipboard"));
            }
        }
        else
        {
            ui::Option(LOC("No position yet"), LOC("Load into the world and take a step first."));
        }

        // Map Marker Teleport
        float mx = 0.0f, my = 0.0f, mz = 0.0f;
        const bool hasMarker = game::Teleport::GetMarkerPosition(&mx, &my, &mz);

        char markerBtnLabel[128];
        char markerBtnDesc[192];
        if (hasMarker)
        {
            if (my == 0.0f)
                snprintf(markerBtnLabel, sizeof(markerBtnLabel), "%s: X %.0f  Y (%s)  Z %.0f", LOC("Teleport to Destination"), mx, LOC("Sky"), mz);
            else
                snprintf(markerBtnLabel, sizeof(markerBtnLabel), "%s: X %.0f  Y %.0f  Z %.0f", LOC("Teleport to Destination"), mx, my, mz);
            snprintf(markerBtnDesc, sizeof(markerBtnDesc), "%s (%s: %s).",
                     LOC("Teleport to active map destination"), LOC("Hotkey"),
                     KeyName(st.markerTeleportKeyVk));
        }
        else
        {
            snprintf(markerBtnLabel, sizeof(markerBtnLabel), "%s: %s", LOC("Teleport to Destination"), LOC("None"));
            snprintf(markerBtnDesc, sizeof(markerBtnDesc), "%s", LOC("Set a destination/waypoint on the world map first."));
        }

        if (ui::Option(markerBtnLabel, markerBtnDesc))
        {
            const auto res = game::Teleport::TeleportToMarker(st.markerFallbackHeight);
            switch (res)
            {
            case game::Teleport::MarkerStatus::Success:
                ui::Toast(LOC("Teleported to destination"));
                break;
            case game::Teleport::MarkerStatus::NoMarker:
                ui::Toast(LOC("No destination found on map"));
                break;
            case game::Teleport::MarkerStatus::NoPlayer:
                ui::Toast(LOC("Player not ready"));
                break;
            case game::Teleport::MarkerStatus::InvalidCoordinates:
                ui::Toast(LOC("Invalid destination coordinates"));
                break;
            case game::Teleport::MarkerStatus::UnsafeContext:
                ui::Toast(LOC("Unsafe destination context"));
                break;
            default:
                ui::Toast(LOC("Destination teleport failed"));
                break;
            }
        }

        if (ui::FloatOption(LOC("Sky Arrival Altitude"), &st.markerFallbackHeight, 50.0f, 3000.0f, 25.0f, 550.0f, "%.0f",
                            LOC("Altitude to teleport to when destination marker does not specify height.")))
        {
            if (st.autoSave)
                Settings::Save();
        }

        // Saved Locations (Bookmarks)
        ui::Submenu(LOC("Saved Locations"), "saved_locs", LOC("Save and teleport to custom bookmarked coordinates."));

        // The game's own fast-travel network: every map gimmick (fast-travel
        // points, ores, chests, shops, bells, dungeons...), grouped by category.
        if (ui::Submenu(LOC("Fast Travel"), "ftcats",
                        LOC("Warp to any location on the map.")))
        {
            game::Teleport::LoadCatalog();
        }

        ui::End();
    }

    // Shared state for the fast-travel category / node browser.
    static size_t s_ftCat        = 0;
    static char   s_ftFilter[48] = "";

    // Draws a `ui::Search` filter box, then `renderRow(i)` for i in
    // [0, total) - each call fetches + filter-tests + draws row i,
    // returning whether it passed the filter. Draws `noMatchDesc` under a
    // "No matches" row if every row was filtered out. Shared by every
    // long, searchable list menu (fast-travel nodes, inventory items) so
    // the search/counter/footer plumbing lives in one place.
    template <typename RenderRow>
    static void RenderFilteredList(size_t total, char* filterBuf, size_t filterCap,
                                   const char* searchDesc, const char* noMatchDesc,
                                   RenderRow renderRow)
    {
        ui::Search(filterBuf, filterCap, searchDesc);

        size_t shown = 0;
        for (size_t i = 0; i < total; ++i)
            if (renderRow(i))
                ++shown;
        if (shown == 0)
            ui::Option("No matches", noMatchDesc);
    }

    // Renders one `ui::Submenu` row per category (built into `label` by
    // `renderLabel(i, label, cap)`), pushing into `targetMenu`. Clears
    // `filterBuf` and resets `targetMenu`'s scroll/selection when the
    // selected category changes - a fresh list makes a leftover
    // filter/selection point at unrelated rows - then updates *currentCat
    // and calls onSelect(i). Shared by the fast-travel and inventory
    // category lists.
    // `iconFor` returns the category's game icon sprite name, or nullptr for a
    // list that has none (the fast-travel one).
    template <typename Index, typename RenderLabel, typename IconFor, typename OnSelect>
    static void RenderCategoryList(Index count, const char* targetMenu, const char* desc,
                                   Index* currentCat, char* filterBuf,
                                   RenderLabel renderLabel, IconFor iconFor, OnSelect onSelect)
    {
        for (Index i = 0; i < count; ++i)
        {
            char label[144];
            if (!renderLabel(i, label, sizeof(label))) continue;
            if (ui::SubmenuItem(label, iconFor(i), targetMenu, desc))
            {
                if (*currentCat != i)
                {
                    filterBuf[0] = 0;
                    ui::ResetMenu(targetMenu);
                }
                *currentCat = i;
                onSelect(i);
            }
        }
    }

    static void RenderFastTravelCats()
    {
        ui::Begin();
        ui::ListJump();

        if (!game::Teleport::LoadCatalog())
        {
            ui::Option("Building destination list...",
                       "Loading - if this persists, load into the world first.");
            ui::End();
            return;
        }

        const size_t n = game::Teleport::CategoryCount();
        RenderCategoryList(n, "ftnodes", "Browse and warp to this category's locations.",
                           &s_ftCat, s_ftFilter,
                           [](size_t i, char* label, size_t cap)
                           {
                               const char* name = nullptr;
                               size_t count = 0;
                               if (!game::Teleport::GetCategory(i, &name, &count)) return false;
                               snprintf(label, cap, "%s  (%zu)", name, count);
                               return true;
                           },
                           [](size_t) -> const char* { return nullptr; },
                           [](size_t i) { game::Teleport::EnsureCategoryNodes(i); });

        ui::End();
    }

    static void RenderFastTravelNodes()
    {
        ui::Begin();
        ui::ListJump();

        game::Teleport::EnsureCategoryNodes(s_ftCat);
        const size_t total = game::Teleport::NodeCount(s_ftCat);
        if (total == 0)
        {
            ui::Option("No locations", "Nothing to warp to in this category.");
            ui::End();
            return;
        }

        // One continuous list: the framework scrolls it, the breadcrumb bar
        // shows "row / total", Left/Right and PgUp/PgDn jump a screen at a
        // time, and the search row filters it live.
        RenderFilteredList(total, s_ftFilter, sizeof(s_ftFilter),
            "Search this list by name.",
            "No locations match this search.",
            [&](size_t i)
            {
                const char* label = nullptr;
                float nx = 0.0f, ny = 0.0f, nz = 0.0f;
                if (!game::Teleport::GetNode(s_ftCat, i, &label, &nx, &ny, &nz)) return false;
                if (s_ftFilter[0] && !ContainsNoCase(label, s_ftFilter)) return false;

                char desc[128];
                snprintf(desc, sizeof(desc), "Fast travel to %s at %.0f, %.0f, %.0f.",
                         label, nx, ny, nz);

                if (ui::Option(label, desc))
                {
                    if (game::Teleport::TravelToNode(s_ftCat, i))
                        ui::Toast("Warping to %s", label);
                }
                return true;
            });

        ui::End();
    }

    static int s_curLocIdx = -1;

    static void GetLocationDisplayName(const State::SavedLocation& loc, size_t index, char* out, size_t cap)
    {
        if (!loc.name[0])
        {
            snprintf(out, cap, "%s #%zu", LOC("Location"), index + 1);
            return;
        }

        int num = 0;
        if (sscanf(loc.name, "Location #%d", &num) == 1 ||
            sscanf(loc.name, "위치 #%d", &num) == 1 ||
            sscanf(loc.name, "位置 #%d", &num) == 1)
        {
            snprintf(out, cap, "%s #%d", LOC("Location"), num);
            return;
        }
        if (!strcmp(loc.name, "Saved Spot") || !strcmp(loc.name, "Saved Location"))
        {
            snprintf(out, cap, "%s #%zu", LOC("Location"), index + 1);
            return;
        }

        snprintf(out, cap, "%s", loc.name);
    }

    static void RenderSavedLocations()
    {
        State& st = State::Get();
        ui::Begin();

        float px = 0.0f, py = 0.0f, pz = 0.0f;
        const bool havePlayer = game::Teleport::GetLastPosition(&px, &py, &pz);

        if (ui::Option(LOC("+ Save Current Location"), LOC("Saves your active player coordinates as a new custom bookmark.")))
        {
            if (havePlayer)
            {
                State::SavedLocation loc{};
                loc.x = px;
                loc.y = py;
                loc.z = pz;
                snprintf(loc.name, sizeof(loc.name), "Location #%zu", st.savedLocations.size() + 1);
                st.savedLocations.push_back(loc);
                Settings::Save();

                char dispName[64];
                GetLocationDisplayName(loc, st.savedLocations.size() - 1, dispName, sizeof(dispName));
                ui::Toast(LOC("Saved '%s' (X %.0f, Y %.0f, Z %.0f)"), dispName, px, py, pz);
            }
            else
            {
                ui::Toast(LOC("Player position not ready"));
            }
        }

        if (st.savedLocations.empty())
        {
            ui::Option(LOC("No saved locations"), LOC("Click '+ Save Current Location' above to bookmark any spot."));
        }
        else
        {
            for (size_t i = 0; i < st.savedLocations.size(); ++i)
            {
                const auto& loc = st.savedLocations[i];
                char dispName[64];
                GetLocationDisplayName(loc, i, dispName, sizeof(dispName));

                char label[144];
                char desc[192];
                snprintf(label, sizeof(label), "%zu. %s  (X %.0f  Y %.0f  Z %.0f)",
                         i + 1, dispName, loc.x, loc.y, loc.z);
                snprintf(desc, sizeof(desc), "%s", LOC("Enter/A: Open details  |  Del/X: Delete location"));

                const ui::BookmarkAction action = ui::BookmarkRow(label, desc);
                if (action == ui::BookmarkAction::Open)
                {
                    s_curLocIdx = static_cast<int>(i);
                    ui::PushMenu("loc_manage", dispName);
                }
                else if (action == ui::BookmarkAction::Delete)
                {
                    char deletedName[64];
                    snprintf(deletedName, sizeof(deletedName), "%s", dispName);
                    st.savedLocations.erase(st.savedLocations.begin() + i);
                    Settings::Save();
                    ui::Toast(LOC("Deleted '%s'"), deletedName);
                    break;
                }
            }

            if (ui::Option(LOC("Clear All Saved Locations"), LOC("Deletes all custom bookmarked locations.")))
            {
                st.savedLocations.clear();
                Settings::Save();
                ui::Toast(LOC("All saved locations cleared"));
            }
        }

        ui::End();
    }

    static void RenderSavedLocationManage()
    {
        State& st = State::Get();
        if (s_curLocIdx < 0 || s_curLocIdx >= static_cast<int>(st.savedLocations.size()))
        {
            ui::Begin();
            ui::Option(LOC("Location not found"), LOC("This saved location no longer exists."));
            ui::End();
            return;
        }

        auto& loc = st.savedLocations[s_curLocIdx];
        char dispName[64];
        GetLocationDisplayName(loc, s_curLocIdx, dispName, sizeof(dispName));
        ui::Begin(dispName);

        char tpLabel[128];
        snprintf(tpLabel, sizeof(tpLabel), "%s (X %.0f  Y %.0f  Z %.0f)", LOC("Teleport Here"), loc.x, loc.y, loc.z);
        if (ui::Option(tpLabel, LOC("Instantly warp your player to these coordinates.")))
        {
            if (game::Teleport::TeleportToCoordinates(loc.x, loc.y, loc.z))
                ui::Toast(LOC("Teleported to %s"), dispName);
            else
                ui::Toast(LOC("Teleport failed"));
        }

        if (ui::TextInput(LOC("Name"), loc.name, sizeof(loc.name), LOC("Press Enter/A to rename this location on your keyboard.")))
        {
            Settings::Save();
        }

        float px = 0.0f, py = 0.0f, pz = 0.0f;
        const bool havePlayer = game::Teleport::GetLastPosition(&px, &py, &pz);
        if (ui::Option(LOC("Update to Current Position"), LOC("Overwrites the saved coordinates with your current position.")))
        {
            if (havePlayer)
            {
                loc.x = px;
                loc.y = py;
                loc.z = pz;
                Settings::Save();
                ui::Toast(LOC("Updated '%s' to X %.0f, Y %.0f, Z %.0f"), dispName, px, py, pz);
            }
            else
            {
                ui::Toast(LOC("Player position not ready"));
            }
        }

        if (ui::Option(LOC("Delete This Location"), LOC("Removes this location from your bookmarks.")))
        {
            st.savedLocations.erase(st.savedLocations.begin() + s_curLocIdx);
            Settings::Save();
            ui::Toast(LOC("Location deleted"));
            ui::PopMenu();
        }

        ui::End();
    }

    // --- Inventory -----------------------------------------------------------
    // Storage -> category -> item. The storage level is the game's own: your
    // pack, Private Storage, the Wardrobe and the Bank are separate storages
    // that all hang off the one holder, so without this level a stack in the
    // Bank and one in your pack are indistinguishable rows.
    //
    // Quantities are edited on the item's own row (ui::ItemRow) rather than in
    // a popup, so the list you browse is the list you edit.
    static int  s_invStore       = 0;
    static int  s_invCat         = 0;
    // Two separate filters, because they do different jobs: the storage page
    // searches EVERY category at once (find an item without knowing which
    // category the game files it under), the category page filters just its own
    // list. Sharing one buffer would carry a storage-wide search into a
    // category that has none of the matches.
    static char s_invFind[48]    = "";
    static char s_invFilter[48]  = "";
    // Add Item mirrors all of that, over the game's catalog instead of what you
    // own: its own category index, and the same two-filter split for the same
    // reason (s_invAddFind searches every category at once, s_invAddFilter just
    // the open one). The amount to add is not here - it lives on the item row
    // itself (ui::ItemAddRow), exactly like a quantity does in the Editor.
    static int  s_invAddCat        = 0;
    static char s_invAddFind[48]   = "";
    static char s_invAddFilter[48] = "";
    // Last storage the user actually entered, so re-entering the same one keeps
    // your place in its category list while switching storages starts fresh.
    static int  s_invStorePrev   = -1;
    // The amount Set All writes. Shared by every category rather than kept per
    // one: it is a number you are about to type anyway, and "999 in this
    // category, 12 in that one" is state nobody asked to keep.
    static int  s_invSetAllQty   = 999;
    // The amount Add All queues for each item, the Add Item mirror of the above.
    // Its own default (a modest count, since this is one press per whole
    // category, not per item) and, like Set All, shared across every category.
    static int  s_invAddAllQty   = 5;

    // Identity of one item row for ui::ItemRow, which needs to notice when a
    // row's item changes under an in-progress edit. Position alone is not
    // enough (the item at an index changes as stacks come and go) and type
    // alone is not either (a storage can hold two stacks of one item), so it
    // takes both. The +1 on `st` keeps the key non-zero, which ItemRow reads as
    // "no identity".
    static unsigned long long ItemKey(int st, int cat, int idx, uint16_t typeId)
    {
        return (static_cast<unsigned long long>(st + 1) & 0xFF) << 56 |
               (static_cast<unsigned long long>(cat + 1) & 0xFF) << 48 |
               (static_cast<unsigned long long>(idx) & 0xFFFF)   << 16 |
               static_cast<unsigned long long>(typeId);
    }

    // The same identity for a CATALOG row, in its own space: `st` there is a
    // storage index and real storages number under twenty, so 0x7F can never
    // collide with one. Catalog and inventory rows never share a page, but the
    // widgets that key off these are shared, and a key meaning two things is
    // the kind of bug that shows up once as an inexplicable glitch.
    static unsigned long long CatalogKey(int cat, int idx, uint16_t typeId)
    {
        return ItemKey(0x7F - 1, cat, idx, typeId);
    }

    // Whether item (st, cat, idx) is one the list would show under `filter`,
    // handing back its info when it is. RenderItemRow decides exactly this, but
    // Set All has to know the answer BEFORE the list is drawn (it says how many
    // items it is about to hit), so the rule lives here where both can read it
    // rather than being written out twice and drifting apart.
    static bool IsUncategorised(int st, int cat)
    {
        const char* name = game::Inventory::CategoryName(st, cat);
        return name && _stricmp(name, "Uncategorised") == 0;
    }

    static bool ItemRecordSafe(const game::Inventory::ItemInfo& it)
    {
        if (!it.name || !it.name[0] || it.qty <= 0 || it.qty >= 2147483000LL)
            return false;
        if (it.typeId == 0 || it.typeId == 0xFFFF)
            return false;
        return true;
    }

    static bool ItemShown(int st, int cat, int idx, const char* filter,
                          game::Inventory::ItemInfo* out)
    {
        game::Inventory::ItemInfo it{};
        if (!game::Inventory::GetItemInfo(st, cat, idx, &it)) return false;
        if (!ItemRecordSafe(it)) return false;
        if (filter && filter[0] && !SearchMatches(it.name, filter) && !SearchMatches(it.key, filter)) return false;
        if (out) *out = it;
        return true;
    }

    // Draws item `idx` of (st, cat) as an editable row and applies whatever was
    // done to it. Returns false when the row does not exist or the filter
    // rejected it, so callers can count what they actually showed. `showCat`
    // names the item's category in the description - for the storage-wide
    // search, where the rows come from all over and otherwise would not say.
    // `locked` is the page's, not the row's: EditsPersist() reads live memory,
    // and a storage-wide search runs this for every item in the storage.
    static bool RenderItemRow(int st, int cat, int idx, const char* filter,
                              bool showCat, bool locked)
    {
        game::Inventory::ItemInfo it{};
        if (!ItemShown(st, cat, idx, filter, &it)) return false;

        char desc[224];
        if (locked)
            snprintf(desc, sizeof(desc),
                     "Editing is locked until your save finishes loading.");
        else if (showCat)
            snprintf(desc, sizeof(desc), "%s, in %s.",
                     it.name, game::Inventory::CategoryName(st, cat));
        else
            snprintf(desc, sizeof(desc), "Change how many you have, or remove it.");

        long long nq = 0;
        const ui::ItemEdit e = ui::ItemRow(it.name, it.icon, it.qty,
                                           ItemKey(st, cat, idx, it.typeId),
                                           locked, &nq, desc);
        if (e == ui::ItemEdit::SetQty)
        {
            // No refresh needed: SetQuantity updates the snapshot too, so a held
            // Left/Right steps off the value it just wrote instead of stalling
            // on a stale one until the refresh throttle lets go.
            game::Inventory::SetQuantity(st, cat, idx, nq);
        }
        else if (e == ui::ItemEdit::Remove)
        {
            if (game::Inventory::RemoveItem(st, cat, idx))
            {
                ui::Toast("Removed %s", it.name);
                game::Inventory::ForceRefresh(); // drop the row now, not in 120ms
            }
        }
        return true;
    }

    // The Inventory tab's own page: the entry point into the item browser/
    // editor (moved to its own submenu so this page has room for inventory-
    // wide settings that aren't about any one item), plus two overrides that
    // write straight into the game's data tables - Slot Size re-stamps every
    // storage's slot count, Max Stack Size re-stamps every item's stack cap.
    // Both are all-at-once, not per-item/per-storage, on purpose: the ask was
    // "one number for everything", not a per-row editor.
    static void RenderInventoryHome()
    {
        State& st = State::Get();
        ui::Begin();

        ui::Submenu(LOC("Money & Currency"), "invmoney", LOC("Directly add money, pouches, chests, or custom currency."));
        ui::Submenu(LOC("Abyss Items & Artifacts"), "invabyss", LOC("Quickly spawn Abyss Artifacts, Sealed Artifacts, Seeds, and Cells."));
        ui::Submenu(LOC("Add Item"), "invadd", LOC("Add any item in the game to your inventory."));
        ui::Submenu(LOC("Restore Items"), "invrestore", LOC("Recover sold, discarded, or missing documents, wanted notices, collectibles & unique quest items."));
        ui::Submenu(LOC("Item Editor"), "invedit", LOC("Browse and edit what you're carrying."));

        bool changed = false;
        if (ui::ToggleInt(LOC("Slot Size"), &st.invSlotSize, &st.invSlotSizeVal,
                          1, 700, 10, 700,
                          LOC("Sets every storage's slot count up to 700 safely directly in RAM.")))
            changed = true;
        if (ui::Toggle(LOC("Max Stack Size"), &st.invStackSize,
                       LOC("Enables universal stack limits and automatically merges all duplicate items into 1 slot.")))
            changed = true;
        if (ui::IntOption(LOC("Set Max Stack Value"), &st.invStackSizeVal,
                          1, 999999999, 10000, 999999,
                          LOC("Enter custom max stack size using keyboard or arrow keys (Press ENTER to type).")))
            changed = true;

        if (changed && st.autoSave)
            Settings::Save();

        ui::End();
    }

    static void RenderInventoryEditor()
    {
        ui::Begin();

        if (!game::Inventory::Ready())
        {
            ui::Option(LOC("Loading inventory..."),
                       LOC("Reading your items - if this persists, open your in-game inventory once and come back here."));
            ui::End();
            return;
        }

        game::Inventory::Refresh();

        // One row per storage that actually holds something. Listed first so
        // the row selected on arrival is a storage - the thing this tab is for -
        // and not the housekeeping below it.
        const int n = game::Inventory::StorageCount();
        RenderCategoryList(n, "invstore", LOC("Browse and edit what is kept in this storage."),
                           &s_invStore, s_invFind,
                           [](int s, char* label, size_t cap)
                           {
                               const int cnt = game::Inventory::StorageItemCount(s);
                               if (cnt == 0) return false;
                               snprintf(label, cap, "%s  (%d)", LOC(game::Inventory::StorageName(s)), cnt);
                               return true;
                           },
                           [](int) -> const char* { return nullptr; }, // storages carry no icon
                           [](int s)
                           {
                               if (s_invStorePrev == s) return;
                               s_invStorePrev = s;
                               s_invCat = 0;
                               ui::ResetMenu("invcat");
                           });

        if (ui::Option(LOC("Refresh"), LOC("Reloads your inventory from the game.")))
        {
            game::Inventory::ForceRefresh();
            ui::Toast(LOC("Inventory refreshed"));
        }

        ui::End();
    }

    // One storage: its categories, or - once the search box has anything in it -
    // the matching items from every one of them, editable right here. That is
    // the shortcut for the common case, where you know the item's name but not
    // the category the game keeps it in.
    static void RenderInventoryStorage()
    {
        ui::Begin(LOC(game::Inventory::StorageName(s_invStore)));

        game::Inventory::Refresh();

        const int n = game::Inventory::CategoryCount(s_invStore);
        if (n == 0)
        {
            ui::Option(LOC("Empty"), LOC("Nothing in this storage right now."));
            ui::End();
            return;
        }

        // Left/Right belong to the item rows when searching; page jumps would
        // eat them (ListJump consumes both), so they are only mapped while this
        // page is a plain category list.
        if (!s_invFind[0])
            ui::ListJump();

        ui::Search(s_invFind, sizeof(s_invFind),
                   LOC("Find an item anywhere in this storage, whatever category it is in."));

        if (!s_invFind[0])
        {
            RenderCategoryList(n, "invcat", LOC("Browse and edit items in this category."),
                               &s_invCat, s_invFilter,
                               [](int c, char* label, size_t cap)
                               {
                                   const int cnt = game::Inventory::ItemCount(s_invStore, c);
                                   if (cnt == 0) return false;
                                   snprintf(label, cap, "%s  (%d)",
                                            LOC(game::Inventory::CategoryName(s_invStore, c)), cnt);
                                   return true;
                               },
                               [](int c) { return game::Inventory::CategoryIcon(s_invStore, c); },
                               [](int) {});
            ui::End();
            return;
        }

        const bool locked = !game::Inventory::EditsPersist();
        int shown = 0;
        for (int c = 0; c < n; ++c)
        {
            const int items = game::Inventory::ItemCount(s_invStore, c);
            for (int i = 0; i < items; ++i)
                if (RenderItemRow(s_invStore, c, i, s_invFind, /*showCat=*/true, locked))
                    ++shown;
        }
        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("Nothing in this storage is called that."));

        ui::End();
    }

    static void RenderSetAll(int shown, bool locked)
    {
        char desc[224];
        if (locked)
            snprintf(desc, sizeof(desc), "%s",
                     LOC("Editing is locked until your save finishes loading."));
        else if (shown == 0)
            snprintf(desc, sizeof(desc), "%s", LOC("Nothing here to set."));
        else if (s_invFilter[0])
            snprintf(desc, sizeof(desc), "%s", LOC("Sets matching items below to this amount."));
        else
            snprintf(desc, sizeof(desc), "%s", LOC("Sets every item in this category to this amount."));

        const bool inert = locked || shown == 0;
        if (!ui::IntAction(LOC("Set All"), &s_invSetAllQty, 1, 999999999, 1, 999, desc) ||
            inert)
            return;

        const int total = game::Inventory::ItemCount(s_invStore, s_invCat);
        int done = 0;
        for (int i = 0; i < total; ++i)
            if (ItemShown(s_invStore, s_invCat, i, s_invFilter, nullptr) &&
                game::Inventory::SetQuantity(s_invStore, s_invCat, i, s_invSetAllQty))
                ++done;

        ui::Toast(LOC("Set %d items to x%d"), done, s_invSetAllQty);
    }

    static void RenderInventoryCat()
    {
        ui::Begin(LOC(game::Inventory::CategoryName(s_invStore, s_invCat)));

        game::Inventory::Refresh();
        const int total = game::Inventory::ItemCount(s_invStore, s_invCat);
        if (total == 0)
        {
            ui::Option(LOC("Empty"), LOC("Nothing in this category right now."));
            ui::End();
            return;
        }

        ui::Search(s_invFilter, sizeof(s_invFilter), LOC("Narrow this category down by name."));

        const bool locked = !game::Inventory::EditsPersist();

        int shown = 0;
        for (int i = 0; i < total; ++i)
            if (ItemShown(s_invStore, s_invCat, i, s_invFilter, nullptr))
                ++shown;

        RenderSetAll(shown, locked);

        for (int i = 0; i < total; ++i)
            RenderItemRow(s_invStore, s_invCat, i, s_invFilter, /*showCat=*/false, locked);
        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("Nothing in this category is called that."));

        ui::End();
    }

    static void ReportPendingAdd()
    {
        static game::Inventory::AddState s_lastAdd = game::Inventory::AddState::Idle;
        const game::Inventory::AddState now = game::Inventory::AddStatus();
        if (now == s_lastAdd) return;
        if (now == game::Inventory::AddState::Added)
        {
            ui::Toast(LOC("Item added"));
            game::Inventory::ForceRefresh();
        }
        else if (now == game::Inventory::AddState::Failed)
        {
            ui::Toast(LOC("Could not add that item - see the log"));
        }
        s_lastAdd = now;
    }

    static void ReportBulkAdd()
    {
        static bool s_wasActive = false;
        const game::Inventory::BulkAdd b = game::Inventory::BulkAddStatus();
        if (s_wasActive && !b.active)
        {
            if (b.failed == 0)
                ui::Toast(LOC("Added %d items"), b.added);
            else
                ui::Toast(LOC("Added %d items, some failed"), b.added);
            game::Inventory::ForceRefresh();
        }
        s_wasActive = b.active;
    }

    static bool RenderAddRow(int cat, int idx, const char* filter, bool showCat, bool locked)
    {
        game::Inventory::ItemInfo it{};
        if (!game::Inventory::GetCatalogItem(cat, idx, &it)) return false;
        if (filter && filter[0] && !SearchMatches(it.name, filter) && !SearchMatches(it.key, filter)) return false;

        char desc[224];
        if (locked)
            snprintf(desc, sizeof(desc), "%s", LOC("Adding is locked until your save finishes loading."));
        else if (showCat)
            snprintf(desc, sizeof(desc), "%s (%s)",
                     it.name, LOC(game::Inventory::CatalogCategoryName(cat)));
        else
            snprintf(desc, sizeof(desc), "%s", LOC("Set how many, then add it."));

        const unsigned long long key = CatalogKey(cat, idx, it.typeId);
        const long long n = ui::ItemAddRow(it.name, it.icon, key, locked, desc);
        if (n > 0)
        {
            if (game::Inventory::AddItem(it.typeId, n))
                ui::Toast(LOC("Adding %lld x %s..."), n, it.name);
            else
                ui::Toast(LOC("Could not add %s"), it.name);
        }
        return true;
    }

    static void RenderInventoryAdd()
    {
        ui::Begin();
        ReportPendingAdd();
        ReportBulkAdd();

        const int n = game::Inventory::CatalogCategoryCount();
        if (n == 0)
        {
            ui::Option(LOC("Catalog unavailable"),
                       LOC("The game's item table did not resolve, so there is nothing to add from."));
            ui::End();
            return;
        }

        if (!s_invAddFind[0])
            ui::ListJump();

        ui::Search(s_invAddFind, sizeof(s_invAddFind),
                   LOC("Find any item in the game by name, whatever category it is in."));

        if (!s_invAddFind[0])
        {
            RenderCategoryList(n, "invaddcat", LOC("Add any item from this category."),
                               &s_invAddCat, s_invAddFilter,
                               [](int c, char* label, size_t cap)
                               {
                                   const int cnt = game::Inventory::CatalogItemCount(c);
                                   if (cnt == 0) return false;
                                   snprintf(label, cap, "%s  (%d)",
                                            LOC(game::Inventory::CatalogCategoryName(c)), cnt);
                                   return true;
                               },
                               [](int c) { return game::Inventory::CatalogCategoryIcon(c); },
                               [](int) {});
            ui::End();
            return;
        }

        const bool locked = !game::Inventory::EditsPersist();
        int shown = 0;
        for (int c = 0; c < n && shown < 200; ++c)
        {
            const int items = game::Inventory::CatalogItemCount(c);
            for (int i = 0; i < items && shown < 200; ++i)
                if (RenderAddRow(c, i, s_invAddFind, /*showCat=*/true, locked))
                    ++shown;
        }
        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("No item in the game is called that."));
        else if (shown >= 200)
            ui::Option(LOC("More matches..."), LOC("Only the first 200 are listed - keep typing to narrow it down."));

        ui::End();
    }

    static void RenderAddAll(int cat, const char* filter, int shown, bool locked)
    {
        char desc[224];
        if (locked)
            snprintf(desc, sizeof(desc), "%s", LOC("Adding is locked until your save finishes loading."));
        else if (shown == 0)
            snprintf(desc, sizeof(desc), "%s", LOC("Nothing here to add."));
        else if (filter && filter[0])
            snprintf(desc, sizeof(desc), "%s", LOC("Adds this many of each matching item below."));
        else
            snprintf(desc, sizeof(desc), "%s", LOC("Adds this many of every item in this category."));

        const bool inert = locked || shown == 0;
        if (!ui::IntAction(LOC("Add All"), &s_invAddAllQty, 1, 999999999, 1, 5, desc) || inert)
            return;

        std::vector<uint16_t> ids;
        ids.reserve(static_cast<size_t>(shown));
        const int total = game::Inventory::CatalogItemCount(cat);
        for (int i = 0; i < total; ++i)
        {
            game::Inventory::ItemInfo it{};
            if (!game::Inventory::GetCatalogItem(cat, i, &it)) continue;
            if (filter && filter[0] && !ContainsNoCase(it.name, filter)) continue;
            ids.push_back(it.typeId);
        }
        if (ids.empty()) return;

        const int count = static_cast<int>(ids.size());
        if (game::Inventory::AddItemsBulk(ids.data(), count, s_invAddAllQty))
            ui::Toast(LOC("Adding %d of each - %d items..."), s_invAddAllQty, count);
        else
            ui::Toast(LOC("Still adding the last batch - let it finish first"));
    }

    static void RenderInventoryAddCat()
    {
        ui::Begin(LOC(game::Inventory::CatalogCategoryName(s_invAddCat)));
        ReportPendingAdd();
        ReportBulkAdd();

        const int total = game::Inventory::CatalogItemCount(s_invAddCat);
        if (total == 0)
        {
            ui::Option(LOC("Empty"), LOC("Nothing in this category."));
            ui::End();
            return;
        }

        ui::Search(s_invAddFilter, sizeof(s_invAddFilter), LOC("Narrow this category down by name."));

        const bool locked = !game::Inventory::EditsPersist();

        int shown = 0;
        for (int i = 0; i < total; ++i)
        {
            game::Inventory::ItemInfo it{};
            if (!game::Inventory::GetCatalogItem(s_invAddCat, i, &it)) continue;
            if (s_invAddFilter[0] && !SearchMatches(it.name, s_invAddFilter) && !SearchMatches(it.key, s_invAddFilter)) continue;
            ++shown;
        }

        RenderAddAll(s_invAddCat, s_invAddFilter, shown, locked);

        for (int i = 0; i < total; ++i)
            RenderAddRow(s_invAddCat, i, s_invAddFilter, /*showCat=*/false, locked);
        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("Nothing in this category is called that."));

        ui::End();
    }

    // --- Money & Currency Generator -------------------------------------------
    static int s_directSilverInput = 20000000;
    static int s_pouchCountInput = 1000;

    static void RenderInventoryMoneyOptional()
    {
        ui::Begin(LOC("Optional"));

        if (ui::Option(LOC(">> Clear Bugged/Fake Wallet Coins <<"),
                       LOC("Wipes all fake/bugged silver coins from your bag so you can fix the 0-Wallet desync.")))
        {
            game::Inventory::SetDirectSilver(0);
            ui::Toast(LOC("Fake coins cleared! Now sell 1 item to the merchant, then try Set Money."));
        }

        if (ui::Option(LOC("Consolidate All Money Stacks"),
                       LOC("Merges all scattered silver slots into 1 single master slot in your bag.")))
        {
            int cleaned = game::Inventory::ConsolidateMoney();
            ui::Toast(LOC("Consolidated money into 1 stack (cleaned %d duplicate slots)."), cleaned);
        }

        ui::End();
    }

    static void RenderInventoryMoney()
    {
        ui::Begin(LOC("Money & Currency"));
        ReportPendingAdd();

        // 1. DIRECT SILVER WALLET
        ui::IntOption(LOC("Direct Silver Amount"), &s_directSilverInput, 0, 20000000, 1000000, 20000000,
                      LOC("Specify the amount of Silver coins to add or set directly into your wallet (max 20,000,000). Set to 0 to clear bugged coins."));

        if (ui::Option(LOC(">> Set Wallet to Exact Amount <<"),
                       LOC("Sets your Silver balance to the exact value specified above.")))
        {
            if (game::Inventory::SetDirectSilver(s_directSilverInput))
                ui::Toast(LOC("Wallet set to %d Silver!"), s_directSilverInput);
            else
            {
                ui::Toast(LOC("ENGINE BLOCK: Wallet is empty or too low (0 Silver)."));
                ui::Toast(LOC("Please sell 1 junk item to any merchant to get at least 2 Silver first, then try again!"));
            }
        }


        if (ui::Option(LOC(">> Add Amount to Existing Wallet <<"),
                       LOC("Adds the specified silver amount on top of your current wallet balance.")))
        {
            if (game::Inventory::AddDirectSilver(s_directSilverInput))
                ui::Toast(LOC("Added +%d Silver to wallet!"), s_directSilverInput);
            else
                ui::Toast(LOC("Silver added to bag!"));
        }

        // Quick Presets
        if (ui::Option(LOC("Set to 1,000,000 Silver (1 Million)"), LOC("Instantly sets wallet to 1,000,000 Silver.")))
        {
            game::Inventory::SetDirectSilver(1000000);
            ui::Toast(LOC("Wallet set to 1,000,000 Silver!"));
        }

        if (ui::Option(LOC("Set to 10,000,000 Silver (10 Million)"), LOC("Instantly sets wallet to 10,000,000 Silver.")))
        {
            game::Inventory::SetDirectSilver(10000000);
            ui::Toast(LOC("Wallet set to 10,000,000 Silver!"));
        }

        if (ui::Option(LOC("Set to 20,000,000 Silver (20 Million)"), LOC("Instantly sets wallet to 20,000,000 Silver (Safe Cap).")))
        {
            game::Inventory::SetDirectSilver(20000000);
            ui::Toast(LOC("Wallet set to 20,000,000 Silver!"));
        }

        // 2. FULL SILVER POUCHES (INVENTORY ITEMS)
        ui::IntOption(LOC("Full Silver Pouches (Count)"), &s_pouchCountInput, 10, 10000, 100, 1000,
                      LOC("Quantity of Full Silver Pouches to inject into your bag (10,000 Silver each)."));

        if (ui::Option(LOC(">> Spawn Full Silver Pouches <<"),
                       LOC("Spawns the specified count of Full Silver Pouches into your bag.")))
        {
            if (game::Inventory::SpawnSilverPouches(s_pouchCountInput))
                ui::Toast(LOC("Spawned %d Full Silver Pouches!"), s_pouchCountInput);
            else
                ui::Toast(LOC("Failed to spawn pouches."));
        }

        if (ui::Option(LOC(">> Cash In All Pouches (Instant Liquidate) <<"),
                       LOC("Converts all Full Silver Pouches in your bag into spendable Silver coins and frees pouch slots.")))
        {
            int64_t added = 0;
            if (game::Inventory::CashInAllSilverPouches(&added))
                ui::Toast(LOC("Liquidated pouches for +%lld Silver!"), added);
            else
                ui::Toast(LOC("No Full Silver Pouches found in bag."));
        }

        // 4. MAINTENANCE
        ui::Submenu(LOC("Optional"), "invmoney_opt", LOC("Tools for bugged wallets and stack consolidation."));

        ui::End();
    }

    // --- Abyss Items & Artifacts Generator ------------------------------------
    static int s_targetSealedTotal = 150;
    static int s_customAbyssArtifactCount = 100;

    static void RenderInventoryAbyss()
    {
        ui::Begin(LOC("Abyss Items & Artifacts"));
        ReportPendingAdd();
        ReportBulkAdd();

        // 1. Smart Sealed Abyss Artifact Collection (Unique ID Scanner & Injector)
        const auto status = game::Inventory::GetSealedArtifactStatus();
        const int missing = status.totalMax - status.totalUniqueOwned;

        char statusLabel[128];
        snprintf(statusLabel, sizeof(statusLabel), "%s: %d / %d (%d Missing)",
                 LOC("Sealed Artifacts Collected"), status.totalUniqueOwned, status.totalMax, missing);
        ui::Option(statusLabel, LOC("Scans your inventory for unique Sealed Abyss Artifact IDs (0001 to 0150)."));

        ui::IntOption(LOC("Target Total Sealed Artifacts"), &s_targetSealedTotal, 1, 150, 1, 10,
                      LOC("Slider to set target collection amount (1 to 150). Adds only missing numbers."));

        char addMissingDesc[256];
        snprintf(addMissingDesc, sizeof(addMissingDesc),
                 "%s (target: %d). %s",
                 LOC("Injects missing Sealed Abyss Artifacts into bag"),
                 s_targetSealedTotal,
                 LOC("Skips numbers you already have so you get only unique ones!"));

        if (ui::Option(LOC(">> Add Missing Sealed Artifacts to Target <<"), addMissingDesc))
        {
            const int added = game::Inventory::AddMissingSealedArtifacts(s_targetSealedTotal);
            if (added > 0)
                ui::Toast(LOC("Added %d missing Sealed Artifacts"), added);
            else
                ui::Toast(LOC("Already have all artifacts up to target %d"), s_targetSealedTotal);
        }

        if (ui::Option(LOC(">> Add ALL Missing Sealed Artifacts (1 to 150) <<"),
                       LOC("Adds all missing unique Sealed Artifact numbers to reach 150/150 complete collection.")))
        {
            const int added = game::Inventory::AddMissingSealedArtifacts(150);
            if (added > 0)
                ui::Toast(LOC("Added %d missing Sealed Artifacts"), added);
            else
                ui::Toast(LOC("Collection already complete (150/150)!"));
        }

        if (ui::Option(LOC("Clean Duplicate Sealed Artifacts"),
                       LOC("Removes duplicate copies of the same sealed artifact number, leaving exactly 1 of each.")))
        {
            const int removed = game::Inventory::CleanDuplicateSealedArtifacts();
            if (removed > 0)
                ui::Toast(LOC("Cleaned %d duplicate Sealed Artifacts"), removed);
            else
                ui::Toast(LOC("No duplicate Sealed Artifacts found"));
        }

        // 2. Abyss Artifact (Used for Skills enhancement & Gear Refinement)
        ui::IntOption(LOC("Abyss Artifacts (Count)"), &s_customAbyssArtifactCount, 1, 99999, 10, 100,
                      LOC("Quantity of Abyss Artifacts (ready material for skill upgrade & blacksmith)."));

        if (ui::Option(LOC(">> Spawn Abyss Artifacts (Usable Material) <<"),
                       LOC("Press Enter / Space / Click to inject Abyss Artifacts into your bag.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_Artifact", s_customAbyssArtifactCount))
                ui::Toast(LOC("Added %d Abyss Artifacts"), s_customAbyssArtifactCount);
            else
                ui::Toast(LOC("Failed to add Abyss Artifacts"));
        }

        // 3. Quick 1-Click Presets
        if (ui::Option(LOC("Add 100x Abyss Artifacts"), LOC("Adds 100x Abyss Artifacts for gear refinement & skills.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_Artifact", 100))
                ui::Toast(LOC("Added 100x Abyss Artifacts"));
        }

        if (ui::Option(LOC("Add 500x Abyss Artifacts"), LOC("Adds 500x Abyss Artifacts for gear refinement & skills.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_Artifact", 500))
                ui::Toast(LOC("Added 500x Abyss Artifacts"));
        }

        if (ui::Option(LOC("Add 50x Blessing of the Immortal (Skill Points)"), LOC("Spawns 50x Blessing of the Immortal to boost skills.")))
        {
            if (game::Inventory::AddItemByKey("Boss_Reward_SuperSkill", 50))
                ui::Toast(LOC("Added 50x Blessing of the Immortal"));
        }

        if (ui::Option(LOC("Add 50x Advanced Skill Manuals"), LOC("Spawns 50x Advanced Skill Manuals (SkillPoint_Book_02).")))
        {
            if (game::Inventory::AddItemByKey("SkillPoint_Book_02", 50))
                ui::Toast(LOC("Added 50x Advanced Skill Manuals"));
        }

        if (ui::Option(LOC("Add 50x Abyssal Seeds"), LOC("Spawns 50x Abyssal Seeds (AbyssStone_Seed).")))
        {
            if (game::Inventory::AddItemByKey("AbyssStone_Seed", 50))
                ui::Toast(LOC("Added 50x Abyssal Seeds"));
        }

        if (ui::Option(LOC("Add 50x Abyss Cells"), LOC("Spawns 50x Abyss Cells (AbyssRuinsCreatureCore).")))
        {
            if (game::Inventory::AddItemByKey("AbyssRuinsCreatureCore", 50))
                ui::Toast(LOC("Added 50x Abyss Cells"));
        }

        if (ui::Option(LOC("Add 10x Vitality of the Abyss (+30 HP)"), LOC("Spawns permanent Abyss HP stat items.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_InfiniteStat_Hp30", 10))
                ui::Toast(LOC("Added 10x Vitality of the Abyss"));
        }

        if (ui::Option(LOC("Add 10x Spirit of the Abyss (+2 MP)"), LOC("Spawns permanent Abyss MP stat items.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_InfiniteStat_Mp2", 10))
                ui::Toast(LOC("Added 10x Spirit of the Abyss"));
        }

        if (ui::Option(LOC("Add 10x Breath of the Abyss (+3 SP)"), LOC("Spawns permanent Abyss SP stat items.")))
        {
            if (game::Inventory::AddItemByKey("Abyss_InfiniteStat_Sp3", 10))
                ui::Toast(LOC("Added 10x Breath of the Abyss"));
        }

        ui::End();
    }

    // --- Restore Items Hub & Categories ---------------------------------------
    static char s_restoreSearch[48] = "";

    static const char* const kRestoreBountyKeys[] = {
        "Quest_WantedPaper_0001", "Quest_WantedPaper_0002", "Quest_WantedPaper_0003", "Quest_WantedPaper_0004",
        "Quest_WantedPaper_0005", "Quest_WantedPaper_0006", "Quest_WantedPaper_0007", "Quest_WantedPaper_0008",
        "Quest_WantedPaper_0009", "Quest_WantedPaper_0011", "Quest_WantedPaper_0012", "Quest_WantedPaper_0013",
        "Quest_WantedPaper_0014", "Quest_WantedPaper_0015", "Quest_WantedPaper_0016", "Quest_WantedPaper_0017",
        "Quest_WantedPaper_0019", "Quest_WantedPaper_0020", "Quest_WantedPaper_0021", "Quest_WantedPaper_0022",
        "Quest_WantedPaper_0023", "Quest_WantedPaper_0024", "Quest_WantedPaper_0025", "Quest_WantedPaper_0026",
        "Quest_WantedPaper_0027", "Quest_WantedPaper_0028", "Quest_WantedPaper_0029", "Quest_WantedPaper_0031",
        "Quest_WantedPaper_0032", "Quest_WantedPaper_0033", "Quest_WantedPaper_0034", "Quest_WantedPaper_0035",
        "Quest_WantedPaper_0036", "Quest_WantedPaper_0037", "Quest_WantedPaper_0038", "Quest_WantedPaper_0039",
        "Quest_WantedPaper_0040", "Quest_WantedPaper_0041", "Quest_WantedPaper_0042", "Quest_WantedPaper_0043",
        "Quest_WantedPaper_0044", "Quest_WantedPaper_0045", "Quest_WantedPaper_0047", "Quest_WantedPaper_0048",
        "Quest_WantedPaper_0049", "Quest_WantedPaper_0050", "Quest_WantedPaper_0052", "Quest_WantedPaper_0059",
        "Quest_WantedPaper_0101", "Quest_WantedPaper_0102", "Quest_WantedPaper_0103", "Quest_WantedPaper_0104",
        "Quest_WantedPaper_0105", "Quest_WantedPaper_0106", "Quest_WantedPaper_0107", "Quest_WantedPaper_0108",
    };
    static constexpr int kRestoreBountyCount = static_cast<int>(sizeof(kRestoreBountyKeys) / sizeof(kRestoreBountyKeys[0]));

    static const char* const kRestoreLoreKeys[] = {
        "BoneCollectorWagon_Book", "Visione_Chip_BoneCollectorWagon",
        "Alustin_Journal_Book_I", "Alustin_Journal_Book_II", "Alustin_Journal_Book_III",
        "Alustin_Journal_Book_IV", "Alustin_Journal_Book_V", "Alustin_Journal_Book_VI",
        "Anamorphic_Book", "BloodyFarmhouse_Book", "CabinDeathMystery_Book", "Criminal_Urga_Book",
        "EngineerWorkLog_Book", "Friendly_Legendary_Animal_Book", "GiantBallista_Book",
        "IceStrandedShip_Book", "JijeongTempleShipLog_Book", "PororinChildrenForest_Book",
        "Quest_Book_Mansion_I", "Blank_Book",
    };
    static constexpr int kRestoreLoreCount = static_cast<int>(sizeof(kRestoreLoreKeys) / sizeof(kRestoreLoreKeys[0]));

    static const char* const kRestoreRecipeKeys[] = {
        "SkillPoint_Book_01", "SkillPoint_Book_02",
        "CraftingRecipe_Kuku_Pot_Book", "CraftingRecipe_Kuku_Pot_Arms_Book",
        "CraftingRecipe_Kuku_Pot_Backpack_Book", "CraftingRecipe_Kuku_Pot_Machine_Book", "CraftingRecipe_Kuku_Pot_Spear_Book",
        "Recipe_Book_Accessory_I", "Recipe_Book_Accessory_II", "Recipe_Book_Accessory_III", "Recipe_Book_Accessory_IV",
        "Recipe_Book_FabricArmor_I", "Recipe_Book_FabricArmor_II", "Recipe_Book_FabricArmor_III", "Recipe_Book_FabricArmor_IV",
        "Recipe_Book_LeatherArmor_I", "Recipe_Book_LeatherArmor_II", "Recipe_Book_LeatherArmor_III", "Recipe_Book_LeatherArmor_IV",
        "Recipe_Book_PlateArmor_I", "Recipe_Book_PlateArmor_II", "Recipe_Book_PlateArmor_III", "Recipe_Book_PlateArmor_IV",
        "Recipe_Book_Weapon_OneHandSword_I", "Recipe_Book_Weapon_Shield_I", "Recipe_Book_Weapon_TwoHandSword_I",
        "Recipe_Book_Equip_Felling_Axe_I", "Recipe_Book_Equip_Felling_Axe_II",
        "Recipe_Book_Equip_Pickaxe_I", "Recipe_Book_Equip_Pickaxe_II",
        "Recipe_Book_FishingRod_II", "Recipe_Book_Beekeeping", "Recipe_Book_Ent",
        "Item_AbyssGear_Recipe_Box_I", "Item_AbyssGear_Recipe_Box_II", "Item_AbyssGear_Recipe_Box_III",
        "Item_AbyssGear_Recipe_Box_IV", "Item_AbyssGear_Recipe_Box_V", "Item_AbyssGear_Recipe_Box_VI",
    };
    static constexpr int kRestoreRecipeCount = static_cast<int>(sizeof(kRestoreRecipeKeys) / sizeof(kRestoreRecipeKeys[0]));

    static const char* const kRestoreKeyKeys[] = {
        "Alchemist_Key", "Alchemist_Key_Proto", "Del_Prison_Room_Key", "FortMoru_Key",
        "GantryCrane_Big_Key", "Goblin_Director_House_Key", "GraceMansion_MetalDoor_Key",
        "GraceMansion_RoomDoor_Key", "Hernand_Winery_Key", "Home_Key",
        "Installation_01_key", "Installation_02_key", "Installation_03_key",
        "MarniTower_Key", "Master_Key", "Metal_Gate_Key", "Neut_Delpheon_EMP_key",
        "Prison_Key", "Quest_GoldenStar_key", "Quest_HernandCastle_Underground_Key",
        "Quest_Hostages_House_Key", "Quest_TrolluniversityTower_Key",
        "Tower_Key", "WellsDungeon_Key", "WestDemenissChurch_Key",
    };
    static constexpr int kRestoreKeyCount = static_cast<int>(sizeof(kRestoreKeyKeys) / sizeof(kRestoreKeyKeys[0]));

    static const char* const kRestoreCollectKeys[] = {
        "Item_gimmick_collectionstorage_0001", "Item_gimmick_basecamp_warehouse_box_0002",
        "Item_gimmick_collection_prop_Ceramic_0021", "Item_gimmick_collection_prop_Ceramic_0023",
        "Item_gimmick_collection_prop_Ceramic_0033", "Item_gimmick_collection_prop_Ceramic_0034",
        "Item_gimmick_collection_prop_Ceramic_0036", "Item_gimmick_collection_prop_Ceramic_0040",
        "Item_gimmick_collection_prop_Ceramic_0042", "Item_gimmick_collection_prop_Ceramic_0043",
        "Item_gimmick_collection_prop_Ceramic_0044", "Item_gimmick_collection_prop_Ceramic_0048",
        "Item_gimmick_collection_prop_Ceramic_0049",
        "Collection_Prop_Chest_0001", "Collection_Prop_Chest_0002", "Collection_Prop_Chest_0003",
        "Collection_Prop_Chest_0004", "Collection_Prop_Chest_0005", "Collection_Prop_Chest_0006",
        "Collection_Prop_Chest_0007", "Collection_Prop_Chest_0008", "Collection_Prop_Chest_0009",
        "Collection_Prop_Chest_0010",
    };
    static constexpr int kRestoreCollectCount = static_cast<int>(sizeof(kRestoreCollectKeys) / sizeof(kRestoreCollectKeys[0]));

    static const char* const kRestoreGearKeys[] = {
        "Bayur_Fabric_Armor", "Bayur_Fabric_Gloves", "Bayur_Fabric_Boots",
        "BountyHunter_Fabric_Armor_I", "BountyHunter_Fabric_Armor_II", "BountyHunter_Fabric_Armor_III",
        "BountyHunter_Fabric_Armor_IV", "BountyHunter_Fabric_Armor_V", "BountyHunter_Fabric_Armor_VI",
        "BountyHunter_Fabric_Gloves_I", "BountyHunter_Fabric_Boots_I", "BountyHunter_Fabric_Cloak_I",
        "BountyHunter_Fabric_Helmet_I", "Quest_Crowman_Key_I",
        "Item_AbyssGear_Special_Box", "Item_AbyssGear_SKill_Lv1_Box", "Item_AbyssGear_SKill_Lv2_Box",
        "Item_AbyssGear_SKill_Lv3_Box", "Item_AbyssGear_Stat_Lv1_Box", "Item_AbyssGear_Stat_Lv2_Box",
        "Item_AbyssGear_Stat_Lv3_Box",
    };
    static constexpr int kRestoreGearCount = static_cast<int>(sizeof(kRestoreGearKeys) / sizeof(kRestoreGearKeys[0]));

    static const char* const kRestoreMountKeys[] = {
        "Item_gimmick_birdfeeder_0001",
        "Item_gimmick_attach_marni_machinetank_backpack_01", "Item_gimmick_attach_marni_machinetank_backpack_02",
        "Item_gimmick_attach_marni_machinetank_backpack_03", "Item_gimmick_attach_marni_machinetank_backpack_04",
        "Item_gimmick_attach_marni_machinetank_backpack_05", "Item_gimmick_attach_marni_machinetank_backpack_06",
        "Item_gimmick_attach_marni_machinetank_backpack_07",
        "Item_gimmick_attach_nuclear_fusion_core_01", "Item_gimmick_attach_mechanical_powerplant_0001",
    };
    static constexpr int kRestoreMountCount = static_cast<int>(sizeof(kRestoreMountKeys) / sizeof(kRestoreMountKeys[0]));

    static const char* const kRestoreMedalKeys[] = {
        "Item_Beighen_Enchant_Coin", "Boss_Reward_BigMoney", "Boss_Reward_SuperSkill", "Boss_Reward_BountyReduction",
        "Item_gimmick_attach_abyss_core_03", "Item_gimmick_attach_abyss_core_04",
        "Item_gimmick_attach_abyss_core_05", "Item_gimmick_attach_abyss_core_06",
        "Abyss_InfiniteStat_Hp30", "Abyss_InfiniteStat_Mp2", "Abyss_InfiniteStat_Sp3",
        "AbyssStone_Seed", "AbyssRuinsCreatureCore",
    };
    static constexpr int kRestoreMedalCount = static_cast<int>(sizeof(kRestoreMedalKeys) / sizeof(kRestoreMedalKeys[0]));

    static void RenderRestoreCategoryPage(const char* title, const char* const* keys, int count)
    {
        ui::Begin(title);
        ReportPendingAdd();
        ReportBulkAdd();

        int ownedCount = 0;
        for (int i = 0; i < count; ++i)
        {
            if (game::Inventory::IsItemKeyOwned(keys[i]))
                ++ownedCount;
        }
        const int missingCount = count - ownedCount;

        char statusLabel[128];
        snprintf(statusLabel, sizeof(statusLabel), "%s: %d / %d (%d %s)",
                 LOC("Collection Status"), ownedCount, count, missingCount, LOC("Missing"));
        ui::Option(statusLabel, LOC("Current ownership status across your inventory, bags, and storages."));

        char bulkDesc[256];
        snprintf(bulkDesc, sizeof(bulkDesc), "%s (%d %s).",
                 LOC("Automatically adds all missing items in this category to your inventory"),
                 missingCount, LOC("items"));

        if (ui::Option(LOC(">> Restore All Missing in Category <<"), bulkDesc))
        {
            if (missingCount <= 0)
            {
                ui::Toast(LOC("All items in this category are already owned!"));
            }
            else
            {
                const int restored = game::Inventory::RestoreCategoryMissing(keys, count);
                if (restored > 0)
                    ui::Toast(LOC("Restoring %d missing items..."), restored);
                else
                    ui::Toast(LOC("Failed to restore items or bulk add busy"));
            }
        }

        ui::Search(s_restoreSearch, sizeof(s_restoreSearch),
                   LOC("Search items in this category by name or internal key."));

        int shown = 0;
        for (int i = 0; i < count; ++i)
        {
            const char* key = keys[i];
            if (!key || !key[0]) continue;

            const char* resolved = game::ResolveItemDisplayName(key);
            char displayName[96]{};
            if (resolved && resolved[0])
            {
                snprintf(displayName, sizeof(displayName), "%s", resolved);
            }
            else
            {
                uint16_t tid = game::Inventory::FindTypeIdByKey(key);
                if (!tid || !game::Inventory::NameForTypeId(tid, displayName, sizeof(displayName)))
                    snprintf(displayName, sizeof(displayName), "%s", key);
            }

            if (s_restoreSearch[0] &&
                !SearchMatches(displayName, s_restoreSearch) &&
                !SearchMatches(key, s_restoreSearch))
                continue;

            const bool owned = game::Inventory::IsItemKeyOwned(key);
            char rowLabel[160];
            snprintf(rowLabel, sizeof(rowLabel), "[%s]  %s",
                     owned ? LOC("IN BAG") : LOC("CATALOG"), displayName);

            char desc[224];
            snprintf(desc, sizeof(desc), "%s: %s | %s: %s. %s",
                     LOC("Key"), key,
                     LOC("Status"), owned ? LOC("In Inventory") : LOC("Not in Bag"),
                     LOC("Press to add 1 copy to inventory."));

            if (ui::Option(rowLabel, desc))
            {
                if (game::Inventory::AddItemByKey(key, 1))
                    ui::Toast(LOC("Added 1x %s"), displayName);
                else
                    ui::Toast(LOC("Could not add %s"), displayName);
            }
            ++shown;
        }

        if (shown == 0)
            ui::Option(LOC("No matches"), LOC("Nothing in this category matches your search."));

        ui::End();
    }

    static void RenderRestoreBounty()
    {
        RenderRestoreCategoryPage(LOC("Bounty Notices (Wanted Posters)"), kRestoreBountyKeys, kRestoreBountyCount);
    }

    static void RenderRestoreLore()
    {
        RenderRestoreCategoryPage(LOC("Documents & Lore Books"), kRestoreLoreKeys, kRestoreLoreCount);
    }

    static void RenderRestoreRecipes()
    {
        RenderRestoreCategoryPage(LOC("Recipes & Crafting Manuals"), kRestoreRecipeKeys, kRestoreRecipeCount);
    }

    static void RenderRestoreKeys()
    {
        RenderRestoreCategoryPage(LOC("Quest Keys, Passes & Memories"), kRestoreKeyKeys, kRestoreKeyCount);
    }

    static void RenderRestoreCollectibles()
    {
        RenderRestoreCategoryPage(LOC("Collectibles & Collector's Chest"), kRestoreCollectKeys, kRestoreCollectCount);
    }

    static void RenderRestoreGear()
    {
        RenderRestoreCategoryPage(LOC("Unique Quest Weapons & Boss Gear"), kRestoreGearKeys, kRestoreGearCount);
    }

    static void RenderRestoreMount()
    {
        RenderRestoreCategoryPage(LOC("Mount, Mecha & Vehicle Gear"), kRestoreMountKeys, kRestoreMountCount);
    }

    static void RenderRestoreMedals()
    {
        RenderRestoreCategoryPage(LOC("Rare Medals, Tokens & Artifacts"), kRestoreMedalKeys, kRestoreMedalCount);
    }

    static void RenderRestoreCatalogArchive()
    {
        ui::Begin(LOC("Quest & Special Item Catalog Archive"));
        ReportPendingAdd();
        ReportBulkAdd();

        ui::Submenu(LOC("1. Bounty Notices (Wanted Posters)"), "invrestore_bounty",
                   LOC("Browse all 50+ Bounty Notices and Wanted Posters (Simon, Ulzok, Haldin, etc.)."));
        ui::Submenu(LOC("2. Documents & Lore Books"), "invrestore_lore",
                   LOC("Browse Skull Collector's Journal, archive records, voyager logs & historical notes."));
        ui::Submenu(LOC("3. Recipes & Crafting Manuals"), "invrestore_recipes",
                   LOC("Browse cooking recipe books, armor/weapon blueprints, and skill manuals."));
        ui::Submenu(LOC("4. Quest Keys, Passes & Memories"), "invrestore_keys",
                   LOC("Browse quest dungeon keys, gate keys, vault keys, and quest memories."));
        ui::Submenu(LOC("5. Collectibles & Collector's Chest"), "invrestore_collect",
                   LOC("Browse Collectibles Chest, private storage items, and ceramic relics."));
        ui::Submenu(LOC("6. Unique Quest Weapons & Boss Gear"), "invrestore_gear",
                   LOC("Browse one-time quest reward armor sets, faceless cloth gear, and boss drops."));
        ui::Submenu(LOC("7. Mount, Mecha & Vehicle Gear"), "invrestore_mount",
                   LOC("Browse Sotdae of Bond, Marni's Mecha power packs, cores, and specialized mount gear."));
        ui::Submenu(LOC("8. Rare Medals, Tokens & Artifacts"), "invrestore_medals",
                   LOC("Browse Beighen refinement tokens, shadow contracts, super skill blessings & power cores."));

        ui::End();
    }

    static void RenderInventoryRestore()
    {
        ui::Begin(LOC("Restore Lost & Sold Items (Buyback)"));
        ReportPendingAdd();
        ReportBulkAdd();

        const int count = game::Inventory::GetLostItemsCount();

        if (count == 0)
        {
            ui::Option(LOC("No Lost / Sold Items Recorded"),
                       LOC("Items you sell to merchants, discard, or delete will automatically appear here for instant recovery."));
        }
        else
        {
            char sumLabel[128];
            snprintf(sumLabel, sizeof(sumLabel), "%s: %d %s",
                     LOC("Lost & Sold Items"), count, LOC("recorded in history"));
            ui::Option(sumLabel, LOC("Items you sold, discarded, or deleted. Select any item to buyback and restore."));

            if (ui::Option(LOC(">> Restore All Lost & Sold Items <<"),
                           LOC("Restores every single lost, sold, or deleted item listed below back into your inventory.")))
            {
                const int restored = game::Inventory::RestoreAllLostItems();
                ui::Toast(LOC("Restored %d lost items!"), restored);
            }

            if (ui::Option(LOC("Clear Lost & Sold History"), LOC("Clears the history log without adding items.")))
            {
                game::Inventory::ClearLostItems();
                ui::Toast(LOC("Lost items history cleared"));
            }

            static char s_lostSearch[48] = "";
            ui::Search(s_lostSearch, sizeof(s_lostSearch), LOC("Filter lost items by name or key..."));

            int shown = 0;
            for (int i = 0; i < count; ++i)
            {
                game::Inventory::LostItemRecord rec{};
                if (!game::Inventory::GetLostItem(i, &rec)) continue;

                if (s_lostSearch[0] &&
                    !SearchMatches(rec.name, s_lostSearch) &&
                    !SearchMatches(rec.key, s_lostSearch) &&
                    !SearchMatches(rec.source, s_lostSearch))
                    continue;

                char rowLabel[160];
                snprintf(rowLabel, sizeof(rowLabel), "[%s]  %lldx  %s",
                         LOC("RESTORE"), static_cast<long long>(rec.qty), rec.name);

                char desc[224];
                snprintf(desc, sizeof(desc), "%s: %s | %s: %s | %s: %s. %s",
                         LOC("Source"), rec.source,
                         LOC("Time"), rec.timeStr[0] ? rec.timeStr : "--:--",
                         LOC("Key"), rec.key[0] ? rec.key : "none",
                         LOC("Press to restore into inventory."));

                if (ui::Option(rowLabel, desc))
                {
                    if (game::Inventory::RestoreLostItem(i))
                        ui::Toast(LOC("Restored %lldx %s"), static_cast<long long>(rec.qty), rec.name);
                    else
                        ui::Toast(LOC("Failed to restore item"));
                    break;
                }
                ++shown;
            }

            if (shown == 0 && s_lostSearch[0])
                ui::Option(LOC("No matches"), LOC("No lost items match your search filter."));
        }

        ui::Submenu(LOC("Quest & Special Item Catalog Archive"), "invrestore_catalog",
                   LOC("Browse the full database of quest items, wanted notices, recipes, and rare relics."));

        ui::End();
    }

    // Human-readable name for a Win32 virtual-key code (rebind row display).
    // The named cases cover keys GetKeyNameText renders poorly (or needs the
    // extended-key bit for); the fallback handles letters/digits/F-keys.
    static const char* KeyName(int vk)
    {
        switch (vk)
        {
        case 0:           return "None";
        case VK_INSERT:   return "Insert";
        case VK_DELETE:   return "Delete";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "Page Up";
        case VK_NEXT:     return "Page Down";
        case VK_UP:       return "Up Arrow";
        case VK_DOWN:     return "Down Arrow";
        case VK_LEFT:     return "Left Arrow";
        case VK_RIGHT:    return "Right Arrow";
        case VK_NUMLOCK:  return "Num Lock";
        case VK_SNAPSHOT: return "Print Screen";
        case VK_DIVIDE:   return "Num /";
        default: break;
        }
        static char buf[32];
        const UINT sc = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
        if (sc && GetKeyNameTextA(static_cast<LONG>(sc << 16), buf, sizeof(buf)) > 0)
            return buf;
        snprintf(buf, sizeof(buf), "Key 0x%02X", vk);
        return buf;
    }

    // Human-readable name for an XInput button mask (plus the trigger
    // sentinel bits, State::kPadLTrigger/kPadRTrigger); a combo joins with
    // " + ".
    static const char* PadMaskName(unsigned int mask)
    {
        static const struct { unsigned int bit; const char* name; } kBtns[] = {
            { XINPUT_GAMEPAD_LEFT_SHOULDER,  "LB" },
            { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB" },
            { XINPUT_GAMEPAD_DPAD_UP,        "D-Pad Up" },
            { XINPUT_GAMEPAD_DPAD_DOWN,      "D-Pad Down" },
            { XINPUT_GAMEPAD_DPAD_LEFT,      "D-Pad Left" },
            { XINPUT_GAMEPAD_DPAD_RIGHT,     "D-Pad Right" },
            { XINPUT_GAMEPAD_A,              "A" },
            { XINPUT_GAMEPAD_B,              "B" },
            { XINPUT_GAMEPAD_X,              "X" },
            { XINPUT_GAMEPAD_Y,              "Y" },
            { XINPUT_GAMEPAD_START,          "Start" },
            { XINPUT_GAMEPAD_BACK,           "Back" },
            { XINPUT_GAMEPAD_LEFT_THUMB,     "LS" },
            { XINPUT_GAMEPAD_RIGHT_THUMB,    "RS" },
            { kPadLTrigger,                  "LT" },
            { kPadRTrigger,                  "RT" },
        };
        static char buf[96];
        buf[0] = 0;
        for (const auto& b : kBtns)
            if (mask & b.bit)
            {
                if (buf[0]) strncat(buf, " + ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, b.name, sizeof(buf) - strlen(buf) - 1);
            }
        return buf[0] ? buf : "None";
    }

    // Lowest virtual-key currently held (mouse buttons 0x01-0x06 skipped),
    // or 0 if none. Used to detect the key the user wants to bind.
    static int FirstKeyDown()
    {
        for (int vk = 0x08; vk <= 0xFE; ++vk)
            if (GetAsyncKeyState(vk) & 0x8000)
                return vk;
        return 0;
    }

    // Which bind is currently listening for a key/button press, or None. Only
    // one column of one row can capture at a time - starting a new one abandons
    // any other. The Key/Pad pair per action maps onto the two columns of a
    // single ui::BindRow.
    enum class BindTarget
    {
        None, MenuKey, MenuPad, MarkerKey, MarkerPad, FlyUpKey, FlyUpPad, FlyDownKey, FlyDownPad
    };
    static BindTarget s_capTarget = BindTarget::None;

    // One action drawn as a two-column keyboard | controller rebind row (see
    // ui::BindRow). Formats each column's current bind (or a "press..." prompt
    // for the column that owns the live capture), then turns the row's result
    // into a capture request or a per-column reset. `keyTarget` / `padTarget`
    // name this action's two capture slots; `def*` are the defaults a reset
    // restores (so a reset can never strand the menu with no way to reopen it).
    static void KeybindActionRow(const char* label, const char* desc, int* cursor,
                                 int* keyVk, unsigned int* padMask,
                                 int defKeyVk, unsigned int defPadMask,
                                 BindTarget keyTarget, BindTarget padTarget)
    {
        const bool capKey = (s_capTarget == keyTarget);
        const bool capPad = (s_capTarget == padTarget);

        char keyBuf[48], padBuf[64];
        snprintf(keyBuf, sizeof(keyBuf), "%s", capKey ? "press a key..." : KeyName(*keyVk));
        snprintf(padBuf, sizeof(padBuf), "%s", capPad ? "press a button..." : PadMaskName(*padMask));

        // While listening, the description spells out how to finish; otherwise
        // it's the action's own explanation.
        const char* rowDesc = desc;
        if      (capKey) rowDesc = "Press the key you want to bind, or Esc to cancel.";
        else if (capPad) rowDesc = "Press the button or combo you want to bind, or Esc to cancel.";

        const int capCol = capKey ? 0 : capPad ? 1 : -1;
        switch (ui::BindRow(label, cursor, keyBuf, padBuf, capCol, rowDesc))
        {
        case ui::BindEdit::RebindKey:
            s_capTarget = capKey ? BindTarget::None : keyTarget; // toggle listening
            break;
        case ui::BindEdit::RebindPad:
            s_capTarget = capPad ? BindTarget::None : padTarget;
            break;
        case ui::BindEdit::ResetKey:
            *keyVk = defKeyVk;
            if (capKey) s_capTarget = BindTarget::None;
            Settings::Save(); // binds persist regardless of Auto Save
            ui::Toast("%s keyboard bind reset to %s", label, KeyName(defKeyVk));
            break;
        case ui::BindEdit::ResetPad:
            *padMask = defPadMask;
            if (capPad) s_capTarget = BindTarget::None;
            Settings::Save();
            ui::Toast("%s controller bind reset to %s", label, PadMaskName(defPadMask));
            break;
        default:
            break;
        }
    }

    // Drives the "press a key / button to bind" capture for whichever row is
    // currently listening (s_capTarget). Kept out of RenderKeybinds' body so
    // its rows read cleanly. Returns true if a capture is (still) active, so
    // the caller keeps State::rebindCapture in sync.
    static bool DriveRebindCapture(State& st)
    {
        // Per-capture phase state.
        static bool         keyArmed = false, padArmed = false;
        static int          pendKey  = 0;
        static unsigned int padAccum = 0;
        static ULONGLONG    startMs  = 0;

        // A fresh capture request (including switching targets mid-listen)
        // resets the phase machine and starts the clock.
        static BindTarget wasTarget = BindTarget::None;
        const bool        capturing = s_capTarget != BindTarget::None;
        if (capturing && wasTarget != s_capTarget)
        {
            keyArmed = padArmed = false;
            pendKey = 0; padAccum = 0;
            startMs = GetTickCount64();
        }
        wasTarget = s_capTarget;
        if (!capturing)
            return false;

        // Resolve which field the active target writes and what the toast calls
        // it. Only the Free Flight pad binds accept the analog-trigger
        // sentinels - Menu Button is polled elsewhere (PollToggleCombo) purely
        // off the real wButtons mask, so a trigger could never fire it.
        int*          keyField       = nullptr;
        unsigned int* padField       = nullptr;
        bool          padTriggersOk  = false;
        const char*   label          = "";
        switch (s_capTarget)
        {
        case BindTarget::MenuKey:    keyField = &st.openKeyVk;           label = "Menu key";               break;
        case BindTarget::MenuPad:    padField = &st.openPadMask;         label = "Menu button";            break;
        case BindTarget::MarkerKey:  keyField = &st.markerTeleportKeyVk; label = "Marker Teleport key";   break;
        case BindTarget::MarkerPad:  padField = &st.markerTeleportPadMask; label = "Marker Teleport button"; padTriggersOk = true; break;
        case BindTarget::FlyUpKey:   keyField = &st.flyUpKeyVk;          label = "Fly Up key";             break;
        case BindTarget::FlyUpPad:   padField = &st.flyUpPadMask;        label = "Fly Up button";          padTriggersOk = true; break;
        case BindTarget::FlyDownKey: keyField = &st.flyDownKeyVk;        label = "Fly Down key";           break;
        case BindTarget::FlyDownPad: padField = &st.flyDownPadMask;      label = "Fly Down button";        padTriggersOk = true; break;
        default: break;
        }

        const bool timedOut = GetTickCount64() - startMs > 6000; // never soft-lock
        const bool escDown  = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

        if (keyField)
        {
            if (timedOut)                     s_capTarget = BindTarget::None;   // give up, keep old bind
            else if (!keyArmed)               keyArmed = (FirstKeyDown() == 0); // let the activating press lift
            else if (pendKey == 0)
            {
                if (escDown) s_capTarget = BindTarget::None;                    // cancel
                else { const int vk = FirstKeyDown(); if (vk && vk != VK_ESCAPE) pendKey = vk; }
            }
            else if (!(GetAsyncKeyState(pendKey) & 0x8000))                     // commit once the key lifts
            {
                *keyField = pendKey;
                Settings::Save();                                               // binds persist regardless of Auto Save
                ui::Toast("%s set to %s", label, KeyName(pendKey));
                s_capTarget = BindTarget::None;
            }
        }
        else if (padField)
        {
            const unsigned int btns = padTriggersOk ? ui::PadButtonsWithTriggers()
                                                      : static_cast<unsigned int>(ui::PadButtons());
            if (timedOut || escDown)          s_capTarget = BindTarget::None;
            else if (!padArmed)               padArmed = (btns == 0);  // let the activating A lift
            else if (btns != 0)               padAccum |= btns;        // accumulate the held combo
            else if (padAccum != 0)                                    // commit on release
            {
                *padField = padAccum;
                Settings::Save();
                ui::Toast("%s set to %s", label, PadMaskName(padAccum));
                s_capTarget = BindTarget::None;
            }
        }

        return s_capTarget != BindTarget::None;
    }

    static void RenderKeybinds()
    {
        State&      st  = State::Get();
        const State def;                 // source of every reset-to-default value
        ui::Begin();

        // Each action is one row with its keyboard bind and controller bind side
        // by side under the two column titles. Left/Right pick the column, Enter
        // rebinds it (then press the key/button - Esc cancels), Del resets it.
        // Every bind here persists in Trinity.ini regardless of Auto Save.
        ui::BindHeader();

        // Per-row focus column (0 = keyboard, 1 = controller), remembered across
        // frames so the highlight stays where the user left it on each row.
        static int s_curMenu = 0, s_curMarker = 0, s_curUp = 0, s_curDown = 0;

        KeybindActionRow(LOC("Open Menu"), LOC("Opens and closes this menu."),
                         &s_curMenu, &st.openKeyVk, &st.openPadMask,
                         def.openKeyVk, def.openPadMask,
                         BindTarget::MenuKey, BindTarget::MenuPad);
        KeybindActionRow(LOC("Marker Teleport"), LOC("Teleport directly to the map marker / custom waypoint placed on the map."),
                         &s_curMarker, &st.markerTeleportKeyVk, &st.markerTeleportPadMask,
                         def.markerTeleportKeyVk, def.markerTeleportPadMask,
                         BindTarget::MarkerKey, BindTarget::MarkerPad);
        KeybindActionRow(LOC("Fly Up"), LOC("While Free Flight is on and you're airborne, hold this to rise."),
                         &s_curUp, &st.flyUpKeyVk, &st.flyUpPadMask,
                         def.flyUpKeyVk, def.flyUpPadMask,
                         BindTarget::FlyUpKey, BindTarget::FlyUpPad);
        KeybindActionRow(LOC("Fly Down"), LOC("While Free Flight is on and you're airborne, hold this to sink."),
                         &s_curDown, &st.flyDownKeyVk, &st.flyDownPadMask,
                         def.flyDownKeyVk, def.flyDownPadMask,
                         BindTarget::FlyDownKey, BindTarget::FlyDownPad);

        st.rebindCapture = DriveRebindCapture(st);

        if (ui::Option(LOC("Reset All Keybinds"),
                       LOC("Resets every keyboard and controller bind on this page to its default.")))
        {
            Settings::ResetBinds();
            Settings::Save(); // binds persist regardless of Auto Save
            s_capTarget = BindTarget::None;
            ui::Toast(LOC("Keybinds reset to defaults"));
        }

        ui::End();
    }

    static void RenderSystem()
    {
        State& st = State::Get();
        ui::Begin();

        // `save` = write Trinity.ini this frame. The Auto Save flag flip is
        // always written (so turning it OFF is remembered); everything else
        // only writes while Auto Save is on.
        bool save = false;

        ui::Submenu(LOC("Keybinds"), "keybinds",
                   LOC("Set the keyboard and controller binds for opening the menu and Free Flight."));

        const char* const localizedThemeNames[] = {
            LOC("Crimson Red"), LOC("Cyber Cyan"), LOC("Neon Purple"),
            LOC("Matrix Emerald"), LOC("Royal Gold"), LOC("Sunset Orange")
        };
        if (ui::Combo(LOC("Theme Color"), &st.themeIndex, localizedThemeNames, 6,
                      LOC("Customize the mod menu accent and header colors.")))
        {
            save = true;
        }

        // Dynamic Language Selector (detects Trinity_*.ini files)
        const int langCount = loc::GetLanguageCount();
        if (langCount > 1)
        {
            std::vector<const char*> langNames(langCount);
            for (int i = 0; i < langCount; ++i)
                langNames[i] = loc::GetLanguageName(i);

            int curLang = loc::GetCurrentLanguageIndex();
            if (ui::Combo(LOC("Language"), &curLang, langNames.data(), langCount,
                          LOC("Select mod menu display language.")))
            {
                loc::SetLanguage(curLang);
                st.languageIndex = curLang;
                snprintf(st.languageCode, sizeof(st.languageCode), "%s", loc::GetLanguageCode(curLang));
                save = true;
            }
        }

        save |= ui::Toggle(LOC("Show FPS Counter"), &st.showFps, LOC("Shows your FPS in the corner of the screen.")) && st.autoSave;

        // Detected Game Version Information
        const char* verStr = core::GetGameVersionDisplay();
        ui::Option(verStr, LOC("Game version automatically detected by Trinity engine."));

        if (ui::Toggle(LOC("Auto Save Features"), &st.autoSave,
                       LOC("Saves your settings automatically and restores them next time.")))
            save = true;
        if (ui::Option(LOC("Reset All to Default"),
                       LOC("Resets every feature to its default.")))
        {
            Settings::ResetFeatures();
            save |= st.autoSave;
            ui::Toast(LOC("All features reset to defaults"));
        }

        if (save)
            Settings::Save();

        ui::End();
    }

    void Render()
    {
        State&   st = State::Get();
        ImGuiIO& io = ImGui::GetIO();

        const char* const localizedTabs[] = {
            LOC("PLAYER"), LOC("INVENTORY"), LOC("TRAVEL"), LOC("WORLD"), LOC("SYSTEM")
        };
        ui::SetTabs(localizedTabs, TabCount);

        // The menu is keyboard/d-pad driven and leaves the mouse to the game so
        // the player can still look around, so we never draw an ImGui cursor.
        io.MouseDrawCursor = false;

        // Toasts outlive the menu (e.g. "Warping to..." after closing it).
        ui::DrawToasts();

        // A queued dye apply finishes on the game thread; report it wherever
        // the user is (the "Applying dye..." toast keeps this path drawing
        // even if they closed the menu right after).
        ReportPendingDye();

        if (st.showFps)
            DrawFpsCounter();

        if (!st.menuOpen)
            return;

        ui::BeginFrame();

        const char* cur = ui::CurrentMenu();

        // A rebind capture only lives on the SYSTEM tab's Keybinds submenu. If
        // we somehow left it (a mouse tab-click, or backing out while
        // listening), abandon the capture so menu navigation never stays
        // frozen elsewhere.
        if (st.rebindCapture && (ui::CurrentTab() != TabSystem || strcmp(cur, "keybinds") != 0))
            st.rebindCapture = false;

        if (!*cur)
        {
            switch (ui::CurrentTab())
            {
            case TabPlayer:    RenderPlayer(); break;
            case TabInventory: RenderInventoryHome(); break;
            case TabTravel:    RenderTravel(); break;
            case TabWorld:     RenderWorld();  break;
            case TabSystem:    RenderSystem(); break;
            default:           RenderPlayer(); break;
            }
        }
        else if (!strcmp(cur, "keybinds")) RenderKeybinds();
        else if (!strcmp(cur, "saved_locs")) RenderSavedLocations();
        else if (!strcmp(cur, "loc_manage")) RenderSavedLocationManage();
        else if (!strcmp(cur, "ftcats"))   RenderFastTravelCats();
        else if (!strcmp(cur, "ftnodes"))  RenderFastTravelNodes();
        else if (!strcmp(cur, "dyeslots"))  RenderDyeSlots();
        else if (!strcmp(cur, "dyeedit"))   RenderDyeEdit();
        else if (!strcmp(cur, "dyecustom")) RenderDyeCustom();
        else if (!strcmp(cur, "equipslots")) RenderEquipSlots();
        else if (!strcmp(cur, "equipedit"))  RenderEquipEdit();
        else if (!strcmp(cur, "equipgear"))  RenderEquipGear();
        else if (!strcmp(cur, "equipswap"))  RenderEquipSwap();
        else if (!strcmp(cur, "invedit"))  RenderInventoryEditor();
        else if (!strcmp(cur, "invstore")) RenderInventoryStorage();
        else if (!strcmp(cur, "invcat"))   RenderInventoryCat();
        else if (!strcmp(cur, "invadd"))    RenderInventoryAdd();
        else if (!strcmp(cur, "invaddcat")) RenderInventoryAddCat();
        else if (!strcmp(cur, "invrestore")) RenderInventoryRestore();
        else if (!strcmp(cur, "invrestore_catalog")) RenderRestoreCatalogArchive();
        else if (!strcmp(cur, "invrestore_bounty")) RenderRestoreBounty();
        else if (!strcmp(cur, "invrestore_lore")) RenderRestoreLore();
        else if (!strcmp(cur, "invrestore_recipes")) RenderRestoreRecipes();
        else if (!strcmp(cur, "invrestore_keys")) RenderRestoreKeys();
        else if (!strcmp(cur, "invrestore_collect")) RenderRestoreCollectibles();
        else if (!strcmp(cur, "invrestore_gear")) RenderRestoreGear();
        else if (!strcmp(cur, "invrestore_mount")) RenderRestoreMount();
        else if (!strcmp(cur, "invrestore_medals")) RenderRestoreMedals();
        else if (!strcmp(cur, "invmoney"))  RenderInventoryMoney();
        else if (!strcmp(cur, "invmoney_opt")) RenderInventoryMoneyOptional();
        else if (!strcmp(cur, "invabyss"))  RenderInventoryAbyss();
        else if (!strcmp(cur, "combat_options")) RenderCombatOptions();
        else if (!strcmp(cur, "mount_options")) RenderMountOptions();
        else if (!strcmp(cur, "world_time_presets")) RenderTimePresets();
        else if (!strcmp(cur, "world_weather")) RenderWeatherAtmosphere();
        else                              RenderPlayer();
    }
}
