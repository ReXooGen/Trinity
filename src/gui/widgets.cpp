#include "widgets.h"
#include "ui_internal.h"
#include "icons.h"

#include <Windows.h>
#include <imgui.h>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "../core/state.h"
#include "../core/localization.h"

namespace trinity::ui
{
    static ImU32 WithAlpha(ImU32 col, float a)
    {
        const ImU32 base = (col >> IM_COL32_A_SHIFT) & 0xFF;
        ImU32 out = static_cast<ImU32>(base * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a));
        return (col & ~IM_COL32_A_MASK) | (out << IM_COL32_A_SHIFT);
    }

    // --- Row core -----------------------------------------------------------------
    struct RowResult
    {
        bool   selected  = false;
        bool   activated = false;
        bool   left      = false;
        bool   right     = false;
        bool   clear     = false;
        bool   drawn     = false;
        ImVec2 mn{}, mx{};
    };

    // Brief border flash on the row that was just activated - tactile
    // feedback for one-shot actions (Copy, Refresh, warp...).
    static const void* s_flashMenu  = nullptr;
    static int         s_flashRow   = -1;
    static ULONGLONG   s_flashUntil = 0;

    // `itemIcon`, when non-null, reserves a square icon box at the left of
    // the row and draws that game icon in it (a dim placeholder frame when the
    // item has no icon or it failed to load - the space is always reserved so
    // labels in a mixed list stay aligned).
    static void FitLabel(char* out, size_t cap, const char* label, float availW);

    static RowResult RowBase(const char* label, const char* desc, RowKind kind,
                             const char* itemIcon = nullptr)
    {
        MenuState& ms = CurMS();
        const int  i  = g_rowIndex++;
        RowResult  r;

        const bool visible = i >= ms.scroll && i < ms.scroll + kMaxVisible;
        if (visible)
        {
            const float s    = g_scale;
            const float rowH = 36.0f * s;
            r.mn    = ImVec2(g_x, g_y);
            r.mx    = ImVec2(g_x + g_width, g_y + rowH);
            r.drawn = true;

            // Mouse: moving over a row selects it, clicking activates it.
            ImGuiIO&   io    = ImGui::GetIO();
            const bool hover = io.MousePos.x >= r.mn.x && io.MousePos.x < r.mx.x &&
                               io.MousePos.y >= r.mn.y && io.MousePos.y < r.mx.y;
            if (hover && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
                ms.selected = i;
            if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ms.selected = i;
                r.activated = true;
            }
        }

        r.selected = (i == ms.selected);
        if (r.selected)
        {
            snprintf(g_selectedDesc, sizeof(g_selectedDesc), "%s", desc ? desc : "");
            g_hintKind   = kind;
            r.activated |= g_nav.select;
            r.left       = g_nav.left;
            r.right      = g_nav.right;
            r.clear      = g_nav.clear;
        }

        const ULONGLONG now = GetTickCount64();
        if (r.activated)
        {
            s_flashMenu  = &ms;
            s_flashRow   = i;
            s_flashUntil = now + 260;
        }

        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float s  = g_scale;

            dl->ChannelsSetCurrent(kChBg);
            dl->AddRectFilled(r.mn, r.mx, theme::RowBg);
            dl->ChannelsSetCurrent(kChFg);

            if (s_flashMenu == &ms && s_flashRow == i && now < s_flashUntil)
            {
                const float a = static_cast<float>(s_flashUntil - now) / 260.0f;
                dl->AddRect(r.mn, r.mx, WithAlpha(theme::TextBright, a), 0.0f, 0, 1.5f * s);
            }

            float textX = r.mn.x + 14.0f * s;
            if (itemIcon)
            {
                const float  inset = 4.0f * s;
                const float  ih    = (r.mx.y - r.mn.y) - inset * 2.0f;
                const ImVec2 imn(textX, r.mn.y + inset);
                const ImVec2 imx(textX + ih, r.mn.y + inset + ih);
                if (!DrawItemIcon(dl, itemIcon, imn, imx))
                    dl->AddRect(imn, imx, WithAlpha(theme::TextDim, 0.35f),
                                2.0f * s, 0, 1.0f * s);
                textX += ih + 10.0f * s;
            }

            // Labels with no caller-drawn value column (plain actions, submenu
            // rows) can carry a game item name long enough to run off the row -
            // or under the submenu chevron - so ellipsise those to fit. Value
            // rows keep their hand-written short labels untouched.
            const char* drawLabel = label;
            char fitted[192];
            if (kind == RowKind::Action || kind == RowKind::Submenu)
            {
                float avail = r.mx.x - textX - 14.0f * s;
                if (kind == RowKind::Submenu) avail -= 16.0f * s; // the chevron
                FitLabel(fitted, sizeof(fitted), label, avail);
                drawLabel = fitted;
            }

            // On icon rows the label sits a hair higher: the font's glyphs
            // render visually low in their box, which reads as misaligned next
            // to the exactly-centered icon.
            const float th = g_fontBody->FontSize;
            const float textY = r.mn.y + ((r.mx.y - r.mn.y) - th) * 0.5f -
                                (itemIcon ? 1.5f * s : 0.0f);
            dl->AddText(g_fontBody, th, ImVec2(textX, textY),
                        r.selected ? theme::TextBright : theme::Text, drawLabel);

            g_y = r.mx.y + 1.0f * s;
        }
        return r;
    }

    // Right-aligned value text; when the row is selected (and not typing),
    // < > adjust arrows flank it.
    static void DrawRowValue(const RowResult& r, const char* text, bool arrows,
                             ImU32 colOverride = 0)
    {
        if (!r.drawn)
            return;

        ImDrawList*  dl = DL();
        const float  s  = g_scale;
        const float  th = g_fontBody->FontSize;
        const ImVec2 ts = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, text);
        const float  cy = (r.mn.y + r.mx.y) * 0.5f;

        float tx = r.mx.x - 14.0f * s - ts.x;
        if (r.selected && arrows)
            tx -= 14.0f * s;

        const ImU32 col = colOverride ? colOverride
                        : r.selected  ? theme::TextBright : theme::TextDim;
        dl->AddText(g_fontBody, th, ImVec2(tx, cy - th * 0.5f), col, text);

        if (r.selected && arrows)
        {
            const float ah = 5.0f * s;
            ArrowH(dl, ImVec2(tx - 11.0f * s, cy), ah, false, theme::TextBright);
            ArrowH(dl, ImVec2(tx + ts.x + 11.0f * s, cy), ah, true, theme::TextBright);
        }
    }

    // --- Text capture ownership -------------------------------------------------
    // At most one row captures typed text at a time, and it must be able to say
    // "is it mine?": a Search row and an editable row now share a menu (the
    // inventory pages put a search box above editable item rows), and Search is
    // drawn first. Without an owner it would see "capturing, but I'm not
    // selected" and drop the capture the row below had just taken - so typing a
    // quantity would have been impossible on any page with a search box.
    static const void* s_capOwner = nullptr;

    static void CapBegin(const void* owner)
    {
        s_capOwner = owner;
        State::Get().textCapture = true;
    }
    static void CapEnd(const void* owner)
    {
        if (s_capOwner != owner) return;
        s_capOwner = nullptr;
        State::Get().textCapture = false;
    }
    // False once BeginFrame drops textCapture on Esc, so owners read that as a
    // cancel without needing to watch for it themselves.
    static bool CapMine(const void* owner)
    {
        return s_capOwner == owner && State::Get().textCapture;
    }

    // --- Inline numeric editing ------------------------------------------------
    // Enter on a value row starts typing an exact number in place; Enter
    // commits, moving off the row (or Esc) cancels. Keyed by the value
    // pointer so exactly one row can be editing at a time.
    static void* s_editPtr    = nullptr;
    static char  s_editBuf[24] = {};

    static void EditBegin(void* key)
    {
        s_editPtr    = key;
        s_editBuf[0] = 0;
        CapBegin(key);
    }

    static void EditEnd()
    {
        CapEnd(s_editPtr);
        s_editPtr = nullptr;
    }

    // Returns 0 = still typing, 1 = committed (*out valid), 2 = cancelled.
    //
    // `emptyValue`, when given, is what an empty buffer commits to instead of
    // cancelling - for rows that start typing with the current value shown as a
    // placeholder, where "type nothing and press Enter" plainly means the number
    // already on the row rather than "never mind".
    static int EditTick(bool allowDot, double* out, const double* emptyValue = nullptr)
    {
        // Esc (BeginFrame drops the capture) reads as a cancel.
        if (!CapMine(s_editPtr))
            return 2;
        g_captureSeen = true;

        if (g_nav.back)
        {
            g_nav.back = false;
            const size_t len = strlen(s_editBuf);
            if (len > 0) s_editBuf[len - 1] = 0;
            else         return 2;
        }

        ImGuiIO& io = ImGui::GetIO();
        for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
        {
            const ImWchar c = io.InputQueueCharacters[n];
            const bool ok = (c >= '0' && c <= '9') ||
                            (allowDot && c == '.' && !strchr(s_editBuf, '.')) ||
                            (c == '-' && s_editBuf[0] == 0);
            if (ok)
            {
                const size_t len = strlen(s_editBuf);
                if (len + 1 < sizeof(s_editBuf))
                {
                    s_editBuf[len]     = static_cast<char>(c);
                    s_editBuf[len + 1] = 0;
                }
            }
        }
        io.InputQueueCharacters.resize(0);

        if (g_nav.select)
        {
            g_nav.select = false;
            if (s_editBuf[0] == 0 || (s_editBuf[0] == '-' && s_editBuf[1] == 0))
            {
                if (!emptyValue) return 2;
                *out = *emptyValue;
                return 1;
            }
            *out = atof(s_editBuf);
            return 1;
        }
        return 0;
    }

    // `placeholder`, when given, is drawn dim in place of an empty buffer - the
    // value the row would commit to if Enter came now (see EditTick). Typing
    // replaces it because the buffer really is empty, which is the select-all
    // behaviour of a normal text field without the state to track a selection.
    static void DrawEditValue(const RowResult& r, const char* placeholder = nullptr)
    {
        char text[32];
        const bool blink = (GetTickCount64() / 530) & 1;
        const bool empty = s_editBuf[0] == 0;
        snprintf(text, sizeof(text), "%s%s",
                 empty && placeholder ? placeholder : s_editBuf, blink ? "_" : " ");
        DrawRowValue(r, text, false,
                     empty && placeholder ? theme::TextDim : theme::TextBright);
    }

    // --- Widgets --------------------------------------------------------------------
    bool Option(const char* label, const char* desc)
    {
        return RowBase(label, desc, RowKind::Action).activated;
    }

    bool OptionItem(const char* label, const char* icon, const char* desc)
    {
        return RowBase(label, desc, RowKind::Action, icon).activated;
    }

    // Draws an on/off switch with its right edge at `trackRight`, vertically
    // centered on `cy`. The sliding-knob animation is keyed by `animKey` (the
    // value pointer). Shared by Toggle and ToggleFloat so the switch can sit
    // either flush against the row edge or left of a value column.
    static void DrawSwitch(ImDrawList* dl, bool value, bool selected,
                           float trackRight, float cy, const void* animKey)
    {
        const float  s      = g_scale;
        const float  trackW = 40.0f * s;
        const float  trackH = 18.0f * s;
        const ImVec2 tmn(trackRight - trackW, cy - trackH * 0.5f);
        const ImVec2 tmx(trackRight,          cy + trackH * 0.5f);

        ImU32 track, knob;
        if (value)
        {
            track = selected ? IM_COL32(255, 255, 255, 235) : theme::Accent;
            knob  = selected ? theme::AccentDark : theme::Knob;
        }
        else
        {
            track = selected ? IM_COL32(0, 0, 0, 120) : theme::SwitchOff;
            knob  = theme::Knob;
        }

        static std::unordered_map<const void*, float> s_anim;
        const float target = value ? 1.0f : 0.0f;
        auto  ins = s_anim.emplace(animKey, target);
        float& k  = ins.first->second;
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt > 0.05f) dt = 0.05f;
            float rate = dt * 16.0f;
            if (rate > 1.0f) rate = 1.0f;
            k += (target - k) * rate;
        }

        dl->AddRectFilled(tmn, tmx, track, trackH * 0.5f);
        const float kr  = trackH * 0.5f - 2.0f * s;
        const float kx0 = tmn.x + kr + 2.0f * s;
        const float kx1 = tmx.x - kr - 2.0f * s;
        dl->AddCircleFilled(ImVec2(kx0 + (kx1 - kx0) * k, cy), kr, knob);
    }

    bool Toggle(const char* label, bool* value, const char* desc)
    {
        RowResult  r       = RowBase(label, desc, RowKind::Toggle);
        const bool changed = r.activated || r.left || r.right;
        if (changed)
            *value = !*value;

        if (r.drawn)
        {
            const float s  = g_scale;
            const float cy = (r.mn.y + r.mx.y) * 0.5f;
            // Knob slides between the ends instead of teleporting; the
            // animation is keyed by the value pointer (stable for the app's
            // lifetime).
            DrawSwitch(DL(), *value, r.selected, r.mx.x - 14.0f * s, cy, value);
        }
        return changed;
    }

    bool FloatOption(const char* label, float* value, float minV, float maxV,
                     float step, float defV, const char* fmt, const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::Value);
        bool      changed = false;
        bool      editing = (s_editPtr == value);

        if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                if (g_hintKind == RowKind::Value) g_hintKind = RowKind::Typing;
                double out = 0.0;
                const int rc = EditTick(true, &out);
                if (rc == 1)
                {
                    if (out < minV) out = minV;
                    if (out > maxV) out = maxV;
                    *value  = static_cast<float>(out);
                    changed = true;
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            const float st = step * (g_nav.adjustBoost ? 10.0f : 1.0f);
            if (r.left)  { *value -= st; changed = true; }
            if (r.right) { *value += st; changed = true; }
            if (r.activated && !g_nav.selectPad)
            {
                EditBegin(value);
                editing = true;
            }
            if (r.clear)
            {
                g_nav.clear = false;
                if (*value != defV) { *value = defV; changed = true; }
            }
        }

        if (*value < minV) *value = minV;
        if (*value > maxV) *value = maxV;

        if (editing)
        {
            g_captureSeen = true;
            DrawEditValue(r);
        }
        else
        {
            char buf[64];
            snprintf(buf, sizeof(buf), fmt, *value);
            DrawRowValue(r, buf, true);
        }
        return changed;
    }

    bool ToggleFloat(const char* label, bool* enabled, float* value,
                     float minV, float maxV, float step, float defV,
                     const char* fmt, const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::ToggleValue);
        bool      changed = false;
        bool      editing = (s_editPtr == value);

        const float s          = g_scale;
        const float trackW     = 40.0f * s;
        const float trackRight = r.mx.x - 14.0f * s;
        const float valRight   = trackRight - trackW - 20.0f * s;

        if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                if (g_hintKind == RowKind::ToggleValue) g_hintKind = RowKind::Typing;
                double out = 0.0;
                const int rc = EditTick(true, &out);
                if (rc == 1)
                {
                    if (out < minV) out = minV;
                    if (out > maxV) out = maxV;
                    *value   = static_cast<float>(out);
                    *enabled = true;
                    changed  = true;
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            if (r.activated)
            {
                ImGuiIO& io = ImGui::GetIO();
                const bool onValueText = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                                          io.MousePos.x >= valRight - 60.0f * s &&
                                          io.MousePos.x <= valRight + 10.0f * s);
                if (onValueText)
                {
                    EditBegin(value);
                    editing = true;
                }
                else
                {
                    *enabled = !*enabled;
                    changed  = true;
                }
            }

            if (r.selected && !editing && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                *enabled = !*enabled;
                changed  = true;
            }

            // Direct digit typing: pressing numbers 0-9 starts typing mode automatically
            if (r.selected && !editing)
            {
                ImGuiIO& io = ImGui::GetIO();
                for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
                {
                    const ImWchar c = io.InputQueueCharacters[n];
                    if ((c >= '0' && c <= '9') || c == '.')
                    {
                        EditBegin(value);
                        editing = true;
                        break;
                    }
                }
            }

            const float st = step * (g_nav.adjustBoost ? 10.0f : 1.0f);
            if (r.left)  { *value -= st; changed = true; }
            if (r.right) { *value += st; changed = true; }
            if (r.clear)
            {
                g_nav.clear = false;
                if (*value != defV) { *value = defV; changed = true; }
            }
        }

        if (*value < minV) *value = minV;
        if (*value > maxV) *value = maxV;

        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float cy = (r.mn.y + r.mx.y) * 0.5f;

            // Switch flush against the row's right edge.
            DrawSwitch(dl, *enabled, r.selected, trackRight, cy, enabled);

            // Value + adjust arrows, right-aligned just left of the switch.
            char buf[64];
            snprintf(buf, sizeof(buf), fmt, *value);
            const float th       = g_fontBody->FontSize;
            const float valRight = trackRight - trackW - 20.0f * s;

            if (editing)
            {
                g_captureSeen = true;
                char text[64];
                const bool blink = (GetTickCount64() / 530) & 1;
                const bool empty = s_editBuf[0] == 0;
                snprintf(text, sizeof(text), "%s%s",
                         empty ? buf : s_editBuf, blink ? "_" : " ");
                const ImVec2 ets = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, text);
                const float  evx = valRight - ets.x;
                dl->AddText(g_fontBody, th, ImVec2(evx, cy - th * 0.5f),
                            empty ? theme::TextDim : theme::TextBright, text);
            }
            else
            {
                const ImVec2 ts  = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, buf);
                const float  vx  = valRight - ts.x;
                const ImU32 vcol = r.selected ? theme::TextBright
                                 : *enabled   ? theme::Text : theme::TextDim;
                dl->AddText(g_fontBody, th, ImVec2(vx, cy - th * 0.5f), vcol, buf);

                if (r.selected)
                {
                    const float ah = 5.0f * s;
                    ArrowH(dl, ImVec2(vx - 11.0f * s, cy), ah, false, theme::TextBright);
                    ArrowH(dl, ImVec2(valRight + 11.0f * s, cy), ah, true, theme::TextBright);
                }
            }
        }
        return changed;
    }

    bool ToggleInt(const char* label, bool* enabled, int* value,
                   int minV, int maxV, int step, int defV,
                   const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::ToggleValue);
        bool      changed = false;
        bool      editing = (s_editPtr == value);

        const float s          = g_scale;
        const float trackW     = 40.0f * s;
        const float trackRight = r.mx.x - 14.0f * s;

        if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                if (g_hintKind == RowKind::ToggleValue) g_hintKind = RowKind::Typing;
                double out = 0.0;
                const int rc = EditTick(false, &out);
                if (rc == 1)
                {
                    if (out < minV) out = minV;
                    if (out > maxV) out = maxV;
                    *value   = static_cast<int>(out);
                    *enabled = true;
                    changed  = true;
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            if (r.activated)
            {
                ImGuiIO& io = ImGui::GetIO();
                const float valRight = trackRight - trackW - 20.0f * s;
                const bool onValueText = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                                          io.MousePos.x >= valRight - 60.0f * s &&
                                          io.MousePos.x <= valRight + 10.0f * s);
                if (onValueText)
                {
                    EditBegin(value);
                    editing = true;
                }
                else
                {
                    *enabled = !*enabled;
                    changed  = true;
                }
            }

            if (r.selected && !editing && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                *enabled = !*enabled;
                changed  = true;
            }

            // Direct digit typing: pressing numbers 0-9 starts typing mode automatically
            if (r.selected && !editing)
            {
                ImGuiIO& io = ImGui::GetIO();
                for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
                {
                    const ImWchar c = io.InputQueueCharacters[n];
                    if (c >= '0' && c <= '9')
                    {
                        EditBegin(value);
                        editing = true;
                        break;
                    }
                }
            }

            const int st = step * (g_nav.adjustBoost ? 10 : 1);
            if (r.left)  { *value -= st; changed = true; }
            if (r.right) { *value += st; changed = true; }
            if (r.clear)
            {
                g_nav.clear = false;
                if (*value != defV) { *value = defV; changed = true; }
            }
        }

        if (*value < minV) *value = minV;
        if (*value > maxV) *value = maxV;

        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float cy = (r.mn.y + r.mx.y) * 0.5f;

            // Switch flush against the row's right edge.
            DrawSwitch(dl, *enabled, r.selected, trackRight, cy, enabled);

            // Value + adjust arrows, right-aligned just left of the switch.
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", *value);
            const float th       = g_fontBody->FontSize;
            const float valRight = trackRight - trackW - 20.0f * s;

            if (editing)
            {
                g_captureSeen = true;
                char text[32];
                const bool blink = (GetTickCount64() / 530) & 1;
                const bool empty = s_editBuf[0] == 0;
                snprintf(text, sizeof(text), "%s%s",
                         empty ? buf : s_editBuf, blink ? "_" : " ");
                const ImVec2 ets = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, text);
                const float  evx = valRight - ets.x;
                dl->AddText(g_fontBody, th, ImVec2(evx, cy - th * 0.5f),
                            empty ? theme::TextDim : theme::TextBright, text);
            }
            else
            {
                const ImVec2 ts  = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, buf);
                const float  vx  = valRight - ts.x;
                const ImU32 vcol = r.selected ? theme::TextBright
                                 : *enabled   ? theme::Text : theme::TextDim;
                dl->AddText(g_fontBody, th, ImVec2(vx, cy - th * 0.5f), vcol, buf);

                if (r.selected)
                {
                    const float ah = 5.0f * s;
                    ArrowH(dl, ImVec2(vx - 11.0f * s, cy), ah, false, theme::TextBright);
                    ArrowH(dl, ImVec2(valRight + 11.0f * s, cy), ah, true, theme::TextBright);
                }
            }
        }
        return changed;
    }

    bool IntOption(const char* label, int* value, int minV, int maxV,
                   int step, int defV, const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::Value);
        bool      changed = false;
        bool      editing = (s_editPtr == value);

        if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                if (g_hintKind == RowKind::Value) g_hintKind = RowKind::Typing;
                double out = 0.0;
                const int rc = EditTick(false, &out);
                if (rc == 1)
                {
                    if (out < minV) out = minV;
                    if (out > maxV) out = maxV;
                    *value  = static_cast<int>(out);
                    changed = true;
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            const int st = step * (g_nav.adjustBoost ? 10 : 1);
            if (r.left)  { *value -= st; changed = true; }
            if (r.right) { *value += st; changed = true; }
            if (r.activated && !g_nav.selectPad)
            {
                EditBegin(value);
                editing = true;
            }
            if (r.clear)
            {
                g_nav.clear = false;
                if (*value != defV) { *value = defV; changed = true; }
            }
        }

        if (*value < minV) *value = minV;
        if (*value > maxV) *value = maxV;

        if (editing)
        {
            g_captureSeen = true;
            DrawEditValue(r);
        }
        else
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", *value);
            DrawRowValue(r, buf, true);
        }
        return changed;
    }

    // IntOption's sibling that fires on commit rather than reporting every
    // change - see widgets.h for why Enter can be both the edit and the action.
    bool IntAction(const char* label, int* value, int minV, int maxV,
                   int step, int defV, const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::ValueAction);
        bool      fired   = false;
        bool      editing = (s_editPtr == value);

        if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                if (g_hintKind == RowKind::ValueAction) g_hintKind = RowKind::TypingApply;

                // An empty buffer commits the amount already on the row, so
                // Enter twice fires with what you can see and there is no
                // "type it out again" tax on the amount you already picked.
                const double cur = static_cast<double>(*value);
                double    out    = 0.0;
                const int rc     = EditTick(false, &out, &cur);
                if (rc == 1)
                {
                    if (out < minV) out = minV;
                    if (out > maxV) out = maxV;
                    *value = static_cast<int>(out);
                    fired  = true;
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            const int st = step * (g_nav.adjustBoost ? 10 : 1);
            if (r.left)  *value -= st;
            if (r.right) *value += st;

            if (r.activated)
            {
                // A pad cannot type, so there A fires with the amount shown -
                // the same split ItemRow makes. Keyboard and mouse get the
                // edit, and the commit that follows is what fires.
                if (g_nav.selectPad)
                {
                    fired = true;
                }
                else
                {
                    EditBegin(value);
                    editing = true;
                }
            }
            if (r.clear)
            {
                g_nav.clear = false;
                *value = defV;
            }
        }

        if (*value < minV) *value = minV;
        if (*value > maxV) *value = maxV;

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", *value);

        if (editing)
        {
            g_captureSeen = true;
            DrawEditValue(r, buf); // the amount an empty buffer would fire with
        }
        else
        {
            DrawRowValue(r, buf, true);
        }
        return fired;
    }

    bool Combo(const char* label, int* index, const char* const* items,
               int count, const char* desc)
    {
        RowResult r       = RowBase(label, desc, RowKind::Choice);
        bool      changed = false;
        if (count > 0)
        {
            if (*index < 0 || *index >= count)
                *index = 0;
            if (r.left)                  { *index = (*index - 1 + count) % count; changed = true; }
            if (r.right || r.activated)  { *index = (*index + 1) % count;         changed = true; }
            DrawRowValue(r, items[*index], true);
        }
        return changed;
    }

    bool Submenu(const char* label, const char* id, const char* desc)
    {
        return SubmenuItem(label, nullptr, id, desc);
    }

    bool SubmenuItem(const char* label, const char* icon, const char* id, const char* desc)
    {
        RowResult r = RowBase(label, desc, RowKind::Submenu, icon);
        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float s  = g_scale;
            ArrowH(dl, ImVec2(r.mx.x - 20.0f * s, (r.mn.y + r.mx.y) * 0.5f), 5.0f * s, true,
                   r.selected ? theme::TextBright : theme::TextDim);
        }
        if (r.activated)
        {
            // Breadcrumb title = the label minus any trailing "  (count)".
            char title[96];
            snprintf(title, sizeof(title), "%s", label);
            if (char* cut = strstr(title, "  ("))
                *cut = 0;
            RequestPush(id, title); // applied in End() so this frame's rows are unaffected
        }
        return r.activated;
    }

    bool Search(char* buf, size_t cap, const char* desc)
    {
        g_captureSeen = true;

        RowResult r  = RowBase(LOC("Search"), desc, RowKind::Search);
        bool changed = false;

        // Enter (or click / A) toggles typing; moving off the row stops it.
        // Only ever the capture this row itself took - an editable row further
        // down the same menu may own it instead.
        if (r.activated)
        {
            if (CapMine(buf)) CapEnd(buf);
            else              CapBegin(buf);
        }
        if (!r.selected)
            CapEnd(buf);

        // Del / X wipes the whole filter in one press.
        if (r.selected && g_nav.clear && buf[0])
        {
            g_nav.clear = false;
            buf[0]      = 0;
            changed     = true;
        }

        const bool typing = CapMine(buf);
        if (typing)
        {
            g_hintKind = RowKind::Typing;

            // Backspace edits instead of navigating back; on an empty buffer
            // it stops typing. Consumed so End() never pops the menu.
            if (g_nav.back)
            {
                g_nav.back = false;
                const size_t len = strlen(buf);
                if (len > 0) { buf[len - 1] = 0; changed = true; }
                else         CapEnd(buf);
            }

            // Type-to-filter: feed every character the OS delivered this frame
            // into the buffer, in order. ImGui's input queue has already filtered
            // out non-printables.
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
            {
                const ImWchar c = io.InputQueueCharacters[i];
                if (c >= 32 && c < 127) // ascii only for the filter
                {
                    const size_t len = strlen(buf);
                    if (len + 1 < cap)
                    {
                        buf[len]     = static_cast<char>(c);
                        buf[len + 1] = 0;
                        changed      = true;
                    }
                }
            }
            io.InputQueueCharacters.resize(0);
        }

        if (r.drawn)
        {
            char text[96];
            if (typing)
            {
                const bool blink = (GetTickCount64() / 530) & 1;
                snprintf(text, sizeof(text), "%s%s", buf, blink ? "_" : " ");
            }
            else
            {
                snprintf(text, sizeof(text), "%s",
                         buf[0] ? buf : g_padActive ? LOC("A to type") : LOC("Enter to type"));
            }

            const ImU32 col = typing      ? theme::TextBright
                            : buf[0]      ? theme::Accent
                            : r.selected  ? theme::TextBright : theme::TextDim;
            DrawRowValue(r, text, false, col);
        }
        return changed;
    }

    bool TextInput(const char* label, char* buf, size_t cap, const char* desc)
    {
        g_captureSeen = true;

        RowResult r  = RowBase(label, desc, RowKind::Search);
        bool changed = false;

        if (r.activated)
        {
            if (CapMine(buf)) CapEnd(buf);
            else              CapBegin(buf);
        }
        if (!r.selected)
            CapEnd(buf);

        if (r.selected && g_nav.clear && buf[0])
        {
            g_nav.clear = false;
            buf[0]      = 0;
            changed     = true;
        }

        const bool typing = CapMine(buf);
        if (typing)
        {
            g_hintKind = RowKind::Typing;

            if (g_nav.back)
            {
                g_nav.back = false;
                const size_t len = strlen(buf);
                if (len > 0) { buf[len - 1] = 0; changed = true; }
                else         CapEnd(buf);
            }

            ImGuiIO& io = ImGui::GetIO();
            for (int n = 0; n < io.InputQueueCharacters.Size; ++n)
            {
                const ImWchar c = io.InputQueueCharacters[n];
                if (c >= 32 && c < 127)
                {
                    const size_t len = strlen(buf);
                    if (len + 1 < cap)
                    {
                        buf[len]     = static_cast<char>(c);
                        buf[len + 1] = 0;
                        changed      = true;
                    }
                }
            }
            io.InputQueueCharacters.resize(0);
        }

        if (r.drawn)
        {
            char text[96];
            if (typing)
            {
                const bool blink = (GetTickCount64() / 530) & 1;
                snprintf(text, sizeof(text), "%s%s", buf, blink ? "_" : " ");
            }
            else
            {
                snprintf(text, sizeof(text), "%s",
                         buf[0] ? buf : g_padActive ? "A to edit" : "Enter to edit");
            }

            const ImU32 col = typing      ? theme::TextBright
                            : buf[0]      ? theme::Accent
                            : r.selected  ? theme::TextBright : theme::TextDim;
            DrawRowValue(r, text, false, col);
        }
        return changed;
    }

    BookmarkAction BookmarkRow(const char* label, const char* desc)
    {
        RowResult r = RowBase(label, desc, RowKind::Bookmark);

        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float s  = g_scale;
            const float cy = (r.mn.y + r.mx.y) * 0.5f;
            const float rx = r.mx.x - 14.0f * s;
            ArrowH(dl, ImVec2(rx - 4.0f * s, cy), 6.0f * s, true,
                   r.selected ? theme::TextBright : theme::TextDim);
        }

        if (r.selected && g_nav.clear)
        {
            g_nav.clear = false;
            return BookmarkAction::Delete;
        }

        if (r.activated)
        {
            return BookmarkAction::Open;
        }

        return BookmarkAction::None;
    }

    // --- Color swatch row -------------------------------------------------------
    // See widgets.h. One RowBase row whose value area is circular color
    // buttons; the focus ring is this row's own second axis, so Up/Down walk
    // the grid rows while Left/Right walk the colors within one.
    int SwatchRow(const char* label, const uint32_t* rgb, int count,
                  int* cursor, int current, const char* desc, int clearIndex)
    {
        if (count < 1 || !cursor)
            return -1;

        const bool labelled = label && label[0];
        RowResult  r = RowBase(labelled ? label : "", desc, RowKind::Choice);

        int& cur = *cursor;
        if (cur < 0)      cur = 0;
        if (cur >= count) cur = count - 1;

        if (r.left  && cur > 0)         --cur;
        if (r.right && cur < count - 1) ++cur;

        if (r.drawn)
        {
            ImDrawList* dl  = DL();
            const float s   = g_scale;
            const float cy  = (r.mn.y + r.mx.y) * 0.5f;
            const float rad = 11.0f * s;
            float       pitch = 30.0f * s;

            float x0;
            if (labelled)
            {
                // Right-aligned like every value column.
                x0 = r.mx.x - 14.0f * s - rad - pitch * (count - 1);
            }
            else
            {
                // Grid style: swatches start at the left inset. Shrink the
                // pitch only if the row genuinely cannot fit them.
                x0 = r.mn.x + 14.0f * s + rad;
                const float avail = g_width - 28.0f * s - rad * 2.0f;
                if (count > 1 && pitch * (count - 1) > avail)
                    pitch = avail / (count - 1);
            }

            // The swatch under the mouse takes the focus ring, so the click
            // RowBase just reported picks what the pointer is on.
            ImGuiIO& io = ImGui::GetIO();
            if (io.MousePos.y >= r.mn.y && io.MousePos.y < r.mx.y &&
                (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
            {
                const int i = static_cast<int>((io.MousePos.x - x0 + pitch * 0.5f) / pitch);
                if (i >= 0 && i < count)
                    cur = i;
            }

            for (int i = 0; i < count; ++i)
            {
                const ImVec2   c(x0 + pitch * i, cy);
                const uint32_t v     = rgb[i];
                const int      cr    = (v >> 16) & 0xFF;
                const int      cg    = (v >> 8) & 0xFF;
                const int      cb    = v & 0xFF;
                const bool     focus = r.selected && i == cur;
                const float    rr    = focus ? rad + 1.5f * s : rad - 1.0f * s;

                if (i == clearIndex)
                {
                    // "Remove dye" swatch: a muted disc with a diagonal slash -
                    // the universal "none" marker, so clearing sits inline with
                    // the palette instead of taking its own row.
                    dl->AddCircleFilled(c, rr, IM_COL32(38, 38, 42, 255));
                    dl->AddCircle(c, rr, WithAlpha(theme::TextDim, 0.55f), 0, 1.0f * s);
                    const float d = rr * 0.58f;
                    dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d),
                                theme::Accent, 2.0f * s);
                }
                else
                {
                    dl->AddCircleFilled(c, rr, IM_COL32(cr, cg, cb, 255));
                    // Hairline so near-black swatches still read against the row.
                    dl->AddCircle(c, rr, WithAlpha(theme::TextDim, 0.45f), 0, 1.0f * s);
                }
                if (focus)
                    dl->AddCircle(c, rr + 2.0f * s, theme::TextBright, 0, 2.0f * s);
                if (i != clearIndex && i == current)
                {
                    // "You are wearing this one" dot, in whichever of black /
                    // white the swatch itself is not.
                    const int lum = (cr * 299 + cg * 587 + cb * 114) / 1000;
                    dl->AddCircleFilled(c, 2.5f * s,
                        lum > 140 ? IM_COL32(20, 20, 22, 255) : theme::Knob);
                }
            }
        }

        return r.activated ? cur : -1;
    }

    // --- Inventory item row ---------------------------------------------------
    // Quantities are edited straight on the row (see widgets.h), so an item
    // list behaves like any other value list rather than gating every change
    // behind a popup.
    static const long long kItemQtyMax = 999999999;

    // Which item row is typing. Unlike the value rows there is no per-row
    // pointer to key on, so the shared editor is keyed on this variable's
    // address and the item's identity is held alongside it.
    static unsigned long long s_itemEditKey = 0;

    // Del / X removes, but only as the second press on the same row within the
    // window below - see widgets.h for why removal confirms and nothing else
    // does. Disarming clears the deadline too, so a stale one can never make a
    // later row read as already-armed.
    static unsigned long long s_armKey   = 0;
    static ULONGLONG          s_armUntil = 0;

    static bool Armed(unsigned long long key)
    {
        return s_armKey == key && GetTickCount64() < s_armUntil;
    }
    static void Disarm()
    {
        s_armKey   = 0;
        s_armUntil = 0;
    }

    // Ellipsise `label` to `availW`. Item names are the game's, not ours: some
    // are longer than the row, and unlike the hand-written labels everywhere
    // else they cannot just be kept short - left alone they run under the
    // amount on their right.
    static void FitLabel(char* out, size_t cap, const char* label, float availW)
    {
        const float th = g_fontBody->FontSize;
        if (g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, label).x <= availW)
        {
            snprintf(out, cap, "%s", label);
            return;
        }

        const float dots = g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, "...").x;
        char  probe[160];
        size_t n = strlen(label);
        if (n >= sizeof(probe)) n = sizeof(probe) - 1;
        while (n > 1)
        {
            --n;
            memcpy(probe, label, n);
            probe[n] = 0;
            if (g_fontBody->CalcTextSizeA(th, FLT_MAX, 0.0f, probe).x + dots <= availW)
                break;
        }
        snprintf(out, cap, "%.*s...", static_cast<int>(n), label);
    }

    ItemEdit ItemRow(const char* label, const char* icon, long long qty,
                     unsigned long long key, bool locked, long long* outQty,
                     const char* desc)
    {
        if (outQty) *outQty = qty;
        if (key == 0) locked = true; // no identity - nothing safe to edit

        // The amount is measured before the row is drawn so the name can be
        // fitted around it. Every state of the value column is about this wide
        // ("x999999999", "Remove?", ten typed digits), so one reservation does
        // for all of them.
        char value[32];
        snprintf(value, sizeof(value), "x%lld", qty);

        const float s = g_scale;
        float avail = g_width - 28.0f * s;                // row padding, both sides
        if (icon) avail -= 38.0f * s;                     // icon box + its gap
        avail -= g_fontBody->CalcTextSizeA(g_fontBody->FontSize, FLT_MAX, 0.0f, value).x;
        avail -= 28.0f * s;                               // adjust arrows + breathing room

        char fitted[160];
        FitLabel(fitted, sizeof(fitted), label, avail);

        // Locked rows are inert, so they advertise no controls of their own.
        RowResult r = RowBase(fitted, desc, locked ? RowKind::None : RowKind::Item, icon);

        ItemEdit  result  = ItemEdit::None;
        bool      editing = (s_editPtr == &s_itemEditKey && s_itemEditKey == key);

        // Typing is anchored to the selected row, so if the selected row is not
        // the one typing then nothing is. Releasing the capture here is what
        // catches a row whose item changed under an edit (its key moved, so the
        // branches below no longer recognise it as editing) - left alone it
        // would swallow the game's keyboard input with no way back.
        if (r.selected && !editing && s_editPtr == &s_itemEditKey)
            EditEnd();

        // Leaving an armed row disarms it, which is what makes "move away to
        // keep it" true - a stray Del must not lie in wait to be confirmed by
        // an unrelated press later.
        if (!r.selected && Armed(key))
            Disarm();

        if (locked)
        {
            if (editing) { EditEnd(); editing = false; }
        }
        else if (editing)
        {
            if (!r.selected)
            {
                EditEnd();
                editing = false;
            }
            else
            {
                g_hintKind = RowKind::Typing;
                double out = 0.0;
                const int rc = EditTick(false, &out);
                if (rc == 1)
                {
                    long long v = static_cast<long long>(out);
                    if (v < 0)           v = 0;
                    if (v > kItemQtyMax) v = kItemQtyMax;
                    if (v != qty && outQty) { *outQty = v; result = ItemEdit::SetQty; }
                }
                if (rc != 0)
                {
                    EditEnd();
                    editing = false;
                }
            }
        }
        else
        {
            const long long step = g_nav.adjustBoost ? 10 : 1;
            long long v = qty;
            if (r.left)  v -= step;
            if (r.right) v += step;
            if (v < 0)           v = 0;
            if (v > kItemQtyMax) v = kItemQtyMax;
            if (v != qty && outQty)
            {
                *outQty = v;
                result  = ItemEdit::SetQty;
                Disarm(); // adjusting is not a step towards removing
            }

            // Enter types an exact amount (keyboard only, like the value rows -
            // a pad has nothing to type with).
            if (r.activated && !g_nav.selectPad)
            {
                s_itemEditKey = key;
                EditBegin(&s_itemEditKey);
                editing = true;
                Disarm();
            }

            if (r.clear)
            {
                g_nav.clear = false;
                if (Armed(key))
                {
                    Disarm();
                    result = ItemEdit::Remove;
                }
                else
                {
                    s_armKey   = key;
                    s_armUntil = GetTickCount64() + 2500;
                }
            }
        }

        const bool armed = Armed(key);

        // The armed row says so where its amount was, and the description spells
        // out what the next press does - the whole point of confirming is lost
        // if the row looks unchanged.
        if (armed && r.selected)
            snprintf(g_selectedDesc, sizeof(g_selectedDesc),
                     "Remove %s for good? This cannot be undone - press %s again to "
                     "confirm, or move away to keep it.",
                     label, g_padActive ? "X" : "Del");

        if (editing)
        {
            g_captureSeen = true;
            DrawEditValue(r);
        }
        else if (armed)
        {
            // Short enough to sit in the value column - the description carries
            // the actual warning.
            DrawRowValue(r, "Remove?", false, theme::Accent);
        }
        else
        {
            // Draw the amount this frame's input just produced, rather than the
            // one passed in: the caller only sees it on the next frame, and a
            // held arrow should not visibly trail the press.
            if (result == ItemEdit::SetQty && outQty)
                snprintf(value, sizeof(value), "x%lld", *outQty);
            DrawRowValue(r, value, !locked, locked ? theme::TextDim : 0);
        }
        return result;
    }

    // --- Catalog "add this item" row (see widgets.h) ---------------------------
    // The pending amount, and the row it belongs to. Kept here rather than in the
    // caller so it resets on its own when the selection moves: an amount carried
    // over from the last item is a trap.
    static unsigned long long s_addKey = 0;
    static long long          s_addQty = 1;

    long long ItemAddRow(const char* label, const char* icon,
                         unsigned long long key, bool locked, const char* desc)
    {
        if (key == 0) locked = true; // no identity - nothing safe to act on

        // This row's amount: 1 unless it is the row currently being adjusted.
        long long qty = (key != 0 && key == s_addKey) ? s_addQty : 1;

        // Measured before drawing so the name is fitted around it, same as
        // ItemRow - "Add x999999" is the widest this column ever gets.
        char value[32];
        snprintf(value, sizeof(value), "x%lld", qty);

        const float s = g_scale;
        float avail = g_width - 28.0f * s;
        if (icon) avail -= 38.0f * s;
        avail -= g_fontBody->CalcTextSizeA(g_fontBody->FontSize, FLT_MAX, 0.0f, value).x;
        avail -= 28.0f * s;

        char fitted[160];
        FitLabel(fitted, sizeof(fitted), label, avail);

        RowResult r = RowBase(fitted, desc, locked ? RowKind::None : RowKind::ItemAdd, icon);

        long long added = 0;
        if (!locked)
        {
            const long long step = g_nav.adjustBoost ? 10 : 1;
            long long v = qty;
            if (r.left)  v -= step;
            if (r.right) v += step;
            if (v < 1)           v = 1;           // adding zero is not a thing
            if (v > kItemQtyMax) v = kItemQtyMax;
            if (v != qty)
            {
                s_addKey = key;                   // this row owns the amount now
                s_addQty = v;
                qty      = v;
            }

            if (r.activated)
            {
                added = qty;
                // Back to 1 so the next add is not silently the last amount.
                s_addKey = 0;
                s_addQty = 1;
            }
        }

        // Moving away resets the amount - see the header. Done after the input
        // above so the frame that steps it is not immediately undone.
        if (!r.selected && key != 0 && key == s_addKey)
        {
            s_addKey = 0;
            s_addQty = 1;
        }

        // Draw what this frame's input produced, not what we were passed in:
        // a held arrow must not visibly trail the press.
        snprintf(value, sizeof(value), "x%lld", added ? added : qty);
        DrawRowValue(r, value, !locked, locked ? theme::TextDim : 0);
        return added;
    }

    // --- Keybind rows (see widgets.h) -----------------------------------------
    // Column geometry shared by BindRow and BindHeader so the "Keyboard" /
    // "Controller" titles sit exactly over the two row cells. Two equal cells,
    // right-aligned in the row; the action label lives in the space to their
    // left.
    static void BindCellLayout(float* keyX, float* padX, float* cellW)
    {
        const float s    = g_scale;
        const float w    = 150.0f * s;
        const float gap  = 12.0f * s;
        const float rowR = g_x + g_width;
        const float padL = rowR - 14.0f * s - w;
        *padX  = padL;
        *keyX  = padL - gap - w;
        *cellW = w;
    }

    void BindHeader()
    {
        ImDrawList* dl = DL();
        const float s  = g_scale;
        const float h  = 20.0f * s;

        float keyX, padX, w;
        BindCellLayout(&keyX, &padX, &w);

        // Paint the strip's background just like a real row (RowBase does this on
        // kChBg) - without it the header eats vertical space but leaves the game
        // world showing through behind the "Keyboard" / "Controller" titles.
        dl->ChannelsSetCurrent(kChBg);
        dl->AddRectFilled(ImVec2(g_x, g_y), ImVec2(g_x + g_width, g_y + h), theme::RowBg);
        dl->ChannelsSetCurrent(kChFg);

        const float fz = g_fontBody->FontSize * 0.72f;
        const float ty = g_y + (h - fz) * 0.5f;
        auto title = [&](const char* t, float x)
        {
            const float tw = g_fontBody->CalcTextSizeA(fz, FLT_MAX, 0.0f, t).x;
            dl->AddText(g_fontBody, fz, ImVec2(x + (w - tw) * 0.5f, ty), theme::TextDim, t);
        };
        title("Keyboard",   keyX);
        title("Controller", padX);

        // The header eats vertical space before the first row, but End()
        // positions the selection highlight and scrollbar from g_listTop using
        // a fixed per-row stride - they know nothing about a header. Re-anchor
        // g_listTop to just below it so the highlight stays lined up with the
        // rows (the header itself is never a selectable row).
        g_y      += h;
        g_listTop = g_y;
    }

    BindEdit BindRow(const char* label, int* cursor, const char* keyText,
                     const char* padText, int capturingCol, const char* desc)
    {
        RowResult r = RowBase(label, desc, RowKind::Bind);

        int  dummy = 0;
        int& cur   = cursor ? *cursor : dummy;
        if (cur < 0) cur = 0;
        if (cur > 1) cur = 1;

        // While a column here is listening, capture has frozen nav (g_nav is all
        // zero), so none of the input branches below fire - the row simply
        // redraws with the listening column pulsing until the caller commits it.
        if (r.left  && cur > 0) --cur;
        if (r.right && cur < 1) ++cur;

        float keyX, padX, w;
        BindCellLayout(&keyX, &padX, &w);

        // Mouse: the cell under the pointer takes focus, and a click on it is the
        // rebind (mirrors SwatchRow).
        if (r.drawn)
        {
            ImGuiIO& io = ImGui::GetIO();
            const bool over = io.MousePos.y >= r.mn.y && io.MousePos.y < r.mx.y;
            if (over && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
                         ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
            {
                if      (io.MousePos.x >= keyX && io.MousePos.x < keyX + w) cur = 0;
                else if (io.MousePos.x >= padX && io.MousePos.x < padX + w) cur = 1;
            }
        }

        BindEdit result = BindEdit::None;
        if (r.activated)
            result = (cur == 0) ? BindEdit::RebindKey : BindEdit::RebindPad;
        if (r.clear)
        {
            g_nav.clear = false;
            result = (cur == 0) ? BindEdit::ResetKey : BindEdit::ResetPad;
        }

        if (r.drawn)
        {
            ImDrawList* dl = DL();
            const float s  = g_scale;
            const float cy = (r.mn.y + r.mx.y) * 0.5f;
            const float fz = g_fontBody->FontSize * 0.92f;

            auto cell = [&](float x, const char* text, bool focused, bool listening)
            {
                const ImVec2 mn(x, r.mn.y + 5.0f * s);
                const ImVec2 mx(x + w, r.mx.y - 5.0f * s);

                // Listening pulses the accent; a focused cell (selected row, this
                // column) gets a solid highlight; the rest keep a faint outline
                // so both columns always read as distinct, fillable slots.
                if (listening)
                {
                    const float a = 0.45f + 0.35f * static_cast<float>((GetTickCount64() / 400) & 1);
                    dl->AddRectFilled(mn, mx, WithAlpha(theme::Accent, 0.30f), 4.0f * s);
                    dl->AddRect(mn, mx, WithAlpha(theme::Accent, a), 4.0f * s, 0, 1.5f * s);
                }
                else if (focused)
                {
                    dl->AddRectFilled(mn, mx, WithAlpha(theme::TextBright, 0.10f), 4.0f * s);
                    dl->AddRect(mn, mx, WithAlpha(theme::TextBright, 0.55f), 4.0f * s, 0, 1.0f * s);
                }
                else
                {
                    dl->AddRect(mn, mx, WithAlpha(theme::TextDim, 0.30f), 4.0f * s, 0, 1.0f * s);
                }

                const ImU32 col = (listening || focused) ? theme::TextBright : theme::TextDim;
                const float tw  = g_fontBody->CalcTextSizeA(fz, FLT_MAX, 0.0f, text).x;
                float tx = x + (w - tw) * 0.5f;
                if (tx < x + 6.0f * s) tx = x + 6.0f * s; // keep a long bind off the edge
                dl->AddText(g_fontBody, fz, ImVec2(tx, cy - fz * 0.5f), col, text);
            };

            cell(keyX, keyText, r.selected && cur == 0, capturingCol == 0);
            cell(padX, padText, r.selected && cur == 1, capturingCol == 1);
        }

        return result;
    }
}

