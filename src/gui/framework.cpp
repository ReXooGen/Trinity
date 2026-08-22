#include "framework.h"
#include "ui_internal.h"
#include "icons.h"

#include <Windows.h>
#include <Xinput.h>
#include <imgui.h>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/state.h"
#include "../core/version.h"
#include "../core/localization.h"
#include "../hooks/xinput_hook.h"

namespace trinity::ui
{
    // --- Theme Presets & Dynamic State ---------------------------------------
    namespace theme
    {
        ImU32 HeaderTop  = IM_COL32(128,  10,  26, 255);
        ImU32 HeaderBot  = IM_COL32( 52,   4,  12, 255);
        ImU32 Accent     = IM_COL32(214,  36,  56, 255);
        ImU32 AccentDark = IM_COL32(120,  14,  30, 255);

        struct Preset
        {
            ImU32 top, bot, acc, accDark;
        };

        static const Preset kPresets[] = {
            // 0: Crimson Red (Default)
            { IM_COL32(128, 10, 26, 255), IM_COL32(52, 4, 12, 255), IM_COL32(214, 36, 56, 255), IM_COL32(120, 14, 30, 255) },
            // 1: Cyber Cyan
            { IM_COL32(10, 80, 128, 255), IM_COL32(4, 30, 52, 255), IM_COL32(0, 195, 255, 255), IM_COL32(14, 90, 120, 255) },
            // 2: Neon Purple
            { IM_COL32(100, 10, 128, 255), IM_COL32(40, 4, 52, 255), IM_COL32(185, 36, 214, 255), IM_COL32(100, 14, 120, 255) },
            // 3: Matrix Emerald
            { IM_COL32(10, 110, 50, 255), IM_COL32(4, 45, 20, 255), IM_COL32(16, 185, 100, 255), IM_COL32(10, 100, 50, 255) },
            // 4: Royal Gold
            { IM_COL32(130, 90, 10, 255), IM_COL32(55, 35, 4, 255), IM_COL32(245, 170, 20, 255), IM_COL32(130, 90, 14, 255) },
            // 5: Sunset Orange
            { IM_COL32(135, 45, 10, 255), IM_COL32(55, 18, 4, 255), IM_COL32(255, 95, 30, 255), IM_COL32(135, 50, 14, 255) }
        };

        void UpdateColors()
        {
            int idx = State::Get().themeIndex;
            if (idx < 0 || idx >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])))
                idx = 0;
            HeaderTop  = kPresets[idx].top;
            HeaderBot  = kPresets[idx].bot;
            Accent     = kPresets[idx].acc;
            AccentDark = kPresets[idx].accDark;
        }
    }

    // --- Shared state (declared in ui_internal.h) -----------------------------
    Nav      g_nav;
    ImFont*  g_fontTitle = nullptr;
    ImFont*  g_fontBody  = nullptr;
    ImFont*  g_fontBold  = nullptr;
    float    g_scale     = 1.0f;
    float    g_x = 0.0f, g_y = 0.0f, g_width = 0.0f, g_listTop = 0.0f;
    int      g_rowIndex  = 0;
    char     g_selectedDesc[256] = {};
    RowKind  g_hintKind  = RowKind::None;
    bool     g_padActive = false;
    bool     g_captureSeen = false;

    ImDrawList* DL() { return ImGui::GetForegroundDrawList(); }

    // --- Menu / navigation state ---------------------------------------------
    struct StackEntry
    {
        std::string id;
        std::string title; // breadcrumb label
    };

    static std::vector<StackEntry>                    g_stack;
    static std::unordered_map<std::string, MenuState> g_menus;
    static std::string                                g_pendingPushId;
    static std::string                                g_pendingPushTitle;
    static bool                                       g_pendingPop = false;

    static const char* const* g_tabNames  = nullptr;
    static int                g_tabCount  = 0;
    static int                g_tab       = 0;
    static int                g_pendingTab = -1; // set by clicks; End() applies

    MenuState& CurMS()
    {
        if (!g_stack.empty())
            return g_menus[g_stack.back().id];
        char key[16];
        snprintf(key, sizeof(key), "tab:%d", g_tab);
        return g_menus[key];
    }

    void RequestPush(const char* id, const char* title)
    {
        g_pendingPushId    = id;
        g_pendingPushTitle = title ? title : id;
        g_pendingPop       = false;
    }

    void SetTabs(const char* const* names, int count)
    {
        g_tabNames = names;
        g_tabCount = count;
        if (g_tab >= count) g_tab = 0;
    }

    int CurrentTab() { return g_tab; }

    // --- Fonts / style --------------------------------------------------------
    void InitStyle(float uiScale)
    {
        g_scale = uiScale < 0.5f ? 0.5f : uiScale;

        ImGuiIO& io = ImGui::GetIO();
        char windir[MAX_PATH]{};
        GetWindowsDirectoryA(windir, MAX_PATH);

        char segoePath[MAX_PATH], seguisbPath[MAX_PATH], segoeuibPath[MAX_PATH];
        char yaheiPath[MAX_PATH], yaheibdPath[MAX_PATH];
        char malgunPath[MAX_PATH], malgunbdPath[MAX_PATH];
        char meiryoPath[MAX_PATH], meiryobdPath[MAX_PATH];

        snprintf(segoePath, sizeof(segoePath), "%s\\Fonts\\segoeui.ttf", windir);
        snprintf(seguisbPath, sizeof(seguisbPath), "%s\\Fonts\\seguisb.ttf", windir);
        snprintf(segoeuibPath, sizeof(segoeuibPath), "%s\\Fonts\\segoeuib.ttf", windir);

        snprintf(yaheiPath, sizeof(yaheiPath), "%s\\Fonts\\msyh.ttc", windir);
        snprintf(yaheibdPath, sizeof(yaheibdPath), "%s\\Fonts\\msyhbd.ttc", windir);

        snprintf(malgunPath, sizeof(malgunPath), "%s\\Fonts\\malgun.ttf", windir);
        snprintf(malgunbdPath, sizeof(malgunbdPath), "%s\\Fonts\\malgunbd.ttf", windir);

        snprintf(meiryoPath, sizeof(meiryoPath), "%s\\Fonts\\meiryo.ttc", windir);
        snprintf(meiryobdPath, sizeof(meiryobdPath), "%s\\Fonts\\meiryob.ttc", windir);

        const bool hasYahei = (GetFileAttributesA(yaheiPath) != INVALID_FILE_ATTRIBUTES);
        const bool hasYaheiBd = (GetFileAttributesA(yaheibdPath) != INVALID_FILE_ATTRIBUTES);
        const bool hasMalgun = (GetFileAttributesA(malgunPath) != INVALID_FILE_ATTRIBUTES);
        const bool hasMalgunBd = (GetFileAttributesA(malgunbdPath) != INVALID_FILE_ATTRIBUTES);
        const bool hasMeiryo = (GetFileAttributesA(meiryoPath) != INVALID_FILE_ATTRIBUTES);
        const bool hasMeiryoBd = (GetFileAttributesA(meiryobdPath) != INVALID_FILE_ATTRIBUTES);

        // Build a lean, high-speed Chinese glyph range (Common 2500 + all Trinity menu characters)
        static ImVector<ImWchar> s_zhRanges;
        if (s_zhRanges.empty() && hasYahei)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            // Add all extra Chinese characters used in Trinity UI (including 辑, 镶, 嵌, 渊, 斗, 昼, 耀, etc.)
            builder.AddText("霓炫帧编辑装备精炼深渊符文镶嵌斗气落日耀橙矩阵翡翠赛博青蓝快捷键昼夜时间重置孔位个人仓库营地衣柜推进锁定加快减慢运行速度单手双手武器盾牌远程匕首头盔防具披风手套靴子项链戒指眼镜面具骑乘载具料理药水食材药材杂物书籍配方地图通缉令工具货币记忆钥匙封印文物机关控制库库罐诱饵贸易品未分类搜索输入");
            builder.BuildRanges(&s_zhRanges);
        }

        // Build Korean glyph range
        static ImVector<ImWchar> s_koRanges;
        if (s_koRanges.empty() && hasMalgun)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
            builder.BuildRanges(&s_koRanges);
        }

        // Build Japanese glyph range
        static ImVector<ImWchar> s_jaRanges;
        if (s_jaRanges.empty() && hasMeiryo)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
            builder.BuildRanges(&s_jaRanges);
        }

        // Build base glyph range (Default ASCII/Latin + Cyrillic + PlayStation Text Icons)
        static ImVector<ImWchar> s_baseRanges;
        if (s_baseRanges.empty())
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
            // Add PlayStation controller text equivalents: ✖ (Cross), ◯ (Circle), ⬜ (Square), △ (Triangle)
            builder.AddText(u8"✖◯⬜△");
            builder.BuildRanges(&s_baseRanges);
        }

        auto LoadFontWithFallbacks = [&](const char* primaryPath, const char* zhPath, const char* koPath, const char* jaPath, float size) -> ImFont*
        {
            ImFontConfig cfg;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.PixelSnapH = true;

            ImFont* font = nullptr;
            if (GetFileAttributesA(primaryPath) != INVALID_FILE_ATTRIBUTES)
                font = io.Fonts->AddFontFromFileTTF(primaryPath, size, &cfg, s_baseRanges.Data);

            if (!font)
                font = io.Fonts->AddFontDefault(&cfg);

            ImFontConfig cfgMerge;
            cfgMerge.MergeMode = true;
            cfgMerge.OversampleH = 1;
            cfgMerge.OversampleV = 1;
            cfgMerge.PixelSnapH = true;

            // Merge Chinese glyphs into THIS font
            if (zhPath && !s_zhRanges.empty())
            {
                io.Fonts->AddFontFromFileTTF(zhPath, size, &cfgMerge, s_zhRanges.Data);
            }

            // Merge Korean glyphs into THIS font
            if (koPath && !s_koRanges.empty())
            {
                io.Fonts->AddFontFromFileTTF(koPath, size, &cfgMerge, s_koRanges.Data);
            }

            // Merge Japanese glyphs into THIS font
            if (jaPath && !s_jaRanges.empty())
            {
                io.Fonts->AddFontFromFileTTF(jaPath, size, &cfgMerge, s_jaRanges.Data);
            }

            return font;
        };

        g_fontBody  = LoadFontWithFallbacks(segoePath,   hasYahei ? yaheiPath : nullptr,     hasMalgun ? malgunPath : nullptr,     hasMeiryo ? meiryoPath : nullptr,     21.0f * g_scale);
        g_fontBold  = LoadFontWithFallbacks(seguisbPath, hasYaheiBd ? yaheibdPath : (hasYahei ? yaheiPath : nullptr), hasMalgunBd ? malgunbdPath : (hasMalgun ? malgunPath : nullptr), hasMeiryoBd ? meiryobdPath : (hasMeiryo ? meiryoPath : nullptr), 21.0f * g_scale);

        // g_fontTitle is for TRINITY header (Latin)
        ImFontConfig cfgTitle;
        cfgTitle.OversampleH = 1;
        cfgTitle.OversampleV = 1;
        cfgTitle.PixelSnapH = true;
        
        char impactPath[MAX_PATH], georgiaPath[MAX_PATH];
        snprintf(impactPath, sizeof(impactPath), "%s\\Fonts\\impact.ttf", windir);
        snprintf(georgiaPath, sizeof(georgiaPath), "%s\\Fonts\\georgiab.ttf", windir);

        const trinity::State& st = trinity::State::Get();
        bool fontLoaded = false;

        if (st.useCustomFont && st.customFont[0] != '\0' && GetFileAttributesA(st.customFont) != INVALID_FILE_ATTRIBUTES)
        {
            g_fontTitle = io.Fonts->AddFontFromFileTTF(st.customFont, 35.0f * g_scale, &cfgTitle, s_baseRanges.Data);
            fontLoaded = true;
        }

        if (!fontLoaded)
        {
            if (st.builtInFontIndex == 1 && GetFileAttributesA(impactPath) != INVALID_FILE_ATTRIBUTES)
                g_fontTitle = io.Fonts->AddFontFromFileTTF(impactPath, 34.0f * g_scale, &cfgTitle, s_baseRanges.Data);
            else if (st.builtInFontIndex == 2 && GetFileAttributesA(georgiaPath) != INVALID_FILE_ATTRIBUTES)
                g_fontTitle = io.Fonts->AddFontFromFileTTF(georgiaPath, 32.0f * g_scale, &cfgTitle, s_baseRanges.Data);
            else if (GetFileAttributesA(segoeuibPath) != INVALID_FILE_ATTRIBUTES)
                g_fontTitle = io.Fonts->AddFontFromFileTTF(segoeuibPath, 30.0f * g_scale, &cfgTitle, s_baseRanges.Data);
            else
                g_fontTitle = io.Fonts->AddFontDefault(&cfgTitle);
        }

        if (fontLoaded)
        {
            // If custom font is loaded, apply it to everything (body and bold)
            ImFontConfig cfgBody;
            cfgBody.OversampleH = 1;
            cfgBody.OversampleV = 1;
            cfgBody.PixelSnapH = true;
            g_fontBody = io.Fonts->AddFontFromFileTTF(st.customFont, 24.0f * g_scale, &cfgBody, s_baseRanges.Data);
            g_fontBold = g_fontBody;
        }

        if (!g_fontBody)  g_fontBody  = io.Fonts->AddFontDefault();
        if (!g_fontBold)  g_fontBold  = g_fontBody;
        if (!g_fontTitle) g_fontTitle = g_fontBold;

        io.FontDefault = g_fontBody;
    }

    // --- Controller -----------------------------------------------------------
    // XInputGetState on a disconnected slot is expensive, so back off between
    // reconnect attempts.
    static bool PollPad(XINPUT_STATE& out)
    {
        static bool      s_connected = false;
        static ULONGLONG s_nextRetry = 0;

        const ULONGLONG now = GetTickCount64();
        if (!s_connected && now < s_nextRetry)
            return false;

        ZeroMemory(&out, sizeof(out));
        bool anyConnected = false;

        // Read the REAL pad across slots 0-3, bypassing the menu-open neutralisation
        // the XInput hook applies to the game. Merge states so an idle virtual controller
        // on slot 0 doesn't mask a real controller on slot 1.
        for (DWORD i = 0; i < 4; ++i)
        {
            XINPUT_STATE temp;
            if (hooks::XInputReadReal(i, &temp) == ERROR_SUCCESS)
            {
                anyConnected = true;
                out.Gamepad.wButtons |= temp.Gamepad.wButtons;
                
                if (temp.Gamepad.bLeftTrigger > out.Gamepad.bLeftTrigger)
                    out.Gamepad.bLeftTrigger = temp.Gamepad.bLeftTrigger;
                if (temp.Gamepad.bRightTrigger > out.Gamepad.bRightTrigger)
                    out.Gamepad.bRightTrigger = temp.Gamepad.bRightTrigger;
                    
                // Use the thumbstick values from the first controller that has them moved
                if (out.Gamepad.sThumbLX == 0 && out.Gamepad.sThumbLY == 0)
                {
                    out.Gamepad.sThumbLX = temp.Gamepad.sThumbLX;
                    out.Gamepad.sThumbLY = temp.Gamepad.sThumbLY;
                }
                if (out.Gamepad.sThumbRX == 0 && out.Gamepad.sThumbRY == 0)
                {
                    out.Gamepad.sThumbRX = temp.Gamepad.sThumbRX;
                    out.Gamepad.sThumbRY = temp.Gamepad.sThumbRY;
                }
            }
        }
        
        if (anyConnected)
        {
            s_connected = true;
            return true;
        }

        s_connected = false;
        s_nextRetry = now + 2000;
        return false;
    }

    unsigned short PadButtons()
    {
        XINPUT_STATE st;
        return PollPad(st) ? st.Gamepad.wButtons : 0;
    }

    unsigned int PadButtonsWithTriggers()
    {
        XINPUT_STATE st;
        if (!PollPad(st))
            return 0;

        unsigned int mask = st.Gamepad.wButtons;
        if (st.Gamepad.bLeftTrigger  > 64) mask |= kPadLTrigger;
        if (st.Gamepad.bRightTrigger > 64) mask |= kPadRTrigger;
        return mask;
    }

    bool PollToggleCombo()
    {
        static bool s_prev = false;

        // The whole configured mask must be held (a single button when the mask
        // is one bit, or a combo like LB + D-Pad Down). A zero mask disables the
        // controller open entirely.
        const WORD mask = static_cast<WORD>(State::Get().openPadMask);
        bool held = false;
        if (mask != 0)
        {
            XINPUT_STATE st;
            if (PollPad(st))
                held = (st.Gamepad.wButtons & mask) == mask;
        }

        const bool fired = held && !s_prev;
        s_prev = held;
        return fired;
    }

    bool PollMenuToggle()
    {
        // While a rebind row is listening, the press being captured must not
        // also open/close the menu.
        if (State::Get().rebindCapture)
            return false;

        // Keyboard bind via async key state - focus/subclass independent, so it
        // fires consistently where the old WndProc WM_KEYUP toggle didn't.
        static bool s_prevKey = false;
        const int   vk        = State::Get().openKeyVk;
        const bool  keyDown   = vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool  keyEdge   = keyDown && !s_prevKey;
        s_prevKey = keyDown;

        // Controller combo (already edge-triggered internally).
        return keyEdge || PollToggleCombo();
    }

    // Fires on press, then repeats while held (d-pad scrolling). g_padDownAt
    // doubles as the held-duration source for the adjust boost.
    static ULONGLONG g_padDownAt[4] = {};

    static bool RepeatHeld(int slot, bool held)
    {
        static ULONGLONG s_nextFire[4] = {};

        const ULONGLONG now = GetTickCount64();
        if (!held)
        {
            g_padDownAt[slot] = 0;
            return false;
        }
        if (g_padDownAt[slot] == 0)
        {
            g_padDownAt[slot] = now;
            s_nextFire[slot]  = now + 350;
            return true;
        }
        if (now >= s_nextFire[slot])
        {
            s_nextFire[slot] = now + 60;
            return true;
        }
        return false;
    }

    static bool PadHeldOver(int slot, ULONGLONG ms)
    {
        return g_padDownAt[slot] != 0 && GetTickCount64() - g_padDownAt[slot] > ms;
    }

    // --- Input gathering --------------------------------------------------------
    void BeginFrame()
    {
        g_nav         = {};
        g_captureSeen = false;
        g_hintKind    = RowKind::None;

        State&   st = State::Get();

        // While a SYSTEM-tab row is listening for a new bind, freeze menu
        // navigation: the captured key/button only binds - it never moves the
        // cursor, selects, or backs out. (g_nav stays zeroed from the reset
        // above.) The capture driver in menu.cpp does the polling.
        if (st.rebindCapture)
            return;

        ImGuiIO& io = ImGui::GetIO();

        // Keyboard (ImGui handles key repeat for held arrows).
        bool anyKey = false;
        auto key = [&](ImGuiKey k, bool repeat)
        {
            const bool p = ImGui::IsKeyPressed(k, repeat);
            anyKey |= p;
            return p;
        };

        g_nav.up       = key(ImGuiKey_UpArrow, true);
        g_nav.down     = key(ImGuiKey_DownArrow, true);
        g_nav.left     = key(ImGuiKey_LeftArrow, true);
        g_nav.right    = key(ImGuiKey_RightArrow, true);
        g_nav.pageUp   = key(ImGuiKey_PageUp, true);
        g_nav.pageDown = key(ImGuiKey_PageDown, true);
        g_nav.home     = key(ImGuiKey_Home, false);
        g_nav.end      = key(ImGuiKey_End, false);
        g_nav.select   = key(ImGuiKey_Enter, false) || key(ImGuiKey_KeypadEnter, false) || (!st.textCapture && key(ImGuiKey_Space, false));
        g_nav.back     = key(ImGuiKey_Backspace, false);
        g_nav.clear    = key(ImGuiKey_Delete, false);

        // Section switching - suspended while typing so 'q'/'e' reach the text.
        if (!st.textCapture)
        {
            if (key(ImGuiKey_Q, false))   g_nav.tabDelta = -1;
            if (key(ImGuiKey_E, false))   g_nav.tabDelta = +1;
            if (key(ImGuiKey_Tab, false)) g_nav.tabDelta = +1;
        }

        // ESC leaves text capture first, then acts as Back (End() closes the
        // menu when Back fires at a tab root). INSERT always closes outright.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            anyKey = true;
            if (st.textCapture) st.textCapture = false;
            else                g_nav.back     = true;
        }

        // Controller: d-pad repeats, buttons edge-triggered. LB doubles as the
        // menu-toggle modifier (LB + D-Pad Down), so tab-prev fires on LB
        // RELEASE and only if the combo didn't happen while it was held.
        bool padEvent = false;
        XINPUT_STATE xs;
        if (PollPad(xs))
        {
            static WORD      s_prevButtons = 0;
            static bool      s_lbHeld      = false;
            static bool      s_lbCombo     = false;
            static ULONGLONG s_lastPoll    = 0;

            const WORD b  = xs.Gamepad.wButtons;
            const bool lb = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;

            // If the menu just (re)opened with LB still held from the open
            // combo, don't treat that release as a tab switch.
            const ULONGLONG now = GetTickCount64();
            const bool reopened = now - s_lastPoll > 250;
            s_lastPoll = now;
            if (reopened)
            {
                s_prevButtons = b;
                s_lbHeld      = lb;
                s_lbCombo     = lb; // swallow the in-flight press
            }

            if (lb && !s_lbHeld) { s_lbHeld = true; s_lbCombo = false; }
            if (lb && (b & XINPUT_GAMEPAD_DPAD_DOWN)) s_lbCombo = true;
            if (!lb && s_lbHeld)
            {
                s_lbHeld = false;
                if (!s_lbCombo) { g_nav.tabDelta = -1; padEvent = true; }
            }

            // While LB is held it's a modifier - the d-pad shouldn't navigate.
            const bool dpadLive = !lb;
            g_nav.up    |= RepeatHeld(0, dpadLive && (b & XINPUT_GAMEPAD_DPAD_UP));
            g_nav.down  |= RepeatHeld(1, dpadLive && (b & XINPUT_GAMEPAD_DPAD_DOWN));
            g_nav.left  |= RepeatHeld(2, dpadLive && (b & XINPUT_GAMEPAD_DPAD_LEFT));
            g_nav.right |= RepeatHeld(3, dpadLive && (b & XINPUT_GAMEPAD_DPAD_RIGHT));

            if ((b & XINPUT_GAMEPAD_A) && !(s_prevButtons & XINPUT_GAMEPAD_A))
            {
                g_nav.select    = true;
                g_nav.selectPad = true;
            }
            g_nav.back  |= (b & XINPUT_GAMEPAD_B) && !(s_prevButtons & XINPUT_GAMEPAD_B);
            g_nav.clear |= (b & XINPUT_GAMEPAD_X) && !(s_prevButtons & XINPUT_GAMEPAD_X);
            if ((b & XINPUT_GAMEPAD_RIGHT_SHOULDER) && !(s_prevButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER))
                g_nav.tabDelta = +1;

            if (b & ~s_prevButtons)
                padEvent = true;
            s_prevButtons = b;
        }

        // The footer shows glyphs for whichever device spoke last.
        if (padEvent)    g_padActive = true;
        else if (anyKey) g_padActive = false;

        // Value rows step x10 once left/right has been held for a while.
        // (Key hold time is tracked here - ImGui's per-key DownDuration is
        // internal API in this version.)
        static ULONGLONG s_keyDownAt[2] = {};
        const ImGuiKey  boostKeys[2] = { ImGuiKey_LeftArrow, ImGuiKey_RightArrow };
        const ULONGLONG tnow = GetTickCount64();
        bool keyBoost = false;
        for (int i = 0; i < 2; ++i)
        {
            if (!ImGui::IsKeyDown(boostKeys[i])) s_keyDownAt[i] = 0;
            else if (s_keyDownAt[i] == 0)        s_keyDownAt[i] = tnow;
            else if (tnow - s_keyDownAt[i] > 1200) keyBoost = true;
        }
        g_nav.adjustBoost = keyBoost || PadHeldOver(2, 1200) || PadHeldOver(3, 1200);

        // Mouse wheel walks the list.
        const float wheel = io.MouseWheel;
        if (wheel > 0.0f)      g_nav.up   = true;
        else if (wheel < 0.0f) g_nav.down = true;
    }

    // --- Drawing helpers --------------------------------------------------------
    void ArrowH(ImDrawList* dl, ImVec2 c, float h, bool right, ImU32 col)
    {
        if (right)
            dl->AddTriangleFilled(ImVec2(c.x + h, c.y), ImVec2(c.x - h, c.y + h), ImVec2(c.x - h, c.y - h), col);
        else
            dl->AddTriangleFilled(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y - h), ImVec2(c.x + h, c.y + h), col);
    }

    void ArrowV(ImDrawList* dl, ImVec2 c, float h, bool up, ImU32 col)
    {
        if (up)
            dl->AddTriangleFilled(ImVec2(c.x, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(c.x - h, c.y + h), col);
        else
            dl->AddTriangleFilled(ImVec2(c.x, c.y + h), ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y - h), col);
    }

    static ImU32 WithAlpha(ImU32 col, float a)
    {
        const ImU32 base = (col >> IM_COL32_A_SHIFT) & 0xFF;
        ImU32 out = static_cast<ImU32>(base * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a));
        return (col & ~IM_COL32_A_MASK) | (out << IM_COL32_A_SHIFT);
    }

    // Map a tab by its display name to a category glyph (order-independent, so
    // menu.cpp can reorder tabs freely). Unknown names simply get no icon.
    static Icon TabIcon(const char* name)
    {
        if (!name) return Icon::None;
        if (!strcmp(name, "PLAYER") || !strcmp(name, LOC("PLAYER"))) return Icon::TabPlayer;
        if (!strcmp(name, "TRAVEL") || !strcmp(name, LOC("TRAVEL"))) return Icon::TabTravel;
        if (!strcmp(name, "INVENTORY") || !strcmp(name, LOC("INVENTORY"))) return Icon::TabItems;
        if (!strcmp(name, "WORLD") || !strcmp(name, LOC("WORLD"))) return Icon::TabWorld;
        if (!strcmp(name, "SYSTEM") || !strcmp(name, LOC("SYSTEM"))) return Icon::TabSystem;
        return Icon::None;
    }

    // --- Footer hint segments -------------------------------------------------
    // A footer hint is a little run of [glyph][label] pieces. When the icon
    // atlases loaded we draw the real game glyphs; otherwise End() falls back
    // to the plain-text hint strings.
    struct HintSeg { Icon ic; const char* txt; };

    // Lay out segments at font size `fz`, glyphs `gh` tall. Measures if dl==null.
    static float DrawHintSegs(ImDrawList* dl, float x, float cy, float fz, float gh,
                              const HintSeg* segs, int n, ImU32 glyphCol, ImU32 txtCol)
    {
        const float s   = g_scale;
        const float gap = 4.0f * s;   // glyph<->label
        const float seg = 10.0f * s;  // between pieces
        float cx = x;
        for (int i = 0; i < n; ++i)
        {
            if (segs[i].ic != Icon::None)
            {
                const ImVec2 isz = IconSize(segs[i].ic, gh);
                if (dl) DrawIcon(dl, segs[i].ic, ImVec2(cx, cy - isz.y * 0.5f),
                                 ImVec2(cx + isz.x, cy + isz.y * 0.5f), glyphCol);
                cx += isz.x + (segs[i].txt && segs[i].txt[0] ? gap : 4.0f * s);
            }
            if (segs[i].txt && segs[i].txt[0])
            {
                if (dl) dl->AddText(g_fontBody, fz, ImVec2(cx, cy - fz * 0.5f), txtCol, segs[i].txt);
                cx += g_fontBody->CalcTextSizeA(fz, FLT_MAX, 0.0f, segs[i].txt).x + seg;
            }
        }
        return cx - x;
    }

    // Row-context (left) hints as glyph segments. Returns count via `n`.
    static int LeftHintSegs(bool pad, RowKind k, HintSeg* o)
    {
        int n = 0;
        auto add = [&](Icon ic, const char* t) { o[n++] = { ic, t }; };
        if (pad)
        {
            switch (k)
            {
            case RowKind::Action:  add(Icon::PadA, LOC("Select")); break;
            case RowKind::Toggle:  add(Icon::PadA, LOC("Toggle")); break;
            case RowKind::Value:   add(Icon::PadDpad, LOC("Adjust")); add(Icon::PadX, LOC("Reset")); break;
            case RowKind::ToggleValue: add(Icon::PadA, LOC("Toggle")); add(Icon::PadDpad, LOC("Adjust"));
                                       add(Icon::PadX, LOC("Reset")); break;
            case RowKind::Choice:  add(Icon::PadDpad, LOC("Pick")); break;
            case RowKind::Submenu: add(Icon::PadA, LOC("Open")); break;
            case RowKind::Search:  add(Icon::PadA, LOC("Type")); add(Icon::PadX, LOC("Clear")); break;
            case RowKind::Typing:  add(Icon::PadA, LOC("Done")); add(Icon::PadB, LOC("Erase")); break;
            case RowKind::TypingApply: add(Icon::PadA, LOC("Apply")); add(Icon::PadB, LOC("Erase")); break;
            case RowKind::Item:    add(Icon::PadDpad, LOC("Amount")); add(Icon::PadX, LOC("Remove")); break;
            case RowKind::ItemAdd: add(Icon::PadDpad, LOC("Amount")); add(Icon::PadA, LOC("Add")); break;
            case RowKind::ValueAction: add(Icon::PadDpad, LOC("Amount")); add(Icon::PadA, LOC("Apply"));
                                       add(Icon::PadX, LOC("Reset")); break;
            case RowKind::Bind:    add(Icon::PadDpad, LOC("Pick")); add(Icon::PadA, LOC("Rebind"));
                                       add(Icon::PadX, LOC("Reset")); break;
            case RowKind::Bookmark: add(Icon::PadA, LOC("Open")); add(Icon::PadX, LOC("Delete")); break;
            default: break;
            }
        }
        else
        {
            switch (k)
            {
            case RowKind::Action:  add(Icon::KeyEnter, LOC("Select")); break;
            case RowKind::Toggle:  add(Icon::KeyEnter, LOC("Toggle")); break;
            case RowKind::Value:   add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Adjust"));
                                   add(Icon::KeyDel, LOC("Reset")); break;
            case RowKind::ToggleValue: add(Icon::KeyEnter, LOC("Toggle")); add(Icon::KeyLeft, "");
                                       add(Icon::KeyRight, LOC("Adjust")); add(Icon::KeyDel, LOC("Reset")); break;
            case RowKind::Choice:  add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Pick")); break;
            case RowKind::Submenu: add(Icon::KeyEnter, LOC("Open")); break;
            case RowKind::Search:  add(Icon::KeyEnter, LOC("Type")); add(Icon::KeyDel, LOC("Clear")); break;
            case RowKind::Typing:  add(Icon::KeyEnter, LOC("Done")); add(Icon::KeyBackspace, LOC("Erase")); break;
            case RowKind::TypingApply: add(Icon::KeyEnter, LOC("Apply"));
                                       add(Icon::KeyBackspace, LOC("Erase")); break;
            case RowKind::Item:    add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Amount"));
                                   add(Icon::KeyEnter, LOC("Type")); add(Icon::KeyDel, LOC("Remove")); break;
            case RowKind::ItemAdd: add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Amount"));
                                   add(Icon::KeyEnter, LOC("Add")); break;
            case RowKind::ValueAction: add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Amount"));
                                       add(Icon::KeyEnter, LOC("Apply")); add(Icon::KeyDel, LOC("Reset")); break;
            case RowKind::Bind:    add(Icon::KeyLeft, ""); add(Icon::KeyRight, LOC("Pick"));
                                   add(Icon::KeyEnter, LOC("Rebind")); add(Icon::KeyDel, LOC("Reset")); break;
            case RowKind::Bookmark: add(Icon::KeyEnter, LOC("Open")); add(Icon::KeyDel, LOC("Delete")); break;
            default: break;
            }
        }
        return n;
    }

    // Back + section-switch (right) hints as glyph segments.
    static int RightHintSegs(bool pad, bool atRoot, HintSeg* o)
    {
        int n = 0;
        auto add = [&](Icon ic, const char* t) { o[n++] = { ic, t }; };
        if (pad)
        {
            add(Icon::PadB, atRoot ? LOC("Close") : LOC("Back"));
            add(Icon::PadLB, ""); add(Icon::PadRB, LOC("Tab"));
        }
        else
        {
            add(Icon::KeyBackspace, atRoot ? LOC("Close") : LOC("Back"));
            add(Icon::KeyQ, ""); add(Icon::KeyE, LOC("Tab"));
        }
        return n;
    }

    // --- Header ----------------------------------------------------------------
    void Begin(const char* subtitleOverride)
    {
        ImDrawList* dl = DL();
        ImGuiIO&    io = ImGui::GetIO();
        const float s  = g_scale;

        theme::UpdateColors();

        g_width           = 460.0f * s;
        g_x               = 64.0f * s;
        g_y               = 64.0f * s;
        g_rowIndex        = 0;
        g_selectedDesc[0] = 0;

        // Apply up/down using last frame's row count (immediate mode: rows for
        // this frame aren't known yet).
        MenuState& ms = CurMS();
        if (ms.count > 0)
        {
            if (g_nav.up)   ms.selected = (ms.selected - 1 + ms.count) % ms.count;
            if (g_nav.down) ms.selected = (ms.selected + 1) % ms.count;

            // Page / edge jumps clamp instead of wrapping - jumping past the
            // end of a long list should land ON the end, not loop around.
            if (g_nav.pageUp)   ms.selected -= kMaxVisible;
            if (g_nav.pageDown) ms.selected += kMaxVisible;
            if (g_nav.home)     ms.selected  = 0;
            if (g_nav.end)      ms.selected  = ms.count - 1;
            if (ms.selected < 0)          ms.selected = 0;
            if (ms.selected >= ms.count)  ms.selected = ms.count - 1;

            if (ms.selected < ms.scroll)                ms.scroll = ms.selected;
            if (ms.selected >= ms.scroll + kMaxVisible) ms.scroll = ms.selected - kMaxVisible + 1;
        }

        // Selection cursor animation: glide toward the selected row; snap on
        // big jumps (page moves, menu switches) so it never chases across the
        // whole list.
        if (ms.count > 0)
        {
            const float target = static_cast<float>(ms.selected - ms.scroll);
            if (ms.animY < 0.0f || fabsf(ms.animY - target) > static_cast<float>(kMaxVisible))
                ms.animY = target;
            else
            {
                const float dt = io.DeltaTime > 0.05f ? 0.05f : io.DeltaTime;
                float k = dt * 18.0f;
                if (k > 1.0f) k = 1.0f;
                ms.animY += (target - ms.animY) * k;
            }
        }
        else
        {
            ms.animY = -1.0f;
        }

        // Brand bar: gradient, TRINITY left, version right.
        const float brandH = 46.0f * s;
        dl->AddRectFilledMultiColor(
            ImVec2(g_x, g_y), ImVec2(g_x + g_width, g_y + brandH),
            theme::HeaderTop, theme::HeaderTop, theme::HeaderBot, theme::HeaderBot);

        const float tsz = g_fontTitle->FontSize;
        const ImVec2 tp(g_x + 16.0f * s, g_y + (brandH - tsz) * 0.5f);
        dl->AddText(g_fontTitle, tsz, ImVec2(tp.x + 2.0f * s, tp.y + 2.0f * s), theme::Shadow, "TRINITY");
        dl->AddText(g_fontTitle, tsz, tp, theme::TextBright, "TRINITY");

        const char*  ver = "v" TRINITY_VERSION;
        const float  vz  = g_fontBody->FontSize * 0.78f;
        const ImVec2 vs  = g_fontBody->CalcTextSizeA(vz, FLT_MAX, 0.0f, ver);
        dl->AddText(g_fontBody, vz,
                    ImVec2(g_x + g_width - vs.x - 12.0f * s, g_y + (brandH - vz) * 0.5f),
                    WithAlpha(theme::TextBright, 0.55f), ver);
        g_y += brandH;

        // Tab strip: every section always visible, one press away.
        const float tabH = 34.0f * s;
        dl->AddRectFilled(ImVec2(g_x, g_y), ImVec2(g_x + g_width, g_y + tabH), theme::BarBg);
        if (g_tabCount > 0)
        {
            const float cw = g_width / static_cast<float>(g_tabCount);
            const float fz = g_fontBold->FontSize * 0.80f;
            const bool  mouseMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;

            for (int i = 0; i < g_tabCount; ++i)
            {
                const ImVec2 mn(g_x + i * cw, g_y);
                const ImVec2 mx(mn.x + cw, g_y + tabH);
                const bool   active = (i == g_tab);
                const bool   hover  = io.MousePos.x >= mn.x && io.MousePos.x < mx.x &&
                                      io.MousePos.y >= mn.y && io.MousePos.y < mx.y;
                if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    g_pendingTab = i;

                if (active)
                {
                    dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 10));
                    dl->AddRectFilled(ImVec2(mn.x, mx.y - 3.0f * s), mx, theme::Accent);
                }
                else if (hover && mouseMoved)
                {
                    dl->AddRectFilled(ImVec2(mn.x, mx.y - 2.0f * s), mx, theme::AccentDark);
                }

                const ImU32   fg = active ? theme::TextBright : hover ? theme::Text : theme::TextDim;
                const ImVec2  ts = g_fontBold->CalcTextSizeA(fz, FLT_MAX, 0.0f, g_tabNames[i]);

                // Category glyph (if loaded) sits left of the label; the whole
                // icon+label group is centered in the tab.
                const Icon   ic  = TabIcon(g_tabNames[i]);
                const float  icoH = tabH * 0.52f;
                const ImVec2 isz  = ui::IconSize(ic, icoH);
                const float  igap = isz.x > 0.0f ? 6.0f * s : 0.0f;
                float        gx   = mn.x + (cw - (isz.x + igap + ts.x)) * 0.5f;
                const float  midY = mn.y + tabH * 0.5f;

                if (isz.x > 0.0f)
                {
                    ui::DrawIcon(dl, ic, ImVec2(gx, midY - isz.y * 0.5f),
                                 ImVec2(gx + isz.x, midY + isz.y * 0.5f),
                                 WithAlpha(IM_COL32_WHITE, active ? 1.0f : hover ? 0.85f : 0.55f));
                    gx += isz.x + igap;
                }
                dl->AddText(g_fontBold, fz,
                            ImVec2(gx, mn.y + (tabH - fz) * 0.5f), fg, g_tabNames[i]);
            }
        }
        g_y += tabH;

        // Breadcrumb bar: where you are, plus "selected / count" right.
        const float crumbH = 27.0f * s;
        dl->AddRectFilled(ImVec2(g_x, g_y), ImVec2(g_x + g_width, g_y + crumbH), theme::CrumbBg);

        char pos[32];
        snprintf(pos, sizeof(pos), "%d / %d", ms.count > 0 ? ms.selected + 1 : 0, ms.count);
        const float  ph = g_fontBody->FontSize * 0.86f;
        const ImVec2 ps = g_fontBody->CalcTextSizeA(ph, FLT_MAX, 0.0f, pos);
        dl->AddText(g_fontBody, ph,
                    ImVec2(g_x + g_width - ps.x - 12.0f * s, g_y + (crumbH - ph) * 0.5f),
                    theme::TextDim, pos);

        {
            // Crumb parts: tab name, then each pushed submenu's title.
            const char* parts[8];
            int n = 0;
            parts[n++] = (g_tabCount > 0) ? g_tabNames[g_tab] : "TRINITY";
            for (const StackEntry& e : g_stack)
                if (n < 8) parts[n++] = e.title.c_str();
            if (subtitleOverride && n > 1)
                parts[n - 1] = subtitleOverride;

            const float cz    = g_fontBold->FontSize * 0.86f;
            const float sepW  = g_fontBody->CalcTextSizeA(cz, FLT_MAX, 0.0f, "  >  ").x;
            const float availW = g_width - ps.x - 40.0f * s;

            // Drop leading ancestors if the trail is too wide.
            int first = 0;
            for (;;)
            {
                float w = 0.0f;
                for (int i = first; i < n; ++i)
                {
                    w += g_fontBold->CalcTextSizeA(cz, FLT_MAX, 0.0f, parts[i]).x;
                    if (i > first) w += sepW;
                }
                if (w <= availW || first >= n - 1) break;
                ++first;
            }

            float cx = g_x + 12.0f * s;
            const float cy = g_y + (crumbH - cz) * 0.5f;
            if (first > 0)
            {
                dl->AddText(g_fontBody, cz, ImVec2(cx, cy), theme::TextDim, "..  >  ");
                cx += g_fontBody->CalcTextSizeA(cz, FLT_MAX, 0.0f, "..  >  ").x;
            }
            for (int i = first; i < n; ++i)
            {
                const bool last = (i == n - 1);
                dl->AddText(g_fontBold, cz, ImVec2(cx, cy),
                            last ? theme::Accent : theme::TextDim, parts[i]);
                cx += g_fontBold->CalcTextSizeA(cz, FLT_MAX, 0.0f, parts[i]).x;
                if (!last)
                {
                    dl->AddText(g_fontBody, cz, ImVec2(cx, cy), theme::TextDim, "  >  ");
                    cx += sepW;
                }
            }
        }
        g_y += crumbH + 2.0f * s;

        // Rows render into split channels so the animated highlight in End()
        // slides between their backgrounds and their text.
        g_listTop = g_y;
        dl->ChannelsSplit(3);
        dl->ChannelsSetCurrent(kChFg);
    }

    // --- Footer hints -----------------------------------------------------------
    static const char* LeftHint(bool pad, RowKind k)
    {
        if (pad)
        {
            switch (k)
            {
            case RowKind::Action:  return "A select";
            case RowKind::Toggle:  return "A toggle";
            case RowKind::ToggleValue: return "A toggle   < > adjust   X reset";
            case RowKind::Value:   return "< > adjust   X reset";
            case RowKind::Choice:  return "< > pick";
            case RowKind::Submenu: return "A open";
            case RowKind::Search:  return "A type   X clear";
            case RowKind::Typing:  return "A done   B erase";
            case RowKind::TypingApply: return "A apply   B erase";
            case RowKind::Item:    return "< > amount   X remove";
            case RowKind::ItemAdd: return "< > amount   A add";
            case RowKind::ValueAction: return "< > amount   A apply   X reset";
            case RowKind::Bind:    return "< > pick   A rebind   X reset";
            default:               return "";
            }
        }
        switch (k)
        {
        case RowKind::Action:  return "Enter select";
        case RowKind::Toggle:  return "Enter toggle";
        case RowKind::ToggleValue: return "Enter toggle   < > adjust   Del reset";
        case RowKind::Value:   return "< > adjust   Enter type   Del reset";
        case RowKind::Choice:  return "< > pick";
        case RowKind::Submenu: return "Enter open";
        case RowKind::Search:  return "Enter type   Del clear";
        case RowKind::Typing:  return "Enter done   Bksp erase";
        case RowKind::TypingApply: return "Enter apply   Bksp erase";
        case RowKind::Item:    return "< > amount   Enter type   Del remove";
        case RowKind::ItemAdd: return "< > amount   Enter add";
        case RowKind::ValueAction: return "< > amount   Enter type   Del reset";
        case RowKind::Bind:    return "< > pick   Enter rebind   Del reset";
        default:               return "";
        }
    }

    void End()
    {
        MenuState& ms = CurMS();
        ms.count = g_rowIndex;
        if (ms.selected >= ms.count) ms.selected = ms.count > 0 ? ms.count - 1 : 0;
        if (ms.scroll > ms.selected) ms.scroll = ms.selected;

        ImDrawList* dl = DL();
        const float s  = g_scale;

        // Animated selection highlight, drawn between row backgrounds and text.
        if (ms.count > 0 && ms.animY >= 0.0f)
        {
            dl->ChannelsSetCurrent(kChHi);
            const float stride = 37.0f * s;
            const float y0     = g_listTop + ms.animY * stride;
            dl->AddRectFilledMultiColor(
                ImVec2(g_x, y0), ImVec2(g_x + g_width, y0 + 36.0f * s),
                theme::Accent, theme::AccentDark, theme::AccentDark, theme::Accent);
        }
        dl->ChannelsMerge();

        // Scrollbar along the right edge of the rows.
        if (ms.count > kMaxVisible && g_y > g_listTop)
        {
            const float top = g_listTop;
            const float bot = g_y - 1.0f * s;
            const float h   = bot - top;
            dl->AddRectFilled(ImVec2(g_x + g_width - 4.0f * s, top),
                              ImVec2(g_x + g_width, bot), IM_COL32(255, 255, 255, 22));
            const float th = h * static_cast<float>(kMaxVisible) / static_cast<float>(ms.count);
            const float ty = top + h * static_cast<float>(ms.scroll) / static_cast<float>(ms.count);
            dl->AddRectFilled(ImVec2(g_x + g_width - 4.0f * s, ty),
                              ImVec2(g_x + g_width, ty + th), WithAlpha(theme::Accent, 0.85f));
        }

        // Footer: what the selected row responds to (left) + back / sections
        // (right), in the glyphs of whichever device was used last.
        const float fH = 26.0f * s;
        dl->AddRectFilled(ImVec2(g_x, g_y), ImVec2(g_x + g_width, g_y + fH), theme::BarBg);
        const float cy     = g_y + fH * 0.5f;
        const float hintSz = g_fontBody->FontSize * 0.78f;
        const bool  atRoot = g_stack.empty();

        if (IconsReady())
        {
            // Real game glyphs. Left = selected-row context; right = back/tabs.
            const float  gh = fH * 0.86f;
            const ImU32  gcol = WithAlpha(IM_COL32_WHITE, 0.90f);
            HintSeg lseg[6]; const int ln = LeftHintSegs(g_padActive, g_hintKind, lseg);
            DrawHintSegs(dl, g_x + 12.0f * s, cy, hintSz, gh, lseg, ln, gcol, theme::TextDim);

            HintSeg rseg[4]; const int rn = RightHintSegs(g_padActive, atRoot, rseg);
            const float rw = DrawHintSegs(nullptr, 0, cy, hintSz, gh, rseg, rn, gcol, theme::TextDim);
            DrawHintSegs(dl, g_x + g_width - rw - 12.0f * s, cy, hintSz, gh, rseg, rn, gcol, theme::TextDim);
        }
        else
        {
            dl->AddText(g_fontBody, hintSz, ImVec2(g_x + 12.0f * s, cy - hintSz * 0.5f),
                        theme::TextDim, LeftHint(g_padActive, g_hintKind));

            const char* right = g_padActive
                                    ? (atRoot ? "B close   LB/RB tab" : "B back   LB/RB tab")
                                    : (atRoot ? "Bksp close   Q/E tab" : "Bksp back   Q/E tab");
            const ImVec2 rs = g_fontBody->CalcTextSizeA(hintSz, FLT_MAX, 0.0f, right);
            dl->AddText(g_fontBody, hintSz,
                        ImVec2(g_x + g_width - rs.x - 12.0f * s, cy - hintSz * 0.5f),
                        theme::TextDim, right);
        }
        g_y += fH;

        // Description box for the selected row.
        if (g_selectedDesc[0])
        {
            g_y += 8.0f * s;
            const float  pad   = 12.0f * s;
            const float  wrapW = g_width - pad * 2.0f - 4.0f * s;
            const float  dh    = g_fontBody->FontSize;
            const ImVec2 dsz   = g_fontBody->CalcTextSizeA(dh, FLT_MAX, wrapW, g_selectedDesc);

            const ImVec2 mn(g_x, g_y);
            const ImVec2 mx(g_x + g_width, g_y + dsz.y + pad * 2.0f);
            dl->AddRectFilled(mn, mx, theme::RowBg);
            dl->AddRectFilled(mn, ImVec2(mn.x + 3.0f * s, mx.y), theme::Accent);
            dl->AddText(g_fontBody, dh, ImVec2(mn.x + pad + 4.0f * s, mn.y + pad),
                        theme::Text, g_selectedDesc, nullptr, wrapW);
        }

        // A menu without a capture-capable row can't be capturing text - drop
        // the flag if the user backed out of a menu mid-typing.
        if (!g_captureSeen)
            State::Get().textCapture = false;

        // Structural nav is deferred to here so it can't affect rows mid-frame.
        // A section switch clears the old section's submenu stack.
        if (g_nav.tabDelta != 0 && g_tabCount > 0 && g_pendingTab < 0)
            g_pendingTab = (g_tab + g_nav.tabDelta + g_tabCount) % g_tabCount;

        if (g_pendingTab >= 0)
        {
            if (g_pendingTab != g_tab)
            {
                g_tab = g_pendingTab;
                g_stack.clear();
                State::Get().textCapture = false;
            }
            g_pendingTab = -1;
            g_pendingPushId.clear();
        }
        else if (!g_pendingPushId.empty())
        {
            g_stack.push_back({ g_pendingPushId, g_pendingPushTitle });
            g_pendingPushId.clear();
        }
        else if (g_pendingPop || g_nav.back)
        {
            if (g_stack.empty()) State::Get().menuOpen = false;
            else                 g_stack.pop_back();
        }
        g_pendingPop = false;
    }

    void PushMenu(const char* id, const char* title)
    {
        g_pendingPushId    = id    ? id    : "";
        g_pendingPushTitle = title ? title : "";
    }

    void PopMenu()
    {
        // Deferred to End() so the current frame's rows finish first.
        g_pendingPop = true;
    }

    const char* CurrentMenu()
    {
        return g_stack.empty() ? "" : g_stack.back().id.c_str();
    }

    void ListJump()
    {
        // Uses last frame's row count, same as Begin(); called between
        // Begin() and the first row so the rows see the final selection.
        MenuState& ms = CurMS();
        if (ms.count > 0)
        {
            if (g_nav.left)  ms.selected -= kMaxVisible;
            if (g_nav.right) ms.selected += kMaxVisible;
            if (ms.selected < 0)         ms.selected = 0;
            if (ms.selected >= ms.count) ms.selected = ms.count - 1;
            if (ms.selected < ms.scroll)                ms.scroll = ms.selected;
            if (ms.selected >= ms.scroll + kMaxVisible) ms.scroll = ms.selected - kMaxVisible + 1;
        }
        // Consumed either way so rows never also react to left/right.
        g_nav.left = g_nav.right = false;
    }

    void ResetMenu(const char* id)
    {
        g_menus.erase(id);
    }

    // --- Toasts ------------------------------------------------------------------
    namespace
    {
        constexpr ULONGLONG kToastLife = 2600; // ms
        constexpr ULONGLONG kToastIn   = 150;
        constexpr ULONGLONG kToastOut  = 350;

        struct ToastItem
        {
            char      text[120];
            ULONGLONG born;
        };

        ToastItem g_toasts[4] = {};
        int       g_toastCount = 0;
    }

    void Toast(const char* fmt, ...)
    {
        // Newest first; the oldest falls off the end.
        if (g_toastCount < 4) ++g_toastCount;
        for (int i = g_toastCount - 1; i > 0; --i)
            g_toasts[i] = g_toasts[i - 1];

        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_toasts[0].text, sizeof(g_toasts[0].text), fmt, ap);
        va_end(ap);
        g_toasts[0].born = GetTickCount64();
    }

    bool ToastsActive()
    {
        const ULONGLONG now = GetTickCount64();
        for (int i = 0; i < g_toastCount; ++i)
            if (now - g_toasts[i].born < kToastLife)
                return true;
        return false;
    }

    void DrawToasts()
    {
        if (g_toastCount == 0)
            return;

        ImDrawList*     dl  = DL();
        const ImGuiIO&  io  = ImGui::GetIO();
        const float     s   = g_scale;
        const ULONGLONG now = GetTickCount64();

        // Newest at the bottom, stacking upward.
        float y = io.DisplaySize.y - 110.0f * s;
        for (int i = 0; i < g_toastCount; ++i)
        {
            const ULONGLONG age = now - g_toasts[i].born;
            if (age >= kToastLife)
                continue;

            float in = age < kToastIn ? static_cast<float>(age) / kToastIn : 1.0f;
            in = 1.0f - (1.0f - in) * (1.0f - in); // ease-out
            const float out   = age > kToastLife - kToastOut
                                    ? static_cast<float>(kToastLife - age) / kToastOut
                                    : 1.0f;
            const float alpha = in * out;

            const float  tz = g_fontBody->FontSize * 0.92f;
            const ImVec2 ts = g_fontBody->CalcTextSizeA(tz, FLT_MAX, 0.0f, g_toasts[i].text);
            const float  h  = 34.0f * s;
            const float  x  = 64.0f * s - (1.0f - in) * 24.0f * s; // slide in

            const ImVec2 mn(x, y - h);
            const ImVec2 mx(x + ts.x + 34.0f * s, y);
            dl->AddRectFilled(mn, mx, WithAlpha(theme::RowBg, alpha));
            dl->AddRectFilled(mn, ImVec2(mn.x + 3.0f * s, mx.y), WithAlpha(theme::Accent, alpha));
            dl->AddText(g_fontBody, tz,
                        ImVec2(mn.x + 14.0f * s, mn.y + (h - tz) * 0.5f),
                        WithAlpha(theme::Text, alpha), g_toasts[i].text);

            y -= (h + 8.0f * s);
        }
    }
}
