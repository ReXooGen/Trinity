# aob_scan2.ps1 - wildcard-capable replica of Trinity's mem::FindPattern scan.
#
# aob_scan.ps1 rejects wildcards; several Trinity signatures (e.g. kSig_MoveUpdate
# = "48 8B C4 4C 89 48 ? 48 89 50 ? 55 41 56") use them. This variant accepts
# '?' / '??' as a single-byte wildcard and applies the same section filter:
# skip only a section named exactly ".debug" lacking IMAGE_SCN_MEM_EXECUTE,
# scanning [VA, VA+VirtualSize) clamped to the raw bytes present in the file.
#
# Usage: pwsh -NoProfile -File aob_scan2.ps1 -Exe <path> -PatternsFile <json>
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$PatternsFile,
    [int]$MaxHits = 200
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
public static class AobScan2 {
    public static List<long> FindAll(byte[] data, long start, long len, int[] pat, bool[] wild) {
        var res = new List<long>();
        if (pat.Length == 0 || len < pat.Length) return res;
        long end = start + len - pat.Length;
        for (long i = start; i <= end; i++) {
            if (!wild[0] && data[i] != (byte)pat[0]) continue;
            int j = 1;
            while (j < pat.Length) {
                if (!wild[j] && data[i + j] != (byte)pat[j]) break;
                j++;
            }
            if (j == pat.Length) res.Add(i - start);
        }
        return res;
    }

    // Wildcard-aware uniqueness check used to prove a locator is not ambiguous.
    public static int CountAll(byte[] data, long start, long len, int[] pat, bool[] wild) {
        return FindAll(data, start, len, pat, wild).Count;
    }
}
'@

function Parse-Pattern([string]$p) {
    $vals = New-Object System.Collections.Generic.List[int]
    $wilds = New-Object System.Collections.Generic.List[bool]
    foreach ($tok in ($p -split '\s+')) {
        if ($tok -eq '') { continue }
        if ($tok -eq '?' -or $tok -eq '??' -or $tok -eq '**') { $vals.Add(0); $wilds.Add($true) }
        else { $vals.Add([Convert]::ToInt32($tok, 16)); $wilds.Add($false) }
    }
    return @{ vals = $vals.ToArray(); wilds = $wilds.ToArray() }
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

$MEM_EXECUTE = 0x20000000
$scanned = $sections | Where-Object { -not ($_.Name -eq '.debug' -and (($_.Chars -band $MEM_EXECUTE) -eq 0)) }

Write-Output "EXE          : $Exe"
Write-Output ("ImageBase    : 0x{0:X}" -f $imageBase)
Write-Output ("Sections     : {0} total, {1} scanned" -f $sections.Count, $scanned.Count)
Write-Output ""

$data = [System.IO.File]::ReadAllBytes($Exe)
$patterns = Get-Content $PatternsFile -Raw | ConvertFrom-Json
foreach ($entry in $patterns) {
    if ($entry.text) { $pat = [System.Text.Encoding]::ASCII.GetBytes($entry.text); $wild = New-Object bool[] $pat.Length; $vals = [int[]]($pat | ForEach-Object { [int]$_ }) }
    else { $pp = Parse-Pattern $entry.pattern; $vals = $pp.vals; $wild = $pp.wilds }
    $hits = @()
    foreach ($s in $scanned) {
        $len = [Math]::Min([uint32]$s.VSize, [uint32]$s.RawSize)
        if ($len -le 0) { continue }
        $found = [AobScan2]::FindAll($data, [long]$s.RawPtr, [long]$len, $vals, $wild)
        foreach ($off in $found) {
            $va = $imageBase + [uint64]$s.VA + [uint64]$off
            $hits += [pscustomobject]@{ VA = $va; Section = $s.Name }
        }
    }
    $tokCount = $vals.Length
    Write-Output "=== $($entry.name)  [$tokCount tokens]"
    if ($entry.text) { Write-Output "    text    : $($entry.text)" } else { Write-Output "    pattern : $($entry.pattern)" }
    Write-Output "    matches : $($hits.Count)"
    $shown = 0
    foreach ($h in $hits) {
        if ($shown -ge $MaxHits) { Write-Output "      ... ($($hits.Count - $MaxHits) more)"; break }
        Write-Output ("      VA 0x{0:X}   RVA 0x{1:X}   section {2}" -f $h.VA, ($h.VA - $imageBase), $h.Section)
        $shown++
    }
    Write-Output ""
}
