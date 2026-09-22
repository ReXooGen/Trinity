#pragma once

namespace trinity::game
{
    // Reversible controller for the worker level/ability check patch (PE 2850
    // and PE 2944 share the same grade-selector branch bytes) supplied as an
    // Auto Assembler script. It deliberately exposes no worker object layout,
    // enumeration, or direct data editor API.
    class Worker
    {
    public:
        static bool Install();
        static void Remove();
        static bool Ready();
        static bool Enabled();
        static bool SetEnabled(bool enabled);
    };
}
