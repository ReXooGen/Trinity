#pragma once

namespace trinity::ui
{
    // Menu shell: a tabbed, list-based menu custom-drawn on the ImGui draw
    // list - no stock ImGui widgets - navigable with keyboard, mouse
    // (hover / click / wheel) and XInput controller.
    //
    //   Sections (tabs)  Q / E, Tab (keyboard)  -  LB / RB (controller)
    //   Navigate         arrows / d-pad; wheel; PgUp/PgDn/Home/End
    //   Select           Enter / A / click        Back  Backspace / B / Esc
    //   Reset & clear    Del / X
    //
    // Per frame while the menu is open:
    //   BeginFrame();                       // gather nav input once
    //   Begin();                            // brand bar + tab strip + breadcrumb
    //   Toggle("God Mode", &v, "desc");     // rows... (widgets.h)
    //   End();                              // footer, description, nav commit
    //
    // Row widgets live in widgets.h; include both.

    // Loads fonts and sets the UI scale. Call once, right after
    // ImGui::CreateContext and before the first NewFrame.
    void InitStyle(float uiScale);

    // Rising edge of the configured controller open combo on pad 0
    // (State::openPadMask - every bit of the mask must be held).
    bool PollToggleCombo();

    // Current button mask of controller 0 (0 if none/disconnected), read
    // through the menu's XInput bypass. Used by the Keybinds submenu's rebind
    // rows to capture a new controller binding.
    unsigned short PadButtons();

    // Same as PadButtons(), but with State::kPadLTrigger / kPadRTrigger set
    // when the matching analog trigger is pulled past the movement hook's own
    // threshold (teleport.cpp PollFlyPadMask) - so capturing a Free Flight
    // pad bind can recognize a trigger the same way the hook that reads it
    // back does. Menu Key/Button binds don't use this (PollToggleCombo only
    // ever tests the 16 real wButtons bits).
    unsigned int PadButtonsWithTriggers();

    // Rising edge of the menu toggle - INSERT (keyboard) or LB + DOWN (pad).
    // Polled once per frame from the render loop so it works regardless of
    // window focus / message delivery; flip State::menuOpen when it returns true.
    bool PollMenuToggle();

    // Top-level sections, drawn as an always-visible tab strip in the header.
    // Call once before the first Begin(). Q/E/Tab and LB/RB cycle them from
    // anywhere; switching clears the submenu stack of the old section.
    void SetTabs(const char* const* names, int count);
    int  CurrentTab();

    void BeginFrame();

    // Header (brand bar, tab strip, breadcrumb). The breadcrumb is built from
    // the tab name plus the titles of pushed submenus; `subtitleOverride`
    // replaces the last crumb when the page wants a dynamic title.
    void Begin(const char* subtitleOverride = nullptr);
    void End();

    // Call right after Begin() in menus that are pure lists (no value rows):
    // maps Left/Right (arrows / d-pad) to page jumps. PageUp / PageDown /
    // Home / End work in every menu without this.
    void ListJump();

    // Forget a submenu's selection / scroll (e.g. when its contents are
    // about to be replaced, like switching fast-travel categories).
    void ResetMenu(const char* id);

    // Programmatically push or go back one level, applied at the end of the frame.
    void PushMenu(const char* id, const char* title);
    void PopMenu();

    // Submenu id at the top of the navigation stack; "" while at a tab root.
    const char* CurrentMenu();

    // --- Toasts ---------------------------------------------------------------
    // Small transient notifications, bottom-left ("Warping to Harbor Town").
    // They outlive the menu: call DrawToasts() every frame regardless of
    // menuOpen, and include ToastsActive() in the render gate.
    void Toast(const char* fmt, ...);
    void DrawToasts();
    bool ToastsActive();
}
