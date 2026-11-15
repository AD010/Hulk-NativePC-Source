param([string]$GameDir='')
$ErrorActionPreference = 'Stop'
$expected = '6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233'
try {
    if ([string]::IsNullOrWhiteSpace($GameDir)) {
        $GameDir = Read-Host 'Full path to the folder containing original HULK.EXE'
    }
    $folder = [System.IO.Path]::GetFullPath($GameDir.Trim('"'))
    $exe = Join-Path $folder 'HULK.EXE'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {throw "Original HULK.EXE not found in $folder"}
    $found = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($found -ne $expected) {throw "Unsupported HULK.EXE build. Expected SHA-256 $expected, found $found. No files changed."}
    $files = @('dinput8.dll','HulkPCProbe.asi')
    foreach ($name in $files) {
        $source = Join-Path (Join-Path $PSScriptRoot 'bin') $name
        $target = Join-Path $folder $name
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) {throw "Package incomplete: $name"}
        if (Test-Path -LiteralPath $target) {
            $srcHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            $dstHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            if ($srcHash -ne $dstHash) {
                throw "Conflicting existing $target. No existing mod/DLL will be overwritten."
            }
        }
    }
    foreach ($name in $files) {
        $source = Join-Path (Join-Path $PSScriptRoot 'bin') $name
        $target = Join-Path $folder $name
        if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
            [System.IO.File]::Copy($source,$target,$false)
            Write-Host "Installed: $name"
        } else {
            Write-Host "Already installed, SHA verified: $name"
        }
    }
    Write-Host 'Original HULK.EXE unchanged. Start the game normally.' -ForegroundColor Green
    Write-Host 'Diagnostic output: HulkPC_ASI.log in the game folder.'
} catch {
    Write-Host "Installation refused: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
