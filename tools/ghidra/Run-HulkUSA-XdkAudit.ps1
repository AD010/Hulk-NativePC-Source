# Read-only headless PowerPC/XEX callsite analysis. No game files uploaded.
# Requires local Ghidra with XEXLoaderWV installed in the same Ghidra release.
param(
  [Parameter(Mandatory=$true)][string]$GhidraRoot,
  [Parameter(Mandatory=$true)][string]$UsaXex,
  [Parameter(Mandatory=$true)][string]$OutputJson
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$expected='346A73690303F6633BFDAF9850E78793FE07CDBFB8F849B89FBEE320630B11F0'
$analyzer=Join-Path $GhidraRoot 'support\analyzeHeadless.bat'
$script=Join-Path $PSScriptRoot 'HulkUSA_XDKCallsiteAudit.java'
if (-not (Test-Path -LiteralPath $analyzer -PathType Leaf)) {
  throw "Ghidra headless launcher missing: $analyzer"
}
if (-not (Test-Path -LiteralPath $script -PathType Leaf)) {
  throw "Hulk source-only Ghidra script missing: $script"
}
if (-not (Test-Path -LiteralPath $UsaXex -PathType Leaf)) {
  throw "The original private USA default.xex was not provided"
}
$xex=(Resolve-Path -LiteralPath $UsaXex).Path
$sha=(Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash.ToUpperInvariant()
if ($sha -ne $expected) {
  throw "Refusing PAL/unknown XEX input; original USA hash must be $expected (got $sha)"
}
$output=[System.IO.Path]::GetFullPath($OutputJson)
if ($output -eq $xex) {throw 'Refusing to overwrite original XEX'}
$parent=Split-Path -Parent $output
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$projectRoot=Join-Path ([System.IO.Path]::GetTempPath()) ('HulkUSA_Ghidra_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $projectRoot -Force | Out-Null
try {
  Write-Host '[HULK/GHIDRA] Analyzing original USA PowerPC XEX locally'
  $cmdArgs=@($projectRoot,'HulkUSA_EoR','-import',$xex,
    '-scriptPath',$PSScriptRoot,
    '-postScript','HulkUSA_XDKCallsiteAudit.java',$xex,$output,
    '-analysisTimeoutPerFile','1800')
  & $analyzer @cmdArgs
  if ($LASTEXITCODE -ne 0) {
    throw "Ghidra headless analysis failed with code $LASTEXITCODE"
  }
  if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
    throw 'Ghidra analysis did not produce the original USA address-only JSON'
  }
  Write-Host "[HULK/GHIDRA] Original guest callsite evidence: $output"
} finally {
  # The Ghidra project contains private decoded Xbox code; never leave a
  # temporary project inside the public source repository or upload it.
  try {
    Remove-Item -LiteralPath $projectRoot -Recurse -Force -ErrorAction Stop
    Remove-Item -LiteralPath ($projectRoot + '\HulkUSA_EoR.gpr') -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath ($projectRoot + '\HulkUSA_EoR.rep') -Recurse -Force -ErrorAction SilentlyContinue
  } catch {
    Write-Warning "Could not remove private temporary Ghidra project: $projectRoot"
  }
}
