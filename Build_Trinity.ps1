[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$build = Join-Path $source 'build-clean'
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

$configure = '"{0}" >nul && cmake.exe -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3} -DCMAKE_MAKE_PROGRAM="{4}"' -f
    $vcvars, $source, $build, $Configuration, $ninja
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
if (-not (Test-Path -LiteralPath $pkgDir)) {
    New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null
}

Copy-Item -Path $asi.FullName -Destination (Join-Path $pkgDir 'Trinity.asi') -Force
if (Test-Path (Join-Path $source 'Trinity.ini.example')) {
    Copy-Item -Path (Join-Path $source 'Trinity.ini.example') -Destination (Join-Path $pkgDir 'Trinity.ini.example') -Force
}
if (Test-Path (Join-Path $source 'Trinity_zh.ini')) {
    Copy-Item -Path (Join-Path $source 'Trinity_zh.ini') -Destination (Join-Path $pkgDir 'Trinity_zh.ini') -Force
}
if (Test-Path (Join-Path $source 'Trinity_ko.ini')) {
    Copy-Item -Path (Join-Path $source 'Trinity_ko.ini') -Destination (Join-Path $pkgDir 'Trinity_ko.ini') -Force
}
if (Test-Path (Join-Path $source 'README.md')) {
    Copy-Item -Path (Join-Path $source 'README.md') -Destination (Join-Path $pkgDir 'README.md') -Force
}

# Copy ASI Loader (winmm.dll) and checksum file
$parentDir = Split-Path $source -Parent
$winmmPath = Join-Path $parentDir 'winmm.dll'
if (Test-Path -LiteralPath $winmmPath) {
    Copy-Item -Path $winmmPath -Destination (Join-Path $pkgDir 'winmm.dll') -Force
} elseif (Test-Path -LiteralPath (Join-Path $source 'winmm.dll')) {
    Copy-Item -Path (Join-Path $source 'winmm.dll') -Destination (Join-Path $pkgDir 'winmm.dll') -Force
}

$shaPath = Join-Path $parentDir 'winmm-x64.SHA512'
if (Test-Path -LiteralPath $shaPath) {
    Copy-Item -Path $shaPath -Destination (Join-Path $pkgDir 'winmm-x64.SHA512') -Force
} elseif (Test-Path -LiteralPath (Join-Path $source 'winmm-x64.SHA512')) {
    Copy-Item -Path (Join-Path $source 'winmm-x64.SHA512') -Destination (Join-Path $pkgDir 'winmm-x64.SHA512') -Force
}

$versionHeader = Get-Content (Join-Path $source 'src\core\version.h') -Raw
$versionMatch = [regex]::Match($versionHeader, '#define\s+TRINITY_VERSION\s+"([^"\s]+)')
$versionStr = if ($versionMatch.Success) { $versionMatch.Groups[1].Value } else { "1.2.0" }
$zipName = "Trinity-v$versionStr-vTweak.zip"
$zipPath = Join-Path $releaseDir $zipName
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path "$pkgDir\*" -DestinationPath $zipPath -Force
Write-Host "Created Release ZIP: $zipPath"

# Auto-deploy to parent directory
if (Test-Path -LiteralPath $parentDir) {
    Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $parentDir 'Trinity.asi') -Force
    Copy-Item -Path (Join-Path $pkgDir 'Trinity_zh.ini') -Destination (Join-Path $parentDir 'Trinity_zh.ini') -Force
    Copy-Item -Path (Join-Path $pkgDir 'Trinity_ko.ini') -Destination (Join-Path $parentDir 'Trinity_ko.ini') -Force
    Write-Host "Auto-deployed to: $parentDir"
}

# Auto-deploy to Steam game installation folder
$steamGameDir = "C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64"
if (Test-Path -LiteralPath $steamGameDir) {
    try {
        Copy-Item -Path (Join-Path $pkgDir 'Trinity.asi') -Destination (Join-Path $steamGameDir 'Trinity.asi') -Force -ErrorAction Stop
        Copy-Item -Path (Join-Path $pkgDir 'Trinity_zh.ini') -Destination (Join-Path $steamGameDir 'Trinity_zh.ini') -Force -ErrorAction Stop
        Copy-Item -Path (Join-Path $pkgDir 'Trinity_ko.ini') -Destination (Join-Path $steamGameDir 'Trinity_ko.ini') -Force -ErrorAction Stop
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
    Copy-Item -Path (Join-Path $pkgDir 'Trinity_zh.ini') -Destination (Join-Path $steamModsDir 'Trinity_zh.ini') -Force -ErrorAction SilentlyContinue
    Copy-Item -Path (Join-Path $pkgDir 'Trinity_ko.ini') -Destination (Join-Path $steamModsDir 'Trinity_ko.ini') -Force -ErrorAction SilentlyContinue
    Write-Host "Auto-deployed Trinity.asi and translations to Steam mods folder: $steamModsDir"
} catch {
    Write-Host "Note: Game may be running in mods folder, copy skipped: $_"
}

Write-Host "Built: $($asi.FullName)"
