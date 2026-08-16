# Trinity — Crimson Desert (vTweak by Lian)

Trinity is an in-game DirectX 12 mod menu for **Crimson Desert**, originally created by **XeTrinityz**. This repository provides an enhanced, fully updated build for game version **TU 1.17.00 – 1.18.00+** with critical memory fixes, auto-navigation, and quality-of-life enhancements.

> **Single-player use only.** Do not use this project in online or anti-cheat-protected modes. This community project is not affiliated with or endorsed by Pearl Abyss.

---

## Screenshots & In-Game Previews

| Player Menu | Travel & Destination Teleport |
| :---: | :---: |
| ![Player Menu](image.png) | ![Travel Menu](image2.png) |

| Inventory & Max Stack Size | World & Game Speed Control |
| :---: | :---: |
| ![Inventory Menu](image3.png) | ![World Menu](image4.png) |

| System Settings & Themes | Equipment Enhancer & Sockets |
| :---: | :---: |
| ![System Menu](image5.png) | ![Edit Equipment](image6.png) |

| Unlimited Custom Bookmarks | Storage Filters & Item Editor |
| :---: | :---: |
| ![Saved Locations](image7.png) | ![Storage Editor](image8.png) |

## What's New in v1.2.2

- **Game Compatibility**: Verified and optimized for **Crimson Desert TU 1.18.00 – 1.18.01+**.
- **Add Item & Equipment System Hotfix**:
  - **Instant Menu Unlock**: Fixed `"Adding is locked until your save finishes loading"` false positive by validating active client holder immediately upon world load.
  - **Equipable Spawned Weapons & Armor**: Fixed spawned equipment showing as un-equipable by assigning unique 64-bit instance IDs and full durability (`10000`) on spawn across both server and client authority realms.
  - **Bucket Capacity Auto-Expansion**: Dynamically expands category capacity up to 2,000+ slots before injection and runs `RepairUsedSlots` to prevent `insert planner refused, err=-771604600 (no slot / bucket full)`.
  - **Non-Stackable Equipment Protection**: Smart `Max Stack Size` filter that preserves weapons, shields, and armors as single-instance items (`qty = 1`) so they can always be equipped without stack collisions, while materials and consumables stack up to 999,999.

---

## What's New in v1.2.1

- **Money & Currency System Overhaul**:
  - Direct Silver modification with save-and-reload persistence.
  - Non-blocking background currency scanner (prevents UI freeze/hangs).
  - Quick Silver Pouch & Gold Chest Spawner (`Silver_Pack`, `Small_Silver_Pack`, `Boss_Reward_BigMoney`).
  - Added dedicated **Abyss Items & Artifacts** submenu with smart 1-150 Sealed Artifact collection injector, Seeds, Cells, and permanent stat items (+30 HP, +2 MP, +3 SP).

---

## What's New in v1.2.0

- **Add Item Engine Fix (TU 1.18.00+ / 18.0.01 Compatibility)**:
  - Fixed Add Item execution pipeline (`CommitAdd`) and memory signatures matching the latest game executable.
  - Resolves `"not ready"` error when spawning weapons, armor, materials, or consumables.

- **Multi-Language Support (Localization System)**:
  - Added in-game language selection with full translations: **English**, **Simplified Chinese (简体中文)**, and **Korean (한국어)**.
  - Automatically loads and saves language preference to `Trinity.ini`.

- **Updated Subsystem Signatures**:
  - Re-anchored NPC and pet friendliness modifier signatures (`kSig_FriendlySetNpc`, `kSig_FriendlySetPet`) and inventory holder resolvers for TU 1.18+.

---

## What's New in v1.1.0

- **Abyss Socket TU 1.17+ Alignment & Live Socketing**:
  - Aligned the Abyss socket data array pointer to `+0x60` and implemented accurate unlocked record state decoding.
  - Fixes the socket editor displaying all slots as empty and enables seamless Abyss Gear socketing.

- **Batch Equipment Enhancer (1-Click)**:
  - **Repair All Gear**: Instantly restores 100% durability across all equipped weapons and armor.
  - **Max Refinement (+10) All**: Upgrades all equipped items to maximum refinement level (+10).
  - **Unlock All Sockets**: Unlocks all 5 Abyss sockets on all equipped gear in one click.

- **Unlimited Dynamic Saved Locations (Bookmarks)**:
  - Bookmark unlimited custom player coordinates across the world map.
  - In-place keyboard/controller **Rename** feature for custom labels (e.g. "Base Camp", "Dungeon Entrance").
  - Instant **Teleport to Bookmark** and direct **Delete (`Del` / `X`)** shortcut key per location.
  - Fully persisted to `Trinity.ini`.

- **Dynamic Theme Customizer**:
  - 6 selectable menu color themes: **Crimson Red**, **Cyber Cyan**, **Neon Purple**, **Matrix Emerald**, **Royal Gold**, and **Sunset Orange**.

- **Destination Teleport with Live Coordinates & Safe Landing**:
  - Re-anchored destination teleport with active coordinate display and robust physics thread synchronization.
  - Automatic God Mode / invulnerability protection until player touches the ground.

---

## Features

- **Player**: God Mode, Infinite Stamina, Infinite Spirit, Super Jump, Super Run, Free Flight, Damage Multipliers, and Trust Multipliers.
- **Travel**: 
  - One-Click **Teleport to Map Marker**.
  - Fast Travel database grouped by region and POI type (fast-travel nodes, chests, ores, shops, dungeons).
- **Inventory**:
  - Live Inventory Editor with storage & category filters, full-text search, and Set All quantities.
  - Add Item catalog to spawn any weapon, armor, or consumable in the game.
  - Max Bag Space & Max Stack Size overrides.
- **Equipment & Customization**:
  - Live Dye Editor with RGB sliders and save persistence.
  - Abyss Gear socket editor and item refinement level modifiers.
- **World & System**:
  - Time of day and game speed scaling.
  - Full Controller (XInput) and Keyboard/Mouse navigation with custom keybinds.
  - Clean DirectX 12 Dear ImGui overlay with decoded `.paz` item icons.

---

## Installation

1. Install a compatible **ASI Loader** for Crimson Desert (e.g. `dinput8.dll` or `winmm.dll`).
2. Copy `Trinity.asi` into the game root directory (where `CrimsonDesert.exe` is located) or into your loader's `plugins/` folder.
3. Launch the game and load your save.
4. Press **Insert** (Keyboard) or **LB + D-pad Down** (Controller) to open the Trinity menu.

---

## Controls

| Action | Keyboard / Mouse | Controller |
| :--- | :--- | :--- |
| **Open / Close Menu** | `Insert` (or `Esc` to close) | `LB` + `D-pad Down` |
| **Navigate** | `Arrow Keys` / Mouse Click | `D-pad` |
| **Select / Toggle** | `Enter` / `Space` / Left Click | `A` |
| **Back / Parent Menu** | `Backspace` | `B` |
| **Adjust Value / Amount** | `Left` / `Right` (Hold `Shift` for boost) | `D-pad Left` / `Right` |
| **Direct Numeric Input** | Type `0`–`9` or click number text | N/A |
| **Tab Switching** | `Q` / `E` or `Tab` | `LB` / `RB` |

Keybindings can be customized under **SYSTEM > Keybinds**.

---

## Building from Source

### Requirements:
- Windows 10 / 11 (64-bit)
- Visual Studio 2022 Build Tools with **Desktop development with C++**
- Windows 10/11 SDK
- CMake 3.20 or newer

### Build Command:
```powershell
# Configure and build Release x64 binary
powershell -ExecutionPolicy Bypass -File .\Build_Trinity.ps1
```
The compiled mod will be located at `build/Release/Trinity.asi`.

---

## Credits & Acknowledgments

Trinity is fully open-source under the MIT license. We gratefully acknowledge all contributors whose research and code made this project possible:

- **XeTrinityz** — Original Trinity creator and maintainer ([https://github.com/XeTrinityz/Trinity](https://github.com/XeTrinityz/Trinity))
- **Orcax1399** — Research insights credited by the original project
- **Gugi96** — Working ASI / reference research that helped guide compatibility repairs
- **slingblade2047** — Crimson Desert 1.17/1.18 compatibility work ([https://github.com/slingblade2047/Trinity](https://github.com/slingblade2047/Trinity))
- **ReXooGen / Lian** — Additional vTweak features, localization, and maintenance ([https://github.com/ReXooGen/Trinity](https://github.com/ReXooGen/Trinity))

---
*Crimson Desert is a trademark of Pearl Abyss. This project is open-source under the MIT license and intended solely for single-player modding and educational purposes.*
