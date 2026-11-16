param([Parameter(Mandatory=$true)][string]$BackupDir)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($BackupDir.Trim('"'))
$manifestPath=Join-Path $root 'restore.json'
if(!(Test-Path -LiteralPath $manifestPath)){throw 'Missing restoration manifest'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$folder=[IO.Path]::GetFullPath($manifest.GameDir)
$exe=Join-Path $folder 'HULK.EXE'
if(!(Test-Path -LiteralPath $exe)){throw 'Game EXE not found'}
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.ExeSha256){throw 'Original EXE mismatch'}
if(Get-Process HULK -ErrorAction SilentlyContinue){throw 'Close Hulk first'}
foreach($f in $manifest.InstalledFiles){
 $dst=Join-Path $folder $f
 if($f -in $manifest.OriginalFiles){
  [IO.File]::Copy((Join-Path $root $f),$dst,$true)
 } elseif(Test-Path -LiteralPath $dst){
  Remove-Item -LiteralPath $dst -Force
 }
}
Write-Host 'Restored pre-HulkBridge game-local DLLs/settings from selected backup.' -ForegroundColor Green
