[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$build = Join-Path $source 'build-clean'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found. Install CMake or add it to PATH.'
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio 2022 Build Tools with Desktop development with C++.'
}

$visualStudio = & $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'The Visual Studio C++ x64 compiler is missing. Add the Desktop development with C++ workload.'
}

$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
$ninja = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
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

Write-Host "Built: $($asi.FullName)"
