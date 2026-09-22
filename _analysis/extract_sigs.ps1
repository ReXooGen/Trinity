# extract_sigs.ps1 - extract every kSig_* byte-pattern constant from offsets.h
# and emit a patterns JSON for the wildcard-capable scanner.
#
# Handles C++ adjacent string-literal concatenation across lines, which several
# signatures use. Emits {name, pattern} pairs.
param(
    [string]$Offsets = "C:\Users\mul0\Documents\GitHub\Trinity\src\game\offsets.h",
    [string]$Out = "C:\Users\mul0\Documents\GitHub\Trinity\_analysis\patterns_all_sigs.json"
)

$ErrorActionPreference = 'Stop'
$text = Get-Content $Offsets -Raw

# kSig_<name> = "..." "..." ... ;
$rx = [regex]'(?s)kSig_([A-Za-z0-9_]+)\s*=\s*((?:\s*"[^"]*"\s*)+)\s*;'
$entries = @()
foreach ($m in $rx.Matches($text)) {
    $name = 'kSig_' + $m.Groups[1].Value
    $pattern = ([regex]::Matches($m.Groups[2].Value, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value }) -join ''
    $pattern = ($pattern -replace '\s+', ' ').Trim()
    if ($pattern -eq '') { $entries += [pscustomobject]@{ name = $name; pattern = ''; note = 'EMPTY PATTERN - never resolves' } ; continue }
    $entries += [pscustomobject]@{ name = $name; pattern = $pattern }
}

$entries | ConvertTo-Json -Depth 5 | Set-Content -Path $Out
Write-Output "Extracted $($entries.Count) kSig_* constants from $Offsets"
Write-Output "Wrote $Out"
