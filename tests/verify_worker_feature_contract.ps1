$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$state = Get-Content -Raw -LiteralPath (Join-Path $root 'src/core/state.h')
$settings = Get-Content -Raw -LiteralPath (Join-Path $root 'src/core/settings.cpp')
$menu = Get-Content -Raw -LiteralPath (Join-Path $root 'src/gui/menu.cpp')
$example = Get-Content -Raw -LiteralPath (Join-Path $root 'config/Trinity.ini.example')
$dye = Get-Content -Raw -LiteralPath (Join-Path $root 'src/game/dye.cpp')
$dyeHeader = Get-Content -Raw -LiteralPath (Join-Path $root 'src/game/dye.h')

function Require([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw $message }
}

function Forbid([string]$text, [string]$pattern, [string]$message) {
    if ($text -match $pattern) { throw $message }
}

Require $state 'workerMaxLevelAndSkills' 'State must expose the new worker toggle.'
Require $settings 'workerMaxLevelAndSkills' 'Settings must persist the new worker toggle.'
Require $example '(?m)^workerMaxLevelAndSkills=' 'The example INI must document the new worker toggle.'

$legacyKeyPatterns = @(
    '(?m)^\s*workerMaxLevelHook\s*[=]',
    '(?m)^\s*workerAutoApply\s*[=]',
    '(?m)^\s*workerEdit(?:Level|Exp)\s*[=]',
    '(?m)^\s*workerTargetSkillCount\s*[=]',
    '(?m)^\s*workerSkillId\d+\s*[=]'
)
foreach ($pattern in $legacyKeyPatterns) {
    Forbid $settings $pattern "Settings still contains legacy worker key pattern: $pattern"
    Forbid $example $pattern "Example INI still contains legacy worker key pattern: $pattern"
}

Require $menu 'Max Worker Level & Skills' 'Player menu must expose the new worker toggle.'
Forbid $menu 'RenderWorkers|RenderMountOptions|mount_options|world_workers' 'Legacy worker or mount menu route remains.'

Forbid $dyeHeader 'SetTargetMode|GetTargetMode|SetActiveMount|GetActiveMount' 'The Dye API must not expose removed Mount/Horse editor selectors.'
Forbid $dye 'HorseSlotType|GetHorseSlotType|MountSlotName|FindMountComp|g_mountComp|SavedMountSlot|s_savedMountSlots|s_targetMode == 1|MOUNT MODE|mount equip|mount tag|tracked mounts' 'The Dye implementation must not retain Mount/Horse editor paths or mount auto-restore.'

Write-Output 'worker feature contract passed'
