# bnbatch.ps1 - run several Binary Ninja MCP tool calls in one process.
#
# Usage: pwsh -File bnbatch.ps1 -SpecFile calls.json
#   calls.json = [ {"tool":"bn_function_info","args":{"function":"0x1406550b0"},"label":"travel dispatcher"}, ... ]
param(
    [Parameter(Mandatory = $true)][string]$SpecFile,
    [int]$TimeoutSec = 1800
)

$ErrorActionPreference = 'Stop'
$base = 'http://127.0.0.1:24642/mcp'
$sessFile = Join-Path $env:TEMP 'trinity_bn_mcp_session.txt'

function Send-Mcp([string]$body, [string]$sid) {
    $headers = @{ 'Accept' = 'application/json, text/event-stream' }
    if ($sid) { $headers['Mcp-Session-Id'] = $sid }
    $r = Invoke-WebRequest -Uri $base -Method Post -Body $body -ContentType 'application/json' `
        -Headers $headers -UseBasicParsing -TimeoutSec $TimeoutSec
    return @{ sid = [string]$r.Headers['Mcp-Session-Id']; text = [string]$r.Content }
}

function New-Session {
    $init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"trinity-bn","version":"1.0"}}}'
    $res = Send-Mcp $init ''
    $sid = $res.sid
    if (-not $sid) { throw "no session id: $($res.text)" }
    $sid | Set-Content -Path $sessFile -NoNewline
    try { Send-Mcp '{"jsonrpc":"2.0","method":"notifications/initialized"}' $sid | Out-Null } catch { }
    return $sid
}

$sid = ''
if (Test-Path $sessFile) { $sid = (Get-Content $sessFile -Raw).Trim() }
if (-not $sid) { $sid = New-Session }

$calls = Get-Content $SpecFile -Raw | ConvertFrom-Json
$i = 0
foreach ($c in $calls) {
    $i++
    $label = if ($c.label) { $c.label } else { $c.tool }
    Write-Output "########## [$i] $label  ($($c.tool))"
    $argsJson = if ($c.args) { $c.args | ConvertTo-Json -Depth 20 -Compress } else { '{}' }
    $payload = '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"' + $c.tool + '","arguments":' + $argsJson + '}}'
    $out = $null
    try {
        $out = (Send-Mcp $payload $sid).text
        if ($out -match 'stale session') { throw 'stale' }
    } catch {
        $sid = New-Session
        $out = (Send-Mcp $payload $sid).text
    }
    try {
        $j = $out | ConvertFrom-Json
        if ($j.error) { Write-Output ("ERROR: " + ($j.error | ConvertTo-Json -Depth 20 -Compress)); continue }
        $content = $j.result.content
        if ($null -eq $content) { Write-Output ($j.result | ConvertTo-Json -Depth 40); continue }
        foreach ($cc in $content) {
            if ($cc.type -eq 'text') { Write-Output $cc.text }
            else { Write-Output ($cc | ConvertTo-Json -Depth 40) }
        }
    } catch {
        Write-Output $out
    }
    Write-Output ""
}
