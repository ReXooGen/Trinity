# aob_scan.ps1 - reproduce Trinity's mem::FindPattern scan against the on-disk EXE.
#
# Trinity (src/mem/scanner.cpp) walks the loaded module's PE sections, skipping a
# section named exactly ".debug" that lacks IMAGE_SCN_MEM_EXECUTE, and scans
# [VA, VA+VirtualSize). This script does the same against the file image so the
# uniqueness result matches what the deployed ASI will see.
#
# Usage: pwsh -NoProfile -File aob_scan.ps1 -Exe <path> -PatternsFile <json>
#   json = [ { "name": "...", "pattern": "48 89 5C 24 18 ..." }, ... ]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$PatternsFile
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
public static class AobScan {
    public static List<long> FindAll(byte[] data, long start, long len, byte[] pat) {
        var res = new List<long>();
        if (pat.Length == 0 || len < pat.Length) return res;
        long end = start + len - pat.Length;
        byte first = pat[0];
        for (long i = start; i <= end; i++) {
            if (data[i] != first) continue;
            int j = 1;
            while (j < pat.Length && data[i + j] == pat[j]) j++;
            if (j == pat.Length) res.Add(i - start);
        }
        return res;
    }
}
'@

function Parse-Pattern([string]$p) {
    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($tok in ($p -split '\s+')) {
        if ($tok -eq '' -or $tok -eq '?' -or $tok -eq '??') { throw "wildcards not supported in this replica: '$tok'" }
        $bytes.Add([Convert]::ToByte($tok, 16))
    }
    return $bytes.ToArray()
}

$fs = [System.IO.File]::Open($Exe, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
$br = New-Object System.IO.BinaryReader($fs)
$fs.Position = 0x3C; $peOff = $br.ReadInt32()
$fs.Position = $peOff + 4
$machine = $br.ReadUInt16(); $numSec = $br.ReadUInt16()
$fs.Position = $peOff + 24 + 24; $imageBase = [uint64]$br.ReadUInt64()
$fs.Position = $peOff + 24 + 56; $imageSize = $br.ReadUInt32()

$sections = @()
$fs.Position = $peOff + 24 + 240
for ($i = 0; $i -lt $numSec; $i++) {
    $nameBytes = $br.ReadBytes(8)
    $name = ([System.Text.Encoding]::ASCII.GetString($nameBytes)).Trim([char]0)
    $vsize = $br.ReadUInt32(); $vaddr = $br.ReadUInt32(); $rsize = $br.ReadUInt32(); $raddr = $br.ReadUInt32()
    $chars = $br.ReadUInt32()
    $br.ReadBytes(12) | Out-Null
    $sections += [pscustomobject]@{ Name = $name; VA = $vaddr; VSize = $vsize; RawPtr = $raddr; RawSize = $rsize; Chars = $chars }
}
$br.Close(); $fs.Close()

# Trinity's ShouldScanSection(): skip only a section literally named ".debug"
# that is not executable.
$MEM_EXECUTE = 0x20000000
$scanned = $sections | Where-Object { -not ($_.Name -eq '.debug' -and (($_.Chars -band $MEM_EXECUTE) -eq 0)) }

Write-Output "EXE          : $Exe"
Write-Output ("ImageBase    : 0x{0:X}" -f $imageBase)
Write-Output ("ImageSize    : 0x{0:X}" -f $imageSize)
Write-Output ("Sections     : {0} total, {1} scanned by Trinity's filter" -f $sections.Count, $scanned.Count)
Write-Output ""

$data = [System.IO.File]::ReadAllBytes($Exe)
Write-Output ("Loaded {0} bytes of file image." -f $data.Length)
Write-Output ""

$patterns = Get-Content $PatternsFile -Raw | ConvertFrom-Json
foreach ($entry in $patterns) {
    if ($entry.text) { $pat = [System.Text.Encoding]::ASCII.GetBytes($entry.text) }
    else             { $pat = Parse-Pattern $entry.pattern }
    $hits = @()
    foreach ($s in $scanned) {
        # Runtime image is zero-filled past RawSize, so a non-zero pattern can
        # only match within the raw bytes; clamp to what the file actually holds.
        $len = [Math]::Min([uint32]$s.VSize, [uint32]$s.RawSize)
        if ($len -le 0) { continue }
        $found = [AobScan]::FindAll($data, [long]$s.RawPtr, [long]$len, $pat)
        foreach ($off in $found) {
            $va = $imageBase + [uint64]$s.VA + [uint64]$off
            $hits += [pscustomobject]@{ VA = $va; Section = $s.Name }
        }
    }
    Write-Output "=== $($entry.name)  [$($pat.Length) bytes]"
    if ($entry.text) { Write-Output "    text    : $($entry.text)" }
    else             { Write-Output "    pattern : $($entry.pattern)" }
    Write-Output "    matches : $($hits.Count)"
    foreach ($h in $hits) {
        Write-Output ("      VA 0x{0:X}   RVA 0x{1:X}   section {2}" -f $h.VA, ($h.VA - $imageBase), $h.Section)
    }
    Write-Output ""
}
