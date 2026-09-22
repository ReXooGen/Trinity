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

$modPath = Join-Path $PSScriptRoot '..\src\core\mod.cpp'
$settingsPath = Join-Path $PSScriptRoot '..\src\core\settings.cpp'
$menuPath = Join-Path $PSScriptRoot '..\src\gui\menu.cpp'
$playerPath = Join-Path $PSScriptRoot '..\src\game\player.cpp'
$teleportPath = Join-Path $PSScriptRoot '..\src\game\teleport.cpp'
$inventoryPath = Join-Path $PSScriptRoot '..\src\game\inventory.cpp'
$worldPath = Join-Path $PSScriptRoot '..\src\game\world.cpp'
$equipmentPath = Join-Path $PSScriptRoot '..\src\game\equipment.cpp'
$friendlyPath = Join-Path $PSScriptRoot '..\src\game\friendly.cpp'
$workerPath = Join-Path $PSScriptRoot '..\src\game\worker.cpp'
$dx12Path = Join-Path $PSScriptRoot '..\src\hooks\dx12_hook.cpp'

$mod = Get-Content -LiteralPath $modPath -Raw
$settings = Get-Content -LiteralPath $settingsPath -Raw
$menu = Get-Content -LiteralPath $menuPath -Raw
$player = Get-Content -LiteralPath $playerPath -Raw
$teleport = Get-Content -LiteralPath $teleportPath -Raw
$inventory = Get-Content -LiteralPath $inventoryPath -Raw
$world = Get-Content -LiteralPath $worldPath -Raw
$equipment = Get-Content -LiteralPath $equipmentPath -Raw
$friendly = Get-Content -LiteralPath $friendlyPath -Raw
$worker = Get-Content -LiteralPath $workerPath -Raw
$dx12 = Get-Content -LiteralPath $dx12Path -Raw

if ($settings -notmatch 'CrashDiagnostics::PublishFeatureSnapshot\s*\(') {
    $failures.Add('Settings::Load must publish a feature snapshot.')
}

if ($menu -notmatch 'CrashDiagnostics::PublishFeatureSnapshot\s*\(') {
    $failures.Add('Menu rendering must publish a feature snapshot.')
}

if ($mod -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"mod\.initialize\.begin"') {
    $failures.Add('Mod::Initialize must record mod.initialize.begin.')
}

if ($mod -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"mod\.initialize\.complete"') {
    $failures.Add('Mod::Initialize must record mod.initialize.complete.')
}

if ($player -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.player"') {
    $failures.Add('Player installer must record hook.player.')
}

if ($teleport -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.teleport"') {
    $failures.Add('Teleport installer must record hook.teleport.')
}

if ($inventory -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.inventory"') {
    $failures.Add('Inventory installer must record hook.inventory.')
}

if ($world -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.world"') {
    $failures.Add('World installer must record hook.world.')
}

if ($equipment -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.equipment"') {
    $failures.Add('Equipment installer must record hook.equipment.')
}

if ($friendly -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.friendly"') {
    $failures.Add('Friendly installer must record hook.friendly.')
}

if ($worker -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.worker"') {
    $failures.Add('Worker installer must record hook.worker.')
}

if ($dx12 -notmatch 'CrashDiagnostics::Record\s*\(\s*[^,]+,\s*"hook\.dx12"') {
    $failures.Add('DX12 installer must record hook.dx12.')
}

if ($inventory -notmatch 'CrashDiagnostics::Record\s*\(\s*(?:(?:trinity::)?core::diag::|diag::)?BreadcrumbKind::PatchState\s*,\s*"patch\.inventory\.pickup-capacity"') {
    $failures.Add('Inventory pickup capacity patch must record PatchState breadcrumbs.')
}

if ($worker -notmatch 'CrashDiagnostics::Record\s*\(\s*(?:(?:trinity::)?core::diag::|diag::)?BreadcrumbKind::PatchState\s*,\s*"patch\.worker\.job-time"') {
    $failures.Add('Worker job-time patch must record PatchState breadcrumbs.')
}

$safeMemPath = Join-Path $PSScriptRoot '..\src\mem\safe_memory.h'
$safeMem = Get-Content -LiteralPath $safeMemPath -Raw

if ($safeMem -notmatch 'CrashDiagnostics::NoteMemoryWrite\s*\(') {
    $failures.Add('Safe memory write helpers must call CrashDiagnostics::NoteMemoryWrite.')
}

$scopes = @(
    @{ File = $player; Label = 'player.stat-pin'; Name = 'player.stat-pin' },
    @{ File = $teleport; Label = 'player.movement'; Name = 'player.movement' },
    @{ File = $teleport; Label = 'teleport.position'; Name = 'teleport.position' },
    @{ File = $teleport; Label = 'teleport.flight'; Name = 'teleport.flight' },
    @{ File = $teleport; Label = 'teleport.noclip'; Name = 'teleport.noclip' },
    @{ File = $inventory; Label = 'inventory.stack-size'; Name = 'inventory.stack-size' },
    @{ File = $inventory; Label = 'inventory.slot-size'; Name = 'inventory.slot-size' },
    @{ File = $inventory; Label = 'inventory.add-item'; Name = 'inventory.add-item' },
    @{ File = $inventory; Label = 'inventory.quantity'; Name = 'inventory.quantity' },
    @{ File = $world; Label = 'world.time'; Name = 'world.time' },
    @{ File = $world; Label = 'world.weather'; Name = 'world.weather' },
    @{ File = $equipment; Label = 'equipment.modify'; Name = 'equipment.modify' },
    @{ File = $friendly; Label = 'friendly.trust'; Name = 'friendly.trust' },
    @{ File = $worker; Label = 'worker.job-time'; Name = 'worker.job-time' }
)

foreach ($s in $scopes) {
    $escaped = [regex]::Escape($s.Label)
    if ($s.File -notmatch "MutationScope(?:\s+[A-Za-z0-9_]+)?\s*\(\s*`"$escaped`"\s*\)") {
        $failures.Add("MutationScope for $($s.Name) must be present.")
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'crash reporting contract passed'
