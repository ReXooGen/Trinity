# bn_comment.ps1 - set (or append to) a Binary Ninja comment, reading the text
# from a file so long comments need no shell/JSON escaping.
#
# Usage:
#   pwsh -NoProfile -File bn_comment.ps1 -Address 0x140654ED0 -TextFile body.txt
#   pwsh -NoProfile -File bn_comment.ps1 -Address 0x1406550B0 -TextFile add.txt -Append
param(
    [Parameter(Mandatory = $true)][string]$Address,
    [Parameter(Mandatory = $true)][string]$TextFile,
    [switch]$Append
)

$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot 'bn_mcp.ps1'

function Invoke-McpTool([string]$tool, $argsObj) {
    $argsJson = $argsObj | ConvertTo-Json -Depth 10 -Compress
    $params = '{"name":"' + $tool + '","arguments":' + $argsJson + '}'
    $raw = & pwsh -NoProfile -File $helper -Method 'tools/call' -ParamsJson $params
    return ($raw | ConvertFrom-Json)
}

$text = Get-Content $TextFile -Raw
# Normalise CRLF that the editor may have introduced.
$text = $text -replace "`r`n", "`n"
$text = $text.TrimEnd("`n")

if ($Append) {
    $existing = Invoke-McpTool 'bn_comment_get' @{ comment = $Address }
    $old = $existing.result.structuredContent.text
    if ($null -eq $old) { $old = '' }
    if ($old -ne '') {
        $text = ($old.TrimEnd("`n")) + "`n`n" + $text
        Write-Output "Appending to existing $($old.Length) char comment at $Address."
    }
    else {
        Write-Output "No existing comment at $Address; writing new one."
    }
}

$res = Invoke-McpTool 'bn_comment_set' @{ comment = $Address; text = $text }
if ($res.result.isError) {
    Write-Output "FAILED: $($res.result.structuredContent.errorMessage)"
    exit 1
}
$back = Invoke-McpTool 'bn_comment_get' @{ comment = $Address }
Write-Output ("OK: comment at {0} is now {1} chars." -f $Address, $back.result.structuredContent.text.Length)
