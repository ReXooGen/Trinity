#pragma once
#include <Windows.h>

namespace trinity::input
{
    // Subclasses the game window so ImGui receives raw input and, while the
    // menu is open, the game does not.
    void Init(HWND hwnd);
    void Shutdown();
}
