# build_and_scan.ps1 - build an AOB pattern set from BN memory dumps and measure
# uniqueness over the whole image with Trinity's own scanner semantics.
#
# Input : out_bytes.txt produced by bnbatch.ps1 + spec_bytes.json
# Output: patterns_auto.json + scan results on stdout
param(
    [string]$BytesFile = "C:\Users\mul0\Documents\GitHub\Trinity\_analysis\out_bytes.txt",
    [string]$PatternsOut = "C:\Users\mul0\Documents\GitHub\Trinity\_analysis\patterns_auto.json",
    [string]$Exe = "E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe",
    [int]$TrimBytes = 48
)

$ErrorActionPreference = 'Stop'

$lines = Get-Content $BytesFile
$entries = @()
$curLabel = $null
foreach ($line in $lines) {
    if ($line -match '^#+\s*\[\d+\]\s*(.+?)\s*\(bn_memory_read\)') { $curLabel = $Matches[1].Trim(); continue }
    if ($curLabel -and $line -match '"hex":"([0-9a-fA-F]+)"') {
        $hex = $Matches[1]
        if ($hex.Length -ge ($TrimBytes * 2)) { $hex = $hex.Substring(0, $TrimBytes * 2) }
        $tokens = @()
        for ($i = 0; $i -lt $hex.Length; $i += 2) { $tokens += $hex.Substring($i, 2).ToUpper() }
        $entries += [pscustomobject]@{ name = $curLabel; pattern = ($tokens -join ' ') }
        $curLabel = $null
    }
}

# Shipping travel locators (documented in docs/binary-ninja/dossiers/travel.md and
# src/game/offsets.h) re-verified here so the whole evidence set is measured in one
# consistent pass.
$entries += [pscustomobject]@{ name = 'kSig_TravelDispatcher_PE2944'; pattern = '48 89 5C 24 18 89 54 24 10 48 89 4C 24 08 55 56 57 41 56 41 57 48 8D AC 24 50 FE FF FF 48 81 EC B0 02 00 00 41 8B F9 45 8B F8 33 DB' }
$entries += [pscustomobject]@{ name = 'kSig_TravelToNode_PE2944'; pattern = '89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54 41 56 41 57 48 81 EC 90 00 00 00 41 8B D8 33 FF' }

$entries | ConvertTo-Json -Depth 5 | Set-Content -Path $PatternsOut
Write-Output "Wrote $($entries.Count) patterns to $PatternsOut"
Write-Output ""

pwsh -NoProfile -File "C:\Users\mul0\Documents\GitHub\Trinity\_analysis\aob_scan.ps1" -Exe $Exe -PatternsFile $PatternsOut
