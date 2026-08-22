#pragma once
#include <Windows.h>

namespace trinity
{
    // Top-level coordinator: brings up MinHook + the render/input hooks, and
    // tears them back down on unload.
    class Mod
    {
    public:
        static Mod& Get()
        {
            static Mod instance;
            return instance;
        }

        void Initialize(HMODULE module);
        void Shutdown();

        HMODULE Module() const { return m_module; }

    private:
        Mod() = default;

        HMODULE m_module = nullptr;
        bool    m_initialized = false;
    };
}
