# bn_mcp.ps1 - minimal MCP (streamable HTTP) client for Binary Ninja's WARP MCP server.
# Usage:
#   pwsh -File bn_mcp.ps1 -Method tools/list -ParamsJson '{}'
#   pwsh -File bn_mcp.ps1 -Method tools/call -ParamsJson '{"name":"bn_binary_view_list","arguments":{}}'
param(
    [Parameter(Mandatory = $true)][string]$Method,
    [string]$ParamsJson = '{}',
    [switch]$Reinit
)

$ErrorActionPreference = 'Stop'
$base = 'http://127.0.0.1:24642/mcp'
$sessFile = Join-Path $env:TEMP 'bn_mcp_session.txt'

function Send-Mcp([string]$body, [string]$sid) {
    $headers = @{ 'Accept' = 'application/json, text/event-stream' }
    if ($sid) { $headers['Mcp-Session-Id'] = $sid }
    $r = Invoke-WebRequest -Uri $base -Method Post -Body $body -ContentType 'application/json' `
        -Headers $headers -UseBasicParsing -TimeoutSec 900
    $ct = [string]$r.Headers['Content-Type']
    $text = $r.Content
    if ($ct -match 'event-stream') {
        $sb = New-Object System.Text.StringBuilder
        foreach ($line in ($text -split "`r?`n")) {
            if ($line.StartsWith('data:')) { [void]$sb.Append($line.Substring(5).Trim()) }
        }
        $text = $sb.ToString()
    }
    return @{ sid = [string]$r.Headers['Mcp-Session-Id']; text = $text }
}

function New-Session {
    $init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"trinity-doc","version":"1.0"}}}'
    $res = Send-Mcp $init ''
    $sid = $res.sid
    if (-not $sid) { $sid = ($res.text | ConvertFrom-Json).result.sessionId }
    if (-not $sid) { throw "no session id returned: $($res.text)" }
    $sid | Set-Content -Path $sessFile -NoNewline
    $note = '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    try { Send-Mcp $note $sid | Out-Null } catch { }
    return $sid
}

$sid = ''
if (-not $Reinit -and (Test-Path $sessFile)) { $sid = (Get-Content $sessFile -Raw).Trim() }

$payload = '{"jsonrpc":"2.0","id":2,"method":"' + $Method + '","params":' + $ParamsJson + '}'

$done = $false
if ($sid) {
    try {
        $out = Send-Mcp $payload $sid
        if ($out.text -match '"error"' -and $out.text -match 'session') { throw 'stale session' }
        $done = $true
    } catch { $done = $false }
}

if (-not $done) {
    $sid = New-Session
    $out = Send-Mcp $payload $sid
}

$out.text
