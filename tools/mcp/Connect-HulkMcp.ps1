# Registers two already installed local MCP bridges in Codex.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RenderDocMcpRoot,
    [Parameter(Mandatory)][string]$QRenderDoc,
    [Parameter(Mandatory)][string]$GhidraMcpRoot,
    [ValidateRange(1,65535)][int]$GhidraPort = 8080
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed (exit $LASTEXITCODE)" }
}
$codex = (Get-Command codex -ErrorAction Stop).Source
$uv = (Get-Command uv -ErrorAction Stop).Source
$rd = (Resolve-Path -LiteralPath $RenderDocMcpRoot).Path
$gh = (Resolve-Path -LiteralPath $GhidraMcpRoot).Path
$qrd = (Resolve-Path -LiteralPath $QRenderDoc).Path
if (!(Test-Path -LiteralPath $qrd -PathType Leaf) -or (Split-Path $qrd -Leaf) -ine 'qrenderdoc.exe') {
    throw 'Select the installed qrenderdoc.exe'
}
foreach ($root in @($rd, $gh)) {
    if (!(Test-Path -LiteralPath (Join-Path $root 'pyproject.toml') -PathType Leaf)) {
        throw "Missing reviewed MCP checkout: $root"
    }
}
if (!(Test-NetConnection -ComputerName 127.0.0.1 -Port $GhidraPort -InformationLevel Quiet -WarningAction SilentlyContinue)) {
    throw 'Start Ghidra, enable its matching GhidraMCP extension, and open the private project first.'
}
# Use project-specific names; never remove or replace existing entries automatically.
foreach ($name in @('hulk_renderdoc','hulk_ghidra')) {
    & $codex mcp get $name *> $null
    if ($LASTEXITCODE -eq 0) { throw "Already registered: $name. Inspect with codex mcp get before changing." }
}
Invoke-Checked $uv @('run','--directory',$rd,'renderdoc-install-extension')
Invoke-Checked $codex @('mcp','add','hulk_renderdoc','--env',"RENDERDOC_QRENDERDOC_PATH=$qrd",'--',$uv,'run','--directory',$rd,'renderdoc-mcp')
Invoke-Checked $codex @('mcp','add','hulk_ghidra','--',$uv,'run','--directory',$gh,'ghidra-mcp','--ghidra-server',"http://127.0.0.1:$GhidraPort/")
Invoke-Checked $codex @('mcp','list')
Write-Host 'Registration complete. Restart local Codex and verify real MCP calls; registration alone is not a connection test.'
