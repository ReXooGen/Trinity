# bn.ps1 - robust MCP (streamable HTTP) client for Binary Ninja's WARP MCP server.
#
# The harness-provided mcp__binaryninja__* tools in this session fail with
# WinError 10061 (target machine actively refused), while the WARP endpoint that
# Binary Ninja itself serves on 127.0.0.1:24642 is healthy. This script talks to
# that endpoint directly, which is the same access path the rest of this repo's
# analysis tooling already uses.
#
# Usage:
#   pwsh -File bn.ps1 -Tool bn_binary_view_list
#   pwsh -File bn.ps1 -Tool bn_comment_set -ArgsJson '{"comment":"0x1406550B0","text":"hi"}'
#   pwsh -File bn.ps1 -Tool bn_functions_list -ArgsJson '{"offset":0,"limit":50}'
#   pwsh -File bn.ps1 -ListTools
#   pwsh -File bn.ps1 -Tool ... -Raw
param(
    [string]$Tool,
    [string]$ArgsJson = '{}',
    [switch]$ListTools,
    [switch]$Raw,
    [switch]$Reinit,
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
    if (-not $sid) { throw "no session id returned: $($res.text)" }
    $sid | Set-Content -Path $sessFile -NoNewline
    try {
        $note = '{"jsonrpc":"2.0","method":"notifications/initialized"}'
        Send-Mcp $note $sid | Out-Null
    } catch { }
    return $sid
}

function Invoke-Tool([string]$name, [string]$argsJson, [string]$sid) {
    $payload = '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"' + $name + '","arguments":' + $argsJson + '}}'
    return (Send-Mcp $payload $sid).text
}

function Get-Session {
    if (-not $Reinit -and (Test-Path $sessFile)) {
        $s = (Get-Content $sessFile -Raw).Trim()
        if ($s) { return $s }
    }
    return (New-Session)
}

if ($ListTools) {
    $sid = Get-Session
    $payload = '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
    $out = (Send-Mcp $payload $sid).text
    if ($Raw) { $out } else {
        $j = $out | ConvertFrom-Json
        $j.result.tools | ForEach-Object { $_.name }
    }
    exit 0
}

if (-not $Tool) { throw "specify -Tool or -ListTools" }

$sid = Get-Session
$out = $null
try {
    $out = Invoke-Tool $Tool $ArgsJson $sid
    if ($out -match '"error"' -and $out -match 'session') { throw 'stale session' }
} catch {
    $sid = New-Session
    $out = Invoke-Tool $Tool $ArgsJson $sid
}

if ($Raw) { $out; exit 0 }

# Unwrap the MCP tool result envelope down to its text payload.
try {
    $j = $out | ConvertFrom-Json
} catch {
    $out; exit 0
}
if ($j.error) {
    Write-Output ("ERROR: " + ($j.error | ConvertTo-Json -Depth 20 -Compress))
    exit 1
}
$content = $j.result.content
if ($null -eq $content) {
    $j.result | ConvertTo-Json -Depth 40
    exit 0
}
foreach ($c in $content) {
    if ($c.type -eq 'text') { Write-Output $c.text }
    else { $c | ConvertTo-Json -Depth 40 }
}
if ($j.result.isError) { exit 1 }
