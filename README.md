# Trinity Mod Menu v1.3.5.4 for Crimson Desert

## Mod Information

**Mod Name:** Trinity Mod Menu v1.3.5.4 (vUpdate by mul0)  
**Original Creator:** XeTrinityz  
**vTweak Maintenance:** Lian (ReXooGen)  
**TU 2.03.01 Compatibility Update:** mul0  
**Game Version:** 2.03.01 (PE 1.0.0.2949)  
**Target Process:** `CrimsonDesert.exe`  
**Architecture:** 64-bit  
**Platform:** Steam / Windows  
**Steam App ID:** 3321460  
**Last Updated:** 22/09/2026  

## Links

**Download:** https://mul0.com/trainer/crimson-desert-trinity-mod-menu  
**Blog:** https://mul0.com/  
**Steam:** https://store.steampowered.com/app/3321460/Crimson_Desert/  
**Original Nexus Page:** https://www.nexusmods.com/crimsondesert/mods/3109  
**vTweak Source:** https://github.com/ReXooGen/Trinity  
**Video:** https://youtu.be/tJxeqWQpOKY

## Description

Trinity is an in-game DirectX 12 mod menu for the Windows version of **Crimson Desert**. Version **1.3.5.4** updates the vTweak build for **TU 2.03.01** (PE 1.0.0.2949) and provides player, combat, worker, travel, inventory, equipment, world, weather, interface, and quality-of-life controls from a controller-friendly overlay.

The menu supports keyboard, mouse, and XInput controllers. English is built in, while nine optional language files add German, Spanish, French, Indonesian, Japanese, Korean, Brazilian Portuguese, Russian, and Simplified Chinese.

## What's New in v1.3.5.4

- **Max Worker Level & Skills:** Brand-new feature under the PLAYER tab! Implements a reversible memory patch for the game's worker grade-selector branch, forcing top tier (tier 5) and instantly unlocking maximum worker level and all abilities. Gated and verified for PE 2850, PE 2944, and PE 2949, with full configuration persistence in `Trinity.ini` (`workerMaxLevelAndSkills=1`).
- **Full Game Version 2.03.01 (PE 1.0.0.2949) & PE 2944 Compatibility:** Updated function signatures, offsets, and memory hooks to ensure complete stability on the latest retail patch.
- **Bounded Crash Diagnostics System:** Integrated deterministic diagnostics engine with a 512-entry fixed ring buffer tracking feature toggles, hooks, patches, and memory mutation scopes (`NoteMemoryWrite`, `MutationScope`). Generates bounded diagnostic bundles (50–300 MB) for immediate root-cause triage without corrupting process state.
- **Locomotion & Free Flight Offset Update:** Adjusted movement component owner pointer offset to `0x2C0` (`LocoStepperContract::Pe2944`), restoring full Free Flight and locomotion control on PE 2944 / PE 2949.
- **Safe 700-Slot Inventory Expansion:** Native four-argument expansion setter and reversible pickup patch tailored for PE 2949, preventing container state corruption.
- **Direct Map-Marker Teleport:** Verified map waypoint detection and Sky Arrival altitude handling on modern PE revisions.
- **TLS Realm Selector Alignment:** Confirmed TLS realm offset at `0x1EC` across PE 2850, 2944, and 2949, preventing client/server realm mismatches when adding items.

## Main Features

### Player, Combat and Workers

- **Max Worker Level & Skills:** Unlocks maximum level and all abilities for workers and mercenaries.
- **One-Hit Kill:** Eliminates enemies and bosses with a single strike.
- **God Mode:** Continuously restores player health.
- **Infinite Item Durability:** Locks equipped gear at 100% durability.
- **No Fall Damage:** Prevents damage from cliffs and sky teleports.
- **Infinite Stamina & Mount:** Unlimited stamina for both player and mount.
- **Infinite Spirit:** Unlimited Spirit ability gauge.
- **No Bounty:** Clears and prevents guard alerts and bounty during the session.
- **Damage Multipliers:** Independent outgoing and incoming damage tuning.
- **Super Run & Super Jump:** Adjustable movement speed and jump height multipliers.
- **Free Flight:** 3D aerial navigation with custom rise and sink keys.
- **Trust Multiplier:** Faster NPC trust and animal taming progression.

### Equipment

- Repair all equipped gear to full durability.
- Refine all equipped gear to +10.
- Unlock all Abyss sockets.
- Edit individual refinement levels and Abyss socket contents.
- Replace equipped weapons, shields, armor, and accessories with character and slot filters.

### Travel

- Display and copy current player coordinates.
- Teleport to the active world-map destination or custom waypoint.
- Adjustable Sky Arrival Altitude when the marker does not provide elevation.
- Unlimited saved-location bookmarks with teleport, rename, coordinate update, and delete controls.
- Categorized Fast Travel database for regions and points of interest.

### Inventory and Currency

- Browse inventory by storage and category.
- Search, change quantity, remove items, refresh inventory, or set quantities in bulk.
- Search the complete item catalog and add individual items or filtered categories in bulk.
- Safe Slot Size control up to 700 slots using the native expansion setter.
- Custom Max Stack Size with duplicate-stack consolidation and protection for non-stackable equipment and quest items.
- Set an exact Silver balance or add Silver to the current wallet.
- Quick Silver presets up to the safe 20,000,000 cap.
- Spawn and cash in Full Silver Pouches.
- Clear bugged wallet coins and consolidate scattered money stacks.
- Manage Sealed Abyss Artifact collections from 1 to 150 and remove duplicates.
- Spawn Abyss Artifacts, skill items, Seeds, Cells, and permanent Abyss stat items.
- Restore individually recorded sold, discarded, or deleted items, or restore the complete history.
- Search and restore missing wanted posters, lore books, recipes, quest keys, collectibles, unique gear, mount gear, medals, tokens, and artifacts.

### World and Atmosphere

- Adjustable Game Speed.
- Freeze the current Time of Day.
- Advance or rewind the clock by a custom number of hours.
- Dawn, Midday, Sunset, Midnight, and exact-hour time controls.
- Dynamic, Clear, Overcast, Rainy, Thunderstorm, and Dense Fog weather presets.
- Clear distant fog, force a clear sky, or instantly clear active weather.
- Adjust rain, snow, dust, cloud, fog, wind, gust, and turbulence values.
- Disable environmental wind.

### System and Interface

- Rebind Open Menu, Marker Teleport, Fly Up, and Fly Down controls.
- Six menu themes: Crimson Red, Cyber Cyan, Neon Purple, Matrix Emerald, Royal Gold, and Sunset Orange.
- Adjustable menu scale and item-tooltip size.
- Xbox or PlayStation controller icons.
- Built-in and custom `.ttf` / `.otf` title fonts.
- FPS counter and optional console window.
- Automatic game-version detection.
- Auto Save Features and Reset All to Default.

## Default Controls

| Action | Keyboard / Mouse | Controller |
|---|---|---|
| Open / Close Menu | `Insert` | `LB + D-Pad Down` |
| Navigate | Arrow Keys / Mouse | D-Pad |
| Select / Toggle | `Enter`, `Space`, or Left Click | `A` |
| Back | `Backspace` or `Esc` | `B` |
| Change Tabs | `Q`, `E`, or `Tab` | `LB` / `RB` |
| Adjust Values | Left / Right; hold `Shift` for faster adjustment | D-Pad Left / Right |
| Teleport to Map Marker | `F10` | Unassigned by default |
| Free Flight: Rise | Hold `Caps Lock` | Hold `RB` |
| Free Flight: Descend | Hold `Ctrl` | Hold `RT` |

All action binds can be changed under **SYSTEM > Keybinds**.

## Installation

1. Close Crimson Desert before copying or replacing mod files.
2. Open the folder containing `CrimsonDesert.exe` (e.g. `Steam\steamapps\common\Crimson Desert\bin64`).
3. Copy `winmm.dll`, `Trinity.asi`, and the optional `Trinity_*.ini` language files into that folder.
4. Keep `Trinity.ini.example` as a reference or rename a configured copy to `Trinity.ini` if you want to prepare settings manually.
5. Start the game, load a save, and wait until gameplay is fully initialized.
6. Press `Insert` or `LB + D-Pad Down` to open Trinity.

If another compatible ASI Loader is already installed, place `Trinity.asi` in the game directory or in the loader's `plugins` folder and avoid installing a second conflicting proxy DLL.

## Required and Optional Package Files

- **Required for the included installation method:** `winmm.dll` and `Trinity.asi`.
- **Optional configuration reference:** `Trinity.ini.example`.
- **Optional translations:** `Trinity_de.ini`, `Trinity_es.ini`, `Trinity_fr.ini`, `Trinity_id.ini`, `Trinity_ja.ini`, `Trinity_ko.ini`, `Trinity_ptbr.ini`, `Trinity_ru.ini`, and `Trinity_zh.ini`.
- **Documentation only:** `README.md` and `LICENSE`.
- `SHA256SUMS.txt` verifies file integrity but is not loaded by the game.

## Important

- Use Trinity only in single-player gameplay. Do not use it in online or anti-cheat-protected modes.
- Back up important save data before using inventory, currency, equipment, or restoration actions that can persist.
- The No Bounty option is session-only. Other menu actions can intentionally change saved inventory or equipment data.
- Game updates can change executable code and may require another compatibility update.
- This community project is not affiliated with or endorsed by Pearl Abyss.

## Credits

- **XeTrinityz** - original Trinity creator and maintainer.
- **Orcax1399** - research insights credited by the original project.
- **Gugi96** - ASI and compatibility reference research.
- **slingblade2047** - earlier Crimson Desert compatibility work.
- **Lian (ReXooGen)** - vTweak features, localization, and maintenance.
- **mul0** - TU 2.03.01 compatibility update and publication package.

Website: https://mul0.com/