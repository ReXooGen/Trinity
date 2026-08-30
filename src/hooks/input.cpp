#include "input.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "../core/state.h"

// Declared in imgui_impl_win32.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace trinity::input
{
    static WNDPROC g_originalWndProc = nullptr;
    static HWND    g_hwnd = nullptr;

    // The only keyboard keys the menu consumes. While the menu is open we feed
    // these to ImGui and swallow their press from the game; every other key -
    // WASD movement and the rest - is left untouched so the player can still
    // move around with the menu up.
    static bool IsMenuKey(WPARAM vk)
    {
        switch (vk)
        {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_RETURN: // Enter (keypad Enter also arrives as VK_RETURN)
        case VK_BACK:   // Backspace
        case VK_ESCAPE: // closes the menu - must not also reach the game's pause
        case VK_PRIOR: case VK_NEXT: // PageUp / PageDown - jump long lists
        case VK_HOME:  case VK_END:  // first / last row
        case VK_TAB:                 // next section
        case VK_DELETE:              // reset value / clear search
        case 'Q': case 'E':          // previous / next section
            return true;
        default:
            return false;
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        __try
        {
            // NOTE: the INSERT / LB+D-Pad Down toggle is polled from the render loop (see
            // hkPresent -> ui::PollMenuToggle), not handled here. Relying on the
            // game to deliver WM_KEYUP to this subclass proved unreliable.
            if (State::Get().menuOpen)
            {
                // While a search row is capturing text - or a SYSTEM-tab row is
                // listening for a new key bind - EVERY key belongs to the menu:
                // typing "harbor" (or pressing the key you want to bind) must not
                // walk the player around.
                const bool typing = State::Get().textCapture || State::Get().rebindCapture;

                switch (msg)
                {
                case WM_KEYDOWN: case WM_SYSKEYDOWN:
                case WM_KEYUP:   case WM_SYSKEYUP:
                    if (typing || IsMenuKey(wParam))
                    {
                        // Give the menu its navigation key...
                        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
                        // ...and swallow the PRESS from the game. Releases still
                        // fall through so a menu key held across open/close never
                        // sticks - the classic "walks forward forever" bug.
                        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
                            return TRUE;
                    }
                    break; // non-menu keys fall through to the game untouched

                case WM_CHAR:
                    // Text capture gets every character; otherwise only Enter /
                    // Backspace produce a WM_CHAR worth hiding (we swallowed
                    // their WM_KEYDOWN above).
                    if (typing)
                    {
                        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
                        return TRUE;
                    }
                    if (wParam == '\r' || wParam == '\b' || wParam == '\t' ||
                        wParam == 'q' || wParam == 'e' || wParam == 'Q' || wParam == 'E')
                        return TRUE;
                    break;

                // Mouse is deliberately neither forwarded to ImGui nor blocked, so
                // the player keeps full mouse-look and no ImGui cursor appears.
                default:
                    break;
                }
            }

            if (g_originalWndProc)
                return CallWindowProc(g_originalWndProc, hwnd, msg, wParam, lParam);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void Init(HWND hwnd)
    {
        if (g_originalWndProc)
            return;

        g_hwnd = hwnd;
        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
    }

    void Shutdown()
    {
        if (g_originalWndProc && g_hwnd)
        {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(g_originalWndProc));
            g_originalWndProc = nullptr;
            g_hwnd = nullptr;
        }
    }
}
