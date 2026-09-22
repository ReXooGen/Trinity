# extract_inline_aobs.ps1 - find inline byte-pattern string literals in a source file
# (patterns written directly at the call site rather than as a kSig_* constant).
param(
    [string[]]$Files = @(
        "C:\Users\mul0\Documents\GitHub\Trinity\src\game\equipment.cpp",
        "C:\Users\mul0\Documents\GitHub\Trinity\src\game\dye.cpp",
        "C:\Users\mul0\Documents\GitHub\Trinity\src\game\teleport.cpp",
        "C:\Users\mul0\Documents\GitHub\Trinity\src\game\inventory.cpp"
    ),
    [string]$Out = "C:\Users\mul0\Documents\GitHub\Trinity\_analysis\patterns_inline.json"
)
$ErrorActionPreference = 'Stop'
$entries = @()
foreach ($f in $Files) {
    if (-not (Test-Path $f)) { continue }
    $text = Get-Content $f -Raw
    $short = Split-Path $f -Leaf
    # hex byte or ?? tokens, at least 8 tokens, inside a string literal
    $rx = [regex]'"((?:[0-9A-Fa-f?]{2}\s+){7,}[0-9A-Fa-f?]{2})"'
    foreach ($m in $rx.Matches($text)) {
        $pat = ($m.Groups[1].Value -replace '\s+', ' ').Trim()
        # must contain at least 6 real byte tokens to be a plausible AOB
        $real = ($pat -split ' ' | Where-Object { $_ -notmatch '^\?\?$' -and $_ -ne '?' }).Count
        if ($real -lt 6) { continue }
        # record the line number
        $idx = $m.Index
        $line = ($text.Substring(0, $idx) -split "`n").Count
        $entries += [pscustomobject]@{ name = "$short`:$line"; pattern = $pat }
    }
}
# de-duplicate identical patterns, keeping the first label
$seen = @{}
$uniq = @()
foreach ($e in $entries) { if (-not $seen.ContainsKey($e.pattern)) { $seen[$e.pattern] = $true; $uniq += $e } }
$uniq | ConvertTo-Json -Depth 5 | Set-Content -Path $Out
Write-Output "Found $($entries.Count) inline AOB literals ($($uniq.Count) unique) -> $Out"
