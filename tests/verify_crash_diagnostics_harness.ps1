[CmdletBinding()]
param(
    [string]$HarnessPath
)

$ErrorActionPreference = 'Stop'

if (-not $HarnessPath) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\build\Release\TrinityCrashDiagnosticsHarness.exe"),
        (Join-Path $PSScriptRoot "..\build\Debug\TrinityCrashDiagnosticsHarness.exe"),
        (Join-Path $PSScriptRoot "..\build\TrinityCrashDiagnosticsHarness.exe")
    )
    foreach ($cand in $candidates) {
        if (Test-Path -LiteralPath $cand) {
            $HarnessPath = (Resolve-Path $cand).Path
            break
        }
    }
}

if (-not $HarnessPath -or -not (Test-Path -LiteralPath $HarnessPath)) {
    throw "TrinityCrashDiagnosticsHarness.exe not found at '$HarnessPath'"
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("trinity_crash_test_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

try {
    # Phase 1: Handled exception mode produces no crash bundle
    $handledDir = Join-Path $testRoot "handled"
    New-Item -ItemType Directory -Path $handledDir -Force | Out-Null

    $handledProc = Start-Process -FilePath $HarnessPath -ArgumentList @("--handled", $handledDir) -Wait -PassThru -NoNewWindow
    if ($handledProc.ExitCode -ne 0) {
        throw "Handled mode failed with exit code $($handledProc.ExitCode)"
    }
    $handledFiles = Get-ChildItem -LiteralPath $handledDir -Filter "Trinity_Crash_*"
    if ($handledFiles.Count -ne 0) {
        throw "Handled mode created $($handledFiles.Count) unexpected crash files"
    }

    # Phase 2: Fatal crash mode produces matched .txt and .dmp bundle
    $fatalDir = Join-Path $testRoot "fatal"
    New-Item -ItemType Directory -Path $fatalDir -Force | Out-Null

    $fatalProc = Start-Process -FilePath $HarnessPath -ArgumentList @("--fatal", $fatalDir) -Wait -PassThru -NoNewWindow
    if ($fatalProc.ExitCode -eq 0) {
        throw "Fatal child process unexpectedly succeeded with exit code 0"
    }

    $txtFiles = @(Get-ChildItem -LiteralPath $fatalDir -Filter "Trinity_Crash_*.txt")
    $dmpFiles = @(Get-ChildItem -LiteralPath $fatalDir -Filter "Trinity_Crash_*.dmp")

    if ($txtFiles.Count -ne 1) {
        throw "Expected exactly 1 crash report text file, found $($txtFiles.Count)"
    }
    if ($dmpFiles.Count -ne 1) {
        throw "Expected exactly 1 minidump file, found $($dmpFiles.Count)"
    }
    if ($txtFiles[0].BaseName -ne $dmpFiles[0].BaseName) {
        throw "Base stem mismatch: '$($txtFiles[0].BaseName)' vs '$($dmpFiles[0].BaseName)'"
    }

    $reportContent = Get-Content -LiteralPath $txtFiles[0].FullName -Raw
    $requiredSections = @(
        "TRINITY TERMINAL CRASH DIAGNOSTICS",
        "[Session]",
        "[Exception]",
        "[Registers]",
        "[Fault Module]",
        "[Stack]",
        "[Feature Snapshot]",
        "[Hook/Patch Snapshot]",
        "[Breadcrumbs]",
        "[Dump Result]",
        "[Attribution]",
        "harness.pre-crash"
    )

    foreach ($section in $requiredSections) {
        if (-not $reportContent.Contains($section)) {
            throw "Crash report missing required section/marker: '$section'"
        }
    }

    if ($reportContent -notmatch "Success:\s*true") {
        throw "Crash report did not indicate dump success in [Dump Result]"
    }

    $dumpSize = $dmpFiles[0].Length
    Write-Host "Fatal crash dump size: $dumpSize bytes ($([Math]::Round($dumpSize / 1MB, 2)) MB)"
    if ($dumpSize -le 0) {
        throw "Minidump file is empty"
    }
    if ($dumpSize -gt (300 * 1024 * 1024)) {
        throw "Minidump file exceeded 300 MB limit: $dumpSize bytes"
    }

    # Phase 3: Blocked dump still writes diagnostic text report
    $blockDir = Join-Path $testRoot "block"
    New-Item -ItemType Directory -Path $blockDir -Force | Out-Null

    $blockProc = Start-Process -FilePath $HarnessPath -ArgumentList @("--fatal-block-dump", $blockDir) -Wait -PassThru -NoNewWindow
    if ($blockProc.ExitCode -eq 0) {
        throw "Fatal-block-dump child process unexpectedly succeeded with exit code 0"
    }

    $blockTxtFiles = @(Get-ChildItem -LiteralPath $blockDir -Filter "Trinity_Crash_*.txt")
    if ($blockTxtFiles.Count -ne 1) {
        throw "Expected 1 text report in blocked dump test, found $($blockTxtFiles.Count)"
    }
    $blockReport = Get-Content -LiteralPath $blockTxtFiles[0].FullName -Raw
    if (-not $blockReport.Contains("TRINITY TERMINAL CRASH DIAGNOSTICS") -or
        -not $blockReport.Contains("harness.pre-crash")) {
        throw "Blocked dump report is missing header or pre-crash marker"
    }
    if ($blockReport -notmatch "Success:\s*false") {
        throw "Blocked dump report should report Success: false in [Dump Result]"
    }

    # Phase 4: Startup retention keeps exactly 3 newest stems after 5 runs
    $retentionDir = Join-Path $testRoot "retention"
    New-Item -ItemType Directory -Path $retentionDir -Force | Out-Null

    for ($i = 1; $i -le 5; ++$i) {
        $runProc = Start-Process -FilePath $HarnessPath -ArgumentList @("--fatal", $retentionDir) -Wait -PassThru -NoNewWindow
        if ($runProc.ExitCode -eq 0) {
            throw "Retention test run $i unexpectedly exited with code 0"
        }
        Start-Sleep -Milliseconds 1100
    }

    # Run prune-check (clean startup) to ensure pruning runs after the 5th bundle was written
    $pruneProc = Start-Process -FilePath $HarnessPath -ArgumentList @("--prune-check", $retentionDir) -Wait -PassThru -NoNewWindow
    if ($pruneProc.ExitCode -ne 0) {
        throw "Prune check exited with code $($pruneProc.ExitCode)"
    }

    $retainedTxt = @(Get-ChildItem -LiteralPath $retentionDir -Filter "Trinity_Crash_*.txt")
    $retainedDmp = @(Get-ChildItem -LiteralPath $retentionDir -Filter "Trinity_Crash_*.dmp")
    $retainedStems = @($retainedTxt | ForEach-Object { $_.BaseName } | Sort-Object -Unique)

    if ($retainedStems.Count -ne 3) {
        throw "Expected exactly 3 retained stems, but found $($retainedStems.Count): $($retainedStems -join ', ')"
    }
    if ($retainedTxt.Count -ne 3 -or $retainedDmp.Count -ne 3) {
        throw "Expected 3 .txt and 3 .dmp files, found $($retainedTxt.Count) txt and $($retainedDmp.Count) dmp"
    }

    Write-Host "Crash diagnostics harness verification PASSED."
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
