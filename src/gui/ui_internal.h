#pragma once

// Internal contract between the menu shell (framework.cpp) and the row
// widgets (widgets.cpp). Nothing outside src/gui should include this.

#include <imgui.h>

namespace trinity::ui
{
    // --- Theme (Crimson Desert: deep reds on near-black) ---------------------
    namespace theme
    {
        extern ImU32 HeaderTop;
        extern ImU32 HeaderBot;
        extern ImU32 Accent;
        extern ImU32 AccentDark;
        inline constexpr ImU32 RowBg      = IM_COL32( 13,  13,  16, 234);
        inline constexpr ImU32 BarBg      = IM_COL32(  7,   7,   9, 245);
        inline constexpr ImU32 CrumbBg    = IM_COL32( 10,  10,  12, 245);
        inline constexpr ImU32 Text       = IM_COL32(228, 226, 222, 255);
        inline constexpr ImU32 TextBright = IM_COL32(255, 255, 255, 255);
        inline constexpr ImU32 TextDim    = IM_COL32(148, 146, 142, 255);
        inline constexpr ImU32 Shadow     = IM_COL32(  0,   0,   0, 160);
        inline constexpr ImU32 SwitchOff  = IM_COL32( 70,  70,  78, 255);
        inline constexpr ImU32 Knob       = IM_COL32(245, 245, 245, 255);

        void UpdateColors();
    }

    inline constexpr int kMaxVisible = 12;

    // The row area is channel-split so the animated selection highlight can
    // slide BETWEEN the row backgrounds and the row text.
    inline constexpr int kChBg = 0; // row background fills
    inline constexpr int kChHi = 1; // animated selection highlight
    inline constexpr int kChFg = 2; // row text / knobs / values

    // Navigation input for the current frame, gathered once in BeginFrame()
    // from keyboard, mouse wheel and controller. Widgets may CONSUME a flag
    // (set it false) to stop later handlers reacting to it - e.g. Search
    // consumes `back` while erasing text so End() doesn't pop the menu.
    struct Nav
    {
        bool up = false, down = false, left = false, right = false;
        bool pageUp = false, pageDown = false, home = false, end = false;
        bool select = false;      // Enter / A / row click
        bool selectPad = false;   // the select above came from a controller
        bool back = false;        // Backspace / B (Esc maps here too)
        bool clear = false;       // Del / X - reset value, clear search
        bool adjustBoost = false; // left/right held long: value rows step x10
        int  tabDelta = 0;        // -1 / +1 section switch (Q/E, Tab, LB/RB)
    };

    // Per-menu persistent list state.
    struct MenuState
    {
        int   selected = 0;
        int   scroll   = 0;
        int   count    = 0;     // rows drawn last frame; nav uses this
        float animY    = -1.0f; // selection cursor position, in visible-row units
    };

    // What the selected row is, so the footer can show the exact controls it
    // responds to. Widgets set this via RowBase; Typing overrides it while a
    // row is capturing text.
    // ItemAdd is Item's sibling for the Add Item catalog: same amount stepping,
    // but Enter/A commits the add instead of starting inline typing, and there
    // is nothing to remove.
    // ValueAction is a Value that DOES something when its number is committed
    // (Set All), which is why it needs TypingApply rather than reusing Typing:
    // Enter there is not "done", it is the press that runs the action, and a
    // footer saying otherwise would hide that.
    // Bind is the keybinds page's two-column rebind row (keyboard | controller):
    // Left/Right pick which device column has focus, Enter/A rebinds it, Del/X
    // resets it - so its footer must advertise "pick" as well as rebind/reset.
    enum class RowKind { None, Action, Toggle, Value, Choice, Submenu, Search, Typing, ToggleValue, Item, ItemAdd, ValueAction, TypingApply, Bind, Bookmark };

    // --- Shared state (defined in framework.cpp) -----------------------------
    extern Nav     g_nav;
    extern ImFont* g_fontTitle;
    extern ImFont* g_fontBody;
    extern ImFont* g_fontBold;
    extern float   g_scale;

    // Per-frame layout cursor (set up by Begin, advanced by rows).
    extern float g_x, g_y, g_width, g_listTop;
    extern int   g_rowIndex;

    // The selected row's description, drawn later in End(). COPIED, not kept
    // as a pointer: callers routinely snprintf every row's desc into one
    // reused stack buffer.
    extern char    g_selectedDesc[256];
    extern RowKind g_hintKind;

    // True when the last nav input came from a controller - the footer shows
    // pad glyphs instead of keyboard keys.
    extern bool g_padActive;

    // Set by any row that can capture typed text (Search, value edit) each
    // frame it is drawn; End() drops State::textCapture when no such row
    // exists in the current menu (e.g. after backing out mid-typing).
    extern bool g_captureSeen;

    // State of the menu the caller is currently rendering.
    MenuState& CurMS();

    // Deferred submenu push (applied in End). `title` feeds the breadcrumb.
    void RequestPush(const char* id, const char* title);

    ImDrawList* DL(); // the foreground draw list everything renders to

    void ArrowH(ImDrawList* dl, ImVec2 c, float h, bool right, ImU32 col);
    void ArrowV(ImDrawList* dl, ImVec2 c, float h, bool up, ImU32 col);
}
