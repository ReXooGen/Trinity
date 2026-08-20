[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$WithDLC
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$build = if ($WithDLC) { Join-Path $source 'build-dlc' } else { Join-Path $source 'build-clean' }
$dlcFlag = if ($WithDLC) { "-DENABLE_EXTENDED_HOOKS=ON" } else { "-DENABLE_EXTENDED_HOOKS=OFF" }
$variantTag = if ($WithDLC) { "-DLC" } else { "" }
$subFolder = if ($WithDLC) { "GitHub-DLC" } else { "NexusMods" }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio Build Tools with Desktop development with C++.'
}

$visualStudio = & $vswhere -latest -version '[17.0,20.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    $visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $visualStudio) {
    throw 'The Visual Studio C++ x64 compiler is missing. Add the Desktop development with C++ workload.'
}

$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
$ninja = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vsCmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (Test-Path -LiteralPath $vsCmake) {
    $env:PATH = "$(Split-Path $vsCmake);$env:PATH"
}

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found. Install CMake or add it to PATH.'
}
if (-not (Test-Path -LiteralPath $vcvars)) { throw "Developer environment not found: $vcvars" }
if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja not found: $ninja" }

$configure = '"{0}" >nul && cmake.exe -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3} -DCMAKE_MAKE_PROGRAM="{4}" {5}' -f
    $vcvars, $source, $build, $Configuration, $ninja, $dlcFlag
$compile = '"{0}" >nul && cmake.exe --build "{1}"' -f $vcvars, $build

& cmd.exe /d /s /c $configure
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }
& cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

$asi = Get-ChildItem -LiteralPath $build -Recurse -Filter '*.asi' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $asi) { throw 'Build completed, but Trinity.asi was not found.' }

$releaseDir = Join-Path $source 'build\Release'
if (-not (Test-Path -LiteralPath $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

$destinationAsi = Join-Path $releaseDir $asi.Name
Copy-Item -Path $asi.FullName -Destination $destinationAsi -Force
Write-Host "Copied to: $destinationAsi"

# Package directory and ZIP creation for release (Nexus / GitHub)
$pkgDir = Join-Path $releaseDir 'package'
if (Test-Path -LiteralPath $pkgDir) {
    Remove-Item -LiteralPath $pkgDir -Recurse -Force
}
New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null

Copy-Item -Path $asi.FullName -Destination (Join-Path $pkgDir 'Trinity.asi') -Force

# Example Configuration
$cfgExample = Join-Path $source 'config\Trinity.ini.example'
if (-not (Test-Path -LiteralPath $cfgExample)) { $cfgExample = Join-Path $source 'Trinity.ini.example' }
if (Test-Path -LiteralPath $cfgExample) {
    Copy-Item -Path $cfgExample -Destination (Join-Path $pkgDir 'Trinity.ini.example') -Force
}

if (Test-Path (Join-Path $source 'README.md')) {
    Copy-Item -Path (Join-Path $source 'README.md') -Destination (Join-Path $pkgDir 'README.md') -Force
}

# Mod Manager Metadata (DMM / Fluffy / Vortex identification)
$modInfoContent = @"
name=Trinity - vTweak
version=$versionStr
description=DirectX 12 Mod Menu for Crimson Desert (Maintenance & vTweak by Lian)
author=Lian (ReXooGen)
category=Utilities
"@
Set-Content -Path (Join-Path $pkgDir 'modinfo.ini') -Value $modInfoContent -Encoding UTF8

$infoJsonContent = @"
{
  "name": "Trinity - vTweak",
  "version": "$versionStr",
  "author": "Lian (ReXooGen)",
  "description": "DirectX 12 Mod Menu for Crimson Desert (Maintenance & vTweak by Lian)",
  "category": "Utilities"
}
"@
Set-Content -Path (Join-Path $pkgDir 'info.json') -Value $infoJsonContent -Encoding UTF8

# Copy ASI Loader (winmm.dll) and checksum file
$parentDir = Split-Path $source -Parent
$winmmPath = Join-Path $source 'loader\winmm.dll'
if (-not (Test-Path -LiteralPath $winmmPath)) { $winmmPath = Join-Path $parentDir 'winmm.dll' }
if (-not (Test-Path -LiteralPath $winmmPath)) { $winmmPath = Join-Path $source 'winmm.dll' }
if (Test-Path -LiteralPath $winmmPath) {
    Copy-Item -Path $winmmPath -Destination (Join-Path $pkgDir 'winmm.dll') -Force
}

$shaPath = Join-Path $source 'loader\winmm-x64.SHA512'
if (-not (Test-Path -LiteralPath $shaPath)) { $shaPath = Join-Path $parentDir 'winmm-x64.SHA512' }
if (-not (Test-Path -LiteralPath $shaPath)) { $shaPath = Join-Path $source 'winmm-x64.SHA512' }
if (Test-Path -LiteralPath $shaPath) {
    Copy-Item -Path $shaPath -Destination (Join-Path $pkgDir 'winmm-x64.SHA512') -Force
}

$versionHeader = Get-Content (Join-Path $source 'src\core\version.h') -Raw
$versionMatch = [regex]::Match($versionHeader, '#define\s+TRINITY_VERSION\s+"([^"\s]+)')
$versionStr = if ($versionMatch.Success) { $versionMatch.Groups[1].Value } else { "1.2.4" }

$commonReleaseDir = Join-Path $parentDir "Release\$versionStr"
if (-not (Test-Path -LiteralPath $commonReleaseDir)) {
    New-Item -ItemType Directory -Path $commonReleaseDir -Force | Out-Null
}

$variantReleaseDir = Join-Path $commonReleaseDir $subFolder
if (-not (Test-Path -LiteralPath $variantReleaseDir)) {
    New-Item -ItemType Directory -Path $variantReleaseDir -Force | Out-Null
}

$zipName = "Trinity-v$versionStr-vTweak (1.18.0.2).zip"
$zipPath = Join-Path $variantReleaseDir $zipName
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path "$pkgDir\*" -DestinationPath $zipPath -Force
Write-Host "Created Release ZIP: $zipPath"

Copy-Item -Path $zipPath -Destination (Join-Path $commonReleaseDir $zipName) -Force
Copy-Item -Path $zipPath -Destination (Join-Path $releaseDir $zipName) -Force

# Copy loose .asi files directly to variant release folder
Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $variantReleaseDir 'Trinity-1.18.02.asi') -Force
Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $variantReleaseDir 'Trinity.asi') -Force

# Setup dedicated Languages folders
$langReleaseDir = Join-Path $commonReleaseDir 'Languages'
if (-not (Test-Path -LiteralPath $langReleaseDir)) {
    New-Item -ItemType Directory -Path $langReleaseDir -Force | Out-Null
}
$variantLangDir = Join-Path $variantReleaseDir 'Languages'
if (-not (Test-Path -LiteralPath $variantLangDir)) {
    New-Item -ItemType Directory -Path $variantLangDir -Force | Out-Null
}

# Auto-deploy to Mod_Files directory
$modFilesDir = Join-Path $parentDir 'Mod_Files'
if (-not (Test-Path -LiteralPath $modFilesDir)) {
    New-Item -ItemType Directory -Path $modFilesDir -Force | Out-Null
}
$modFilesLangDir = Join-Path $modFilesDir 'Languages'
if (-not (Test-Path -LiteralPath $modFilesLangDir)) {
    New-Item -ItemType Directory -Path $modFilesLangDir -Force | Out-Null
}
Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $modFilesDir 'Trinity.asi') -Force

# Auto-deploy to Steam game installation folder
$steamGameDir = "C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64"
if (Test-Path -LiteralPath $steamGameDir) {
    try {
        Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $steamGameDir 'Trinity.asi') -Force -ErrorAction Stop
        Write-Host "Auto-deployed to Steam game folder: $steamGameDir"
    } catch {
        Write-Host "Note: Game may be running in bin64, copy skipped (will apply when game restarts): $_"
    }
}

# Auto-deploy to Steam mods folder (ASI only)
$steamModsDir = "C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\mods"
if (-not (Test-Path -LiteralPath $steamModsDir)) {
    New-Item -ItemType Directory -Path $steamModsDir -Force | Out-Null
}
try {
    Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $steamModsDir 'Trinity.asi') -Force -ErrorAction Stop
    Write-Host "Auto-deployed Trinity.asi to Steam mods folder: $steamModsDir"
} catch {
    Write-Host "Note: Could not copy to mods folder: $_"
}

# Copy and deploy all discovered translation files (*.ini)
$sourceLangDir = Join-Path $source 'languages'
if (-not (Test-Path -LiteralPath $sourceLangDir)) { $sourceLangDir = $source }
Get-ChildItem -Path $sourceLangDir -Filter 'Trinity_*.ini' -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $langReleaseDir $_.Name) -Force
    Copy-Item -Path $_.FullName -Destination (Join-Path $variantLangDir $_.Name) -Force
    Copy-Item -Path $_.FullName -Destination (Join-Path $modFilesDir $_.Name) -Force
    Copy-Item -Path $_.FullName -Destination (Join-Path $modFilesLangDir $_.Name) -Force
    if (Test-Path -LiteralPath $steamGameDir) {
        Copy-Item -Path $_.FullName -Destination (Join-Path $steamGameDir $_.Name) -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $steamModsDir) {
        Copy-Item -Path $_.FullName -Destination (Join-Path $steamModsDir $_.Name) -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "Built: $($asi.FullName)"
