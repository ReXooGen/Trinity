$ErrorActionPreference = 'Stop'

$dllPath = Join-Path $PSScriptRoot '..\src\dllmain.cpp'
$runtimePath = Join-Path $PSScriptRoot '..\src\core\crash_diagnostics.cpp'
$dll = Get-Content -LiteralPath $dllPath -Raw
$runtime = Get-Content -LiteralPath $runtimePath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if (($dll + $runtime) -match 'AddVectoredExceptionHandler\s*\(') {
    $failures.Add('Crash reporting must not register a vectored first-chance exception handler.')
}

if ($dll -notmatch 'CrashDiagnostics::InstallUnhandledFilter\s*\(\s*module\s*\)') {
    $failures.Add('DllMain must register the diagnostics top-level filter.')
}

if ($dll -notmatch 'CrashDiagnostics::InitializeSession\s*\(\s*module\s*\)') {
    $failures.Add('The worker thread must initialize the diagnostics session outside loader lock.')
}

if ($dll -match 'MiniDumpWriteDump|WriteCrashReport|LONG\s+WINAPI\s+CrashHandler') {
    $failures.Add('DllMain must not own report, dump, or exception-handler implementation.')
}

if ($runtime -notmatch 'SetUnhandledExceptionFilter\s*\(\s*CrashHandler\s*\)') {
    $failures.Add('Crash diagnostics must use the top-level unhandled exception filter.')
}

if ($runtime -notmatch 'InterlockedCompareExchange\s*\(\s*&g_crashHandling') {
    $failures.Add('The terminal crash path must reject recursive handler entry.')
}

if ($runtime -notmatch 'kDiagnosticDumpType') {
    $failures.Add('The terminal writer must use the bounded diagnostic dump policy.')
}

if ($runtime -notmatch 'g_previousCrashHandler\s*\(\s*exceptionPointers\s*\)') {
    $failures.Add('The terminal writer must chain the previously installed filter.')
}

if (($dll + $runtime) -match 'MiniDumpWithFullMemory(?!Info)|MiniDumpWithPrivateReadWriteMemory') {
    $failures.Add('Full/private-memory dump flags are forbidden.')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'crash reporting contract passed'
