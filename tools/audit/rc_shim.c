/*
 * Trinity PE 2949 audit - local build workaround, NOT part of the shipped mod.
 *
 * Problem
 * -------
 * Visual Studio 18 Insiders shipped a newer MSVC toolset (the compiler now
 * reports 19.51.36257 in the directory still named 14.51.36231). Its link.exe
 * passes the literal argument `-ologo` to `rc.exe` while merging a manifest
 * into the resource section, and every Windows SDK resource compiler we tested
 * (10.0.22621.0, 10.0.26100.0, 10.0.28000.0) rejects it:
 *
 *     fatal error RC1106: invalid option: -ologo
 *     LINK : fatal error LNK1327: failure during running rc.exe
 *
 * Copying the exact same args to the same rc.exe by hand succeeds, so the
 * defect is the emitted switch, not the resource compiler.
 *
 * Fix
 * ---
 * Registered as `CMAKE_RC_COMPILER` and also placed first on PATH (link.exe
 * resolves `rc.exe` from PATH). It rebuilds the command line for the real SDK
 * resource compiler, dropping only the malformed `-ologo` switch. Suppressing
 * the banner is cosmetic, so the produced .res bytes are identical to a
 * correct invocation.
 *
 * Build (directly, bypassing the broken CMake wrapper):
 *     cl /nologo /O2 /c rc_shim.c
 *     link /nologo /MANIFEST:NO rc_shim.obj /out:rc.exe
 *
 * Set TRINITY_REAL_RC to override the real resource compiler path, and
 * TRINITY_RC_SHIM_LOG to log every forwarded command line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kDefaultRealRc =
    "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.28000.0\\x64\\rc.exe";

static int IsMalformedLogoSwitch(const char* arg)
{
    if (!arg || (arg[0] != '-' && arg[0] != '/'))
        return 0;
    /* Only `-ologo` is dropped; `-nologo` is valid and must survive. */
    return strcmp(arg + 1, "ologo") == 0;
}

int main(int argc, char** argv)
{
    const char* real = getenv("TRINITY_REAL_RC");
    if (!real || !real[0])
        real = kDefaultRealRc;

    char cmd[32768];
    /* `system()` runs through cmd.exe, which strips the outermost quote pair,
     * so the command line is wrapped in one extra pair to survive intact even
     * though the resource-compiler path contains spaces. */
    int written = snprintf(cmd, sizeof(cmd), "\"\"%s\"", real);
    if (written < 0 || written >= (int)sizeof(cmd))
        return 1;

    for (int i = 1; i < argc; ++i)
    {
        if (IsMalformedLogoSwitch(argv[i]))
            continue;

        const int room = (int)sizeof(cmd) - written;
        const int added = snprintf(cmd + written, (size_t)room, " \"%s\"", argv[i]);
        if (added < 0 || added >= room)
            return 1;
        written += added;
    }

    if (written + 1 >= (int)sizeof(cmd))
        return 1;
    cmd[written++] = '"';
    cmd[written] = '\0';

    const char* log = getenv("TRINITY_RC_SHIM_LOG");
    if (log && log[0])
    {
        FILE* f = fopen(log, "a");
        if (f)
        {
            fprintf(f, "%s\n", cmd);
            fclose(f);
        }
    }

    const int status = system(cmd);
    return status;
}
