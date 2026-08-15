# Trinity — Crimson Desert (vTweak by Lian)

Trinity is an in-game DirectX 12 mod menu for **Crimson Desert**, originally created by **XeTrinityz**. This repository provides an enhanced, fully updated build for game version **TU 1.17.00 – 1.18.00+** with critical memory fixes, auto-navigation, and quality-of-life enhancements.

> **Single-player use only.** Do not use this project in online or anti-cheat-protected modes. This community project is not affiliated with or endorsed by Pearl Abyss.

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

- **Destination Teleport with Live Coordinates**:
  - Re-anchored destination teleport with active coordinate display and robust physics thread synchronization.

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

1. Install a compatible **ASI Loader** for Crimson Desert.
2. Copy `Trinity.asi` into the game root directory (where `CrimsonDesert.exe` is located) or into your loader's `plugins/` folder.
3. Launch the game and load your save.
4. Press **Insert** (Keyboard) or **LB + D-pad Down** (Controller) to open the Trinity menu.

---

## Controls

| Action | Keyboard / Mouse | Controller |
| :--- | :--- | :--- |
| **Open / Close Menu** | `Insert` (or `Esc` to close) | `LB` + `D-pad Down` |
| **Navigate** | `Arrow Keys` / Mouse Click | `D-pad` |
| **Select / Toggle** | `Enter` or Left Click | `A` |
| **Back / Parent Menu** | `Backspace` | `B` |
| **Adjust Value / Amount** | `Left` / `Right` | `D-pad Left` / `Right` |
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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
The compiled mod will be located at `build/Release/Trinity.asi`.

---

## Credits & Acknowledgments

- **XeTrinityz** — Original creator of [Trinity](https://github.com/XeTrinityz/Trinity).
- **ReXooGen (Lian)** — 1.17+ compatibility fork, inventory stride & storage fixes, and auto marker navigation.

---
*Crimson Desert is a trademark of Pearl Abyss. This project is open-source and intended solely for single-player modding and educational purposes.*
