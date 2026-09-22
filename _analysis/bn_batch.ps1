# bn_batch.ps1 - run several Binary Ninja MCP tool calls in one process.
# Usage: pwsh -NoProfile -File bn_batch.ps1 -CallsFile calls.json
# calls.json = [ { "tool": "bn_function_info", "args": { "address": "0x1406550B0" } }, ... ]
param([Parameter(Mandatory = $true)][string]$CallsFile)

$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot 'bn_mcp.ps1'
$calls = Get-Content $CallsFile -Raw | ConvertFrom-Json
$i = 0
foreach ($c in $calls) {
    $i++
    Write-Output "########## CALL $i : $($c.tool)"
    $argsJson = if ($null -ne $c.args) { $c.args | ConvertTo-Json -Depth 20 -Compress } else { '{}' }
    $params = '{"name":"' + $c.tool + '","arguments":' + $argsJson + '}'
    try {
        & pwsh -NoProfile -File $helper -Method 'tools/call' -ParamsJson $params
    }
    catch {
        Write-Output "CALL FAILED: $($_.Exception.Message)"
    }
    Write-Output ""
}
