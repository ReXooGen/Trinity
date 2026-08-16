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

$zipName = "Trinity-v1.1.1-vTweak.zip"
$zipPath = Join-Path $releaseDir $zipName
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path "$pkgDir\*" -DestinationPath $zipPath -Force
Write-Host "Created Release ZIP: $zipPath"
Write-Host "Built: $($asi.FullName)"
