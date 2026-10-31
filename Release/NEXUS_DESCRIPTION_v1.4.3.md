Trinity — Internal DirectX 12 Mod Menu for Crimson Desert
Enhanced and updated for TU 2.04.02 (Charting the Unknown) & the TU 2.00.00 – 2.03.02 line, by Lian (v1.4.3 Release)

Overview
Trinity is a native, high-performance internal DirectX 12 mod menu and trainer for Crimson Desert.
The current release brings full **TU 2.04.02 (PE 1.0.0.3446)** support — including the *Charting the Unknown* update — built on freshly re-derived signatures, table layouts and engine structures, then validated live in game. It ships with SEH-guarded MinHook detours, zero-latency DX12 frame compositing that composites before Frame Generation, revision-gated offsets (TU 1.14 → 2.04 branches coexist in one binary), session-safe No Bounty patching, Easy Parry (Just Guard), Easy Evade (Just Evade), One-Hit Kill, continuous Infinite Stamina & Mount Stamina, Infinite Item Durability, No Fall Damage, a 9-language localized UI, live Self-Healing Item Icons, a Quest Item Restore Archive, multi-character Equipment customization & Live Dyeing (Kliff, Damiane, Oongka), unlimited custom bookmarks, and auto-clearing destination map teleportation.

Key Features

1. Combat & Player Modifiers
- **No Bounty (Session-Safe Wanted-State Patching)**: zeroes bounty prices across the live wanted table (35/35 rows on 2.04.02) and neutralizes crime evaluation. Commit crimes without fines or guard aggro — 100% session-only and safe for save files.
- **Easy Parry (Just Guard)**: natively triggers Perfect Parry and deflect counters whenever you guard against enemy strikes.
- **Easy Evade (Just Evade)**: natively triggers Perfect Dodge slow-motion counters whenever you dodge in combat.
- **One-Hit Kill**: instantly eliminate hostile enemies or bosses with a single strike (1,000x multiplier).
- **God Mode / Infinite Health**: complete invulnerability with boss/enemy immortality protection fix.
- **Infinite Item Durability**: equipped weapons, shields, and armor never degrade (locked at 100% max durability).
- **No Fall Damage**: completely negates fall damage from high drops, cliffs, and Sky Arrival teleports.
- **Continuous Infinite Stamina & Mount**: automatically refreshes stamina to full in real time while sprinting, climbing, jumping, and galloping on mounts.
- **Infinite Spirit**: keeps your HUD and gameplay spirit pool locked and instantly refreshed.
- **Damage Multipliers**: adjustable Outgoing (0–20x) / Incoming (0–10x) damage scaling.
- **Trust Multiplier (1.0x – 25.0x)**: proportional trust gain for gifting NPCs and feeding animals. Scales the underlying trust records on both realms (client + server) so gains persist; per-relationship write throttling keeps FPS smooth even in NPC-dense towns. The in-game popup shows the game's own base amount — your records rise multiplied behind it.
- **Movement Abilities**: Super Run, Super Jump, and smooth camera-relative Free Flight Mode.

2. Add Item & Multi-Storage Spawner
- **Full Item Catalog**: spawn any weapon, armor, crafting material, or consumable — 7,000+ named definitions resolved live from the game's own item table.
- **Native Create Recipe**: follows the engine's own ctor → unique-ID → plan → commit sequence with valid 64-bit instance IDs, so spawned gear can be equipped immediately.
- **Smart Equipped Gear Protection**: actively equipped weapons, armor, and accessories on Kliff, Damiane, and Oongka are never falsely registered as sold or lost.
- **Live Self-Healing Item Icons & Tooltips**: queries live engine item definitions to render full-resolution artwork and side-panel preview cards.
- **Dynamic Bucket Auto-Expansion**: automatically expands internal bucket capacity (up to 700 caps, verified in memory) so adds never fail on a full storage.
- **Non-Stackable Gear Protection**: keeps gear unstacked (qty = 1) to avoid equip collisions, while materials and consumables stack up to 999,999,999.
- ⚠️ **TU 2.04.02 known limitation**: the server-realm write for created items is not yet wired on this revision, so added items can be wiped by the next server reconcile (character switch / save-load). The native create path is identified and queued as the next work item. All other inventory features are unaffected.

3. Dedicated Abyss Items & Quest Restore Archive
- **Smart 1–150 Sealed Artifact Collector**: intelligently scans your bag and injects missing unique Sealed Artifact IDs (0001–0150) without creating duplicates.
- **Refinement & Upgrade Materials**: 1-click spawner for usable Abyss Artifacts (gear refinement & skill upgrades), Abyssal Seeds, and Abyss Cells.
- **Permanent Stat Boosters**: quick add for Vitality of the Abyss (+30 HP), Spirit (+2 MP), and Breath (+3 SP).
- **Quest & Special Item Restore Archive**: recover sold, discarded, or missing documents, wanted notices, collectibles, and unique quest items — with a live tracker that records what you lost and from where.

4. Equipment & Live Dye Editor
- **Repair All Gear (1-Click)**: instantly restores 100% durability on all currently equipped weapons and armor.
- **Max Refinement (+10) All (1-Click)**: upgrades all worn gear to maximum enhancement level.
- **Unlock All Abyss Sockets (1-Click)**: opens all Abyss sockets across all worn equipment.
- **Live Abyss Socket Editor**: real-time socketing and clearing of Abyss Gears with crash-safe validation (edits verify on both realm components before confirming).
- **Live Equipment & Mount Dye Editor**: real-time RGB sliders and color swatches for armor, weapons, and horse barding/saddles — applies through the game's own dyehouse path so color renders instantly and persists.
- **Multi-Character Support**: switch between Kliff, Damiane, and Oongka to view and edit their individual equipment and dyes.
- **Slot Size Control**: raises every storage's slot cap (up to 700, verified live in memory — table defaults, bucket caps and the UI denominator all follow).

5. World, Time & Engine Modifiers
- **Native Engine Game Speed Scaling**: smooth 0.1x–5.0x true engine time-scale manipulation via the FrameTimerUpdate path (offsets re-derived for 2.04.02) — no frame-pacing drops, no physics jitter.
- **Freeze Time of Day**: locks the in-game sun and world clock accumulator seamlessly.
- **Weather & Time Presets**: instantly set time of day (Dawn, Noon, Sunset, Midnight) and control weather transitions.

6. Multi-Language Support & Localization (9 Languages + English)
- Full UI translations: English, German (Deutsch), Spanish (Español), French (Français), Indonesian (Bahasa Indonesia), Japanese (日本語), Korean (한국어), Portuguese – Brazil (Português do Brasil), Russian (Русский), Simplified Chinese (简体中文).
- **Dynamic INI Detection**: automatically scans the `languages/` folder beside Trinity.asi and saves preferences to Trinity.ini.
- **Preserved In-Game Item Names**: UI elements are fully localized while game item definitions remain authentic for maximum stability.

7. Travel & Teleportation
- **Destination Marker Teleport**: instant teleportation to your active map marker with live coordinates, safe landing elevation, and automatic destination clearing upon arrival (5/5 native hooks live on 2.04.02).
- **Unlimited Saved Locations (Bookmarks)**: bookmark unlimited custom coordinates with in-place Rename and 1-key Delete (Del / X).
- **Fast Travel Database**: comprehensive fast-travel waypoints across Pywel.

8. UI, System & HUD Features
- **Show FPS Counter**: clean Crimson-HUD-style overlay (XX FPS / XX FPS FLY).
- **Reordered Menu Tabs**: Player — Inventory — Travel — World — System.
- **Custom Themes**: 6 dynamic menu themes (Crimson Red, Cyber Cyan, Neon Purple, Matrix Emerald, Royal Gold, Sunset Orange).
- **Full Controller Support**: fully navigable with Keyboard/Mouse and Xbox/PlayStation controllers.
- **Native DirectX 12 Rendering**: high-performance font rendering with built-in HDR compositing; the overlay composites **before** Frame Generation and survives swapchain recreation (frame-gen toggles, resolution changes) without crashing.

Default Controls
- Open / Close Menu: **Insert** (or LB + D-Pad Down on controller)
- Navigate Rows: Arrow Up / Down or D-Pad Up / Down
- Switch Tabs: Q / E or LB / RB
- Select / Toggle: Enter or A Button
- Back / Close Submenu: Backspace / Esc or B Button
- Adjust Values / Sliders: Left / Right (hold Shift for boost) or D-Pad Left / Right
- Delete Bookmark: Delete or X Button

Installation
1. Download **Trinity-v1.4.3-vTweak (2.04.02).zip** from the Files tab (for game version **TU 2.04.02**, current as of the *Charting the Unknown* update).
2. Extract all files directly into your Crimson Desert game directory (where CrimsonDesert.exe is located, typically `bin64/`):
   - `Trinity.asi`
   - `winmm.dll` (ASI Loader)
   - `languages/` (folder containing translation .ini files)
3. Launch the game and press **Insert** (or LB + D-Pad Down on controller) to open the menu.

Troubleshooting & Antivirus Exclusion Guide
1. **Antivirus / Windows Defender Exclusion (False Positive)** — because `winmm.dll` (Ultimate ASI Loader) and `Trinity.asi` hook into the game's DirectX 12 render pipeline at runtime, some antivirus programs may falsely flag or block them:
   - **Add Exclusion**: add your Crimson Desert `bin64` folder to your antivirus / Windows Security exclusions list.
   - **Restore Blocked Files**: if the game does not launch, open Windows Security → Virus & threat protection → Protection history, find `winmm.dll` or `Trinity.asi`, and select Restore / Allow on device.
2. **Custom Launcher Compatibility (DLL Renaming)** — if your setup already uses a custom launcher with `winmm.dll`:
   - Keep your existing `winmm.dll` intact in the `bin64` folder.
   - Take the `winmm.dll` from the Trinity package and rename it to `version.dll` (or `dinput8.dll`).
   - Place `version.dll` and `Trinity.asi` into your `bin64` folder and launch normally. Both your launcher and Trinity's ASI loader load together seamlessly.

Important Notes & Compatibility
- **Single-Player Only**: strictly intended for offline single-player gameplay. Do not use in online or anti-cheat-protected modes.
- **Game version**: this release targets **TU 2.04.02 (PE 1.0.0.3446)**. The binary is revision-gated — older title-update branches (TU 1.14 → 2.03) remain built in and load on their matching game versions, but 2.04.02 is what current testing covers.

Credits & Acknowledgments
Trinity is fully open-source under the MIT license. We gratefully acknowledge all contributors whose research and code made this project possible:
- **XeTrinityz** — Original Trinity creator and maintainer ([GitHub](https://github.com/XeTrinityz/Trinity))
- **Orcax1399** — Research insights credited by the original project
- **Gugi96** — Working ASI / reference research that guided early compatibility repairs
- **slingblade2047** — Crimson Desert 1.17/1.18 compatibility work ([GitHub](https://github.com/slingblade2047/Trinity))
- **lucasfelipe24** — TU 2.04.02 compatibility research (PR #2): re-derived signatures, the re-laid-out ItemDef/table structures, the native add-item recipe, the trust component poll, and pak manifest parsing for the current revision ([PR #2](https://github.com/ReXooGen/Trinity/pull/2))
- **patoangolano** — TU 2.03.02 fork research ([trinity-crimson-desert-mounts](https://github.com/patoangolano/trinity-crimson-desert-mounts)); the graphics-options (frame generation / resolution) crash fix in this release is ported from their overlay work
- **ReXooGen / Lian** — vTweak features, localization, maintenance, and the 3446 live-validation & repair pass ([GitHub](https://github.com/ReXooGen/Trinity))

Special thanks to XeTrinityz for creating the original Trinity foundation and to the entire Crimson Desert modding community!
