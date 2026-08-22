# Trinity — Internal Mod Menu for Crimson Desert

An internal mod menu for **Crimson Desert** (DirectX 12). Builds to
`Trinity.asi` and is injected by an ASI loader. Renders a custom GTA-style list
menu (no stock ImGui widgets) over the game, navigable with keyboard, mouse and
controller.

> **Single-player only.** Do not use in online or anti-cheat-protected modes.
> This is a hobby reverse-engineering project, not affiliated with or endorsed
> by Pearl Abyss.

## Features

| Tab | What's in it |
| --- | --- |
| **Player** | God Mode, Infinite Stamina, Infinite Spirit, Super Run, Super Jump, Free Flight, Trust Multiplier, incoming/outgoing damage multipliers, equipment dyeing (full RGB, presets from the game's own palette tables) and an Abyss Gear socket editor |
| **Travel** | Fast travel to any node in the game, grouped and named from the engine's own level tables |
| **Inventory** | Browse and edit what you're carrying, edit stack quantities, and add any item in the game — categories, names and icons all read live from the game's data tables |
| **World** | Game speed, freeze / advance time of day |
| **System** | Rebindable keys and pad buttons, FPS counter, persisted settings |

Item and category icons are decoded straight out of the game's `.paz` pak
archives at runtime, so the menu shows the game's own art rather than
placeholders.

## Architecture

```
Trinity.asi (DLL)
├── dllmain.cpp            DllMain → spawns init thread off the loader lock
├── core/mod.*             Coordinator: MinHook init + hook install/teardown
├── core/settings.*        Persisted settings + keybinds
├── core/logger.h          Colored console log (opened only by the process
│                          that renders; the launcher stays silent)
├── core/state.h           Shared toggles (menu open, feature flags)
├── core/version.h         TRINITY_VERSION
├── mem/scanner.*          AOB signature scanner
├── mem/hooks.h            MinHook helpers
├── mem/safe_memory.h      Guarded reads/writes and pointer-chain walks
├── game/offsets.h         The ONLY place game knowledge lives: byte
│                          signatures + struct offsets, re-scanned at load
├── game/player.*          Player resolution + stat/vital features
├── game/teleport.*        Fast-travel node enumeration and warping
├── game/inventory.*       Inventory read/edit, item catalog, add-item
├── game/equipment.*       Equip component walk, Abyss Gear sockets
├── game/dye.*             Dye records on item values
├── game/friendly.*        NPC/animal trust
├── game/world.*           Game speed, time of day
├── game/pak.*             .paz/.pamt pak reader (LZ4) for tables and icons
├── hooks/dx12_hook.*      Dummy-device vtable grab; Present / ResizeBuffers /
│                          ExecuteCommandLists detours; ImGui DX12 renderer
├── hooks/input.*          WndProc subclass; keyboard/mouse input blocking
├── hooks/xinput_hook.*    XInputGetState detour; blocks the pad from the game
│                          while the menu is open (the menu reads the real pad)
├── gui/framework.*        Custom menu framework: header banner, list rows,
│                          toggles/sliders/submenus, kb+mouse+XInput nav
├── gui/widgets.*          Row / search / list widgets built on the framework
├── gui/icons.*            Pak-sourced item icon textures
└── gui/menu.*             The actual menu content (the tabs above)
```

Everything the mod knows about the game binary is a **byte signature** or a
**struct offset** in [`src/game/offsets.h`](src/game/offsets.h), never an
absolute address, so a game patch that shifts code around does not silently
break it: signatures are re-scanned at load and any failure is logged.

## Controls

| Action        | Keyboard / Mouse            | Controller |
| ------------- | --------------------------- | ---------- |
| Open / close  | INSERT (ESC closes)         | LB + D-pad Down |
| Navigate      | Arrows, mouse wheel / hover | D-pad      |
| Select        | Enter or click              | A          |
| Back          | Backspace                   | B          |
| Adjust values | Left / Right arrows         | D-pad left/right |

All bindings are rebindable under **System → Keybinds**.

The open/close toggle is polled from the render loop rather than from the window
procedure, so it fires reliably regardless of focus or how the game pumps
messages. Keyboard/mouse input is swallowed while the menu is open — except key
and button *releases*, which always reach the game so a key held when the menu
opened can't get stuck. Controller input is blocked from the game via an
`XInputGetState` detour that neutralises the pad while the menu is up.

**How the render hook works:** we spin up a throwaway D3D12 device + swapchain to
read the addresses of `IDXGISwapChain::Present` (vtable[8]), `ResizeBuffers`
(vtable[13]) and `ID3D12CommandQueue::ExecuteCommandLists` (vtable[10]), then
detour them with MinHook. The `ExecuteCommandLists` hook captures the game's real
DIRECT command queue (needed to submit our overlay). On the first `Present` after
that, we build ImGui's DX12 resources and render the overlay every frame.

## Build

Prerequisites:

- Visual Studio 2022 (Desktop C++ workload) + Windows 10/11 SDK
- CMake ≥ 3.20
- Internet on first configure (CMake fetches Dear ImGui and MinHook)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build/Release/Trinity.asi`.

## Install / run

1. Get an ASI loader (e.g. Ultimate ASI Loader). Ship its proxy DLL next to the
   game executable — pick an import the game actually loads (commonly
   `dinput8.dll`, `winmm.dll`, or `version.dll`).
2. Drop `Trinity.asi` in the game folder (or the loader's `scripts/` /
   `plugins/` folder, depending on the loader).
3. Launch the game. A **Trinity** console window opens alongside it.
4. Press **INSERT** (or **LB + D-pad Down**) in-game to toggle the menu.

## Scripts

`scripts/gen_dye_data.py` regenerates [`src/game/dye_data.h`](src/game/dye_data.h)
from the game's own dye tables, read straight out of the pak archives. Re-run it
after a game patch if the dyehouse palette looks off or dyeable gear is being
filtered out:

```powershell
python scripts/gen_dye_data.py "<game install folder>"
```

## Dependencies

Fetched automatically by CMake:

- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause)

## License

[MIT](LICENSE).
