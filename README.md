# Trinity — Crimson Desert (vTweak by Lian)

Trinity is an in-game DirectX 12 mod menu for **Crimson Desert**, originally created by **XeTrinityz**. This repository provides an enhanced, fully updated build for game version **TU 1.17.00 – 2.00.01+** with critical memory fixes, auto-navigation, and quality-of-life enhancements.

> **Single-player use only.** Do not use this project in online or anti-cheat-protected modes. This community project is not affiliated with or endorsed by Pearl Abyss.

---

## Screenshots & In-Game Previews

| Player Menu | Travel & Destination Teleport |
| :---: | :---: |
| ![Player Menu](images/image.png) | ![Travel Menu](images/image2.png) |

| Inventory & Max Stack Size | World & Game Speed Control |
| :---: | :---: |
| ![Inventory Menu](images/image3.png) | ![World Menu](images/image4.png) |

| System Settings & Themes | Equipment Enhancer & Sockets |
| :---: | :---: |
| ![System Menu](images/image5.png) | ![Edit Equipment](images/image6.png) |

| Unlimited Custom Bookmarks | Storage Filters & Item Editor |
| :---: | :---: |
| ![Saved Locations](images/image7.png) | ![Storage Editor](images/image8.png) |

---

## What's New in v1.3.1

- **Critical CTD Fixes & Engine Hardening**:
  - **Fixed NPC Greeting / Interaction Crash (`Trinity.asi+0x19160`)**: Completely replaced inline code patches with SEH-guarded MinHook trampolines and robust pointer validation for quest and transient NPC records.
  - **Fixed Weapon Swapping Race Condition (`CrimsonDesert.exe+0x121F192`)**: Eliminated multi-threaded race conditions between the DirectX 12 render loop and the engine's internal weapon mesh destructor (`ReleaseRef`) by serializing character and equipment scans on the main game thread.
  - **Fixed Character Switching Freeze & Crash**: Resolved deadlocks and crashes occurring when switching between characters (e.g., Damiane ↔ Kliff) after equipment edits via direct actor pointers and SEH exception handlers.
- **Smart Equipped Gear Protection**:
  - Equipping weapons, armor, or accessories no longer falsely registers them as "Sold / Discarded" in the *Restore Lost & Sold Items* menu.
  - Automatically filters actively equipped gear on Kliff, Damiane, and Oongka from the buyback list.
- **Live Self-Healing Item Icons & Side-Panel Tooltips**:
  - Automatically queries the engine's live item definition tables (`IconForTypeId`) to heal corrupted or truncated icon strings from disk history, restoring full high-resolution artwork for all equipment and quest items.
  - Side-panel tooltip preview cards now render reliably for every selected item.
- **Proportional Trust Multiplier Scaling**:
  - Re-engineered trust scaling calculation to provide smooth, proportional progression (1.0x – 100.0x) instead of instant maxing.
- **Native Engine Game Speed Scaling (`hkFrameTimerUpdate`)**:
  - Replaced legacy fixed-timestep overrides with native engine frame timer scaling for smooth 0.1x – 10.0x speed manipulation without FPS drops or physics jitter.

---

## What's New in v1.3.0

- **Crimson Desert TU 2.00.00 – 2.00.01 Full Support**:
  - Updated memory offsets, structures, and function signatures matching the major Title Update 2.00 overhaul.
  - Re-anchored item reflection tables (`iteminfo`, `ItemGroupInfo`, `stringinfo`, `Inventory`, and `TrItemValue` constructor) for seamless Add Item spawning.
- **Expanded Multi-Language Support (9 Languages)**:
  - Added full native translations for: **English**, **German (Deutsch)**, **Spanish (Español)**, **French (Français)**, **Indonesian (Bahasa Indonesia)**, **Japanese (日本語)**, **Korean (한국어)**, **Portuguese - Brazil (Português - Brasil)**, **Russian (Русский)**, and **Simplified Chinese (简体中文)**.
- **Enhanced Character Resolution**:
  - 3-anchor verification for character manager resolution ensuring 100% reliable player detection across transitions.

---

## What's New in v1.2.4

- **Smart Lost & Sold Items Tracker (Buyback & Recycle Bin)**:
  - Real-time differential inventory change tracker that automatically logs every item sold to merchants, discarded, or deleted.
  - One-click restoration per item or bulk `>> Restore All Lost & Sold Items <<`.
  - Persistent disk storage (`Trinity_LostItems.txt`) so your buyback history persists across game sessions.
- **Quest & Special Item Catalog Archive**:
  - Dedicated searchable archive for 50+ Bounty Notices, Lore Documents, Recipes, Quest Keys, Relics, and Unique Gear.
- **Universal Backwards Compatibility (TU 1.10 – 1.18+)**: Multi-version adaptive memory layout and dynamic slot strides (`0xC0` for TU <= 1.15, `0xC8` for TU >= 1.16).
- **Runtime Binary Fingerprinting**: Live in-memory machine code scanner to accurately identify and display active Title Updates (e.g. `TU 1.18.02 (Active)`).
- **Cross-Slot Controller Free Flight**: Polling across controller slots 0 through 3 for robust multi-controller and PS5 pad support.
- **Dedicated Submenus**: Integrated **Money & Currency**, **Abyss Items & Artifacts**, and **Restore Items** submenus.
- **Engine Memory Safety Hardening**: Completely eliminated destructive memory writes and wrapped all subsystem refreshes in SEH for 100% crash-free stability.

---

## What's New in v1.2.3

- **Infinite Mount Stamina TU 1.18+ Fix**: Restored full infinite stamina support for horses, mounts, and dragons while galloping and sprinting.
- **Continuous Stamina & Spirit Auto-Refresh**: Refined stat commit interception so any consumed player or mount stamina and spirit instantly refreshes back to 100% full capacity in real-time.
- **Character Spontaneous Combustion Fix**: Completely purged thermal and elemental debuff meters (types 17, 18, 28, 48) from the scanner, permanently resolving the bug where characters caught fire upon spawn.
- **Native Weather & Environment Controls**: Built-in time of day and weather modifiers under the **WORLD** tab, providing seamless, crash-free environmental control without requiring conflicting external addons.
- **Protagonist Scanning Stability**: Hardened `TickResolveSelf` vital chain validation to eliminate access violation crashes in crowded NPC areas.

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
