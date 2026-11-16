param([string]$GameDir='')
$ErrorActionPreference='Stop'
$expected='6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233'
$knownProxies=@(
 '2ea93f8427fd139aa2f39c793f2b5352ec525bf2ba76bafd0ddcc70b55b2045d',
 '4553d4e4d3778792273ba2e5f60f2e146ae0836b72743058ff11f0028df82fc8',
 '9ede6318807ce9db3e9e3056976e8fb381e18a867007c0b54da0cd39776e7e6b',
 'f08274eccda801eee19c41c00b8d9001c09b8a6440a2c6c8be8e2c7104d980b4',
 'fe1591539fb8f4fc773ec31602078f306246a34bc4b062d62255fff846675e27',
 '847dbffd02caea306b1e32476a601e0d82b7b5ef73887c83a87384248049d1bb'
)
function Hash([string]$p) {return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
try {
 if([string]::IsNullOrWhiteSpace($GameDir)){$GameDir=Read-Host 'Folder containing original HULK.EXE'}
 $folder=[IO.Path]::GetFullPath($GameDir.Trim('"'))
 $exe=Join-Path $folder 'HULK.EXE'
 if(!(Test-Path -LiteralPath $exe -PathType Leaf)) {throw 'Original HULK.EXE not found'}
 if((Hash $exe) -ne $expected) {throw 'Unsupported HULK.EXE. No files changed.'}
 if(Get-Process HULK -ErrorAction SilentlyContinue) {throw 'Close Hulk before installing.'}
 $modules=@('dinput8.dll','HulkBridge.asi','HulkShader.asi')
 foreach($f in $modules) {
  if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot ('bin\'+$f)) -PathType Leaf)) {throw "Missing package file $f"}
 }
 $targetProxy=Join-Path $folder 'dinput8.dll'
 if((Test-Path -LiteralPath $targetProxy) -and (Hash $targetProxy) -notin $knownProxies) {
  throw 'Third-party dinput8.dll found. Refusing to replace or interfere with it.'
 }
 foreach($f in @('HulkBridge.asi','HulkShader.asi')) {
  $t=Join-Path $folder $f
  if((Test-Path -LiteralPath $t) -and (Hash $t) -ne (Hash (Join-Path $PSScriptRoot ('bin\'+$f)))) {
   throw "Unrecognized existing $f. No files changed."
  }
 }
 $backup=Join-Path $folder ('HulkBridge_backup_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
 [IO.Directory]::CreateDirectory($backup)|Out-Null
 $original=@()
 foreach($f in @('dinput8.dll','HulkBridge.asi','HulkShader.asi','HulkBridge.ini')) {
  $path=Join-Path $folder $f
  if(Test-Path -LiteralPath $path -PathType Leaf){
   [IO.File]::Copy($path,(Join-Path $backup $f),$false)
   $original+= $f
  }
 }
 $metadata=@{OriginalFiles=$original;InstalledFiles=@($modules)+@('HulkBridge.ini');ExeSha256=$expected;GameDir=$folder}
 $metadata|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $backup 'restore.json') -Encoding UTF8
 foreach($f in $modules){
  $src=Join-Path $PSScriptRoot ('bin\'+$f)
  [IO.File]::Copy($src,(Join-Path $folder $f),$true)
 }
 $settings=Join-Path $folder 'HulkBridge.ini'
 if(!(Test-Path -LiteralPath $settings)){
  [IO.File]::Copy((Join-Path $PSScriptRoot 'HulkBridge.ini'),$settings,$false)
 }
 Write-Host 'HulkBridge x86 installed; original resources, HULK.EXE, DXVK d3d9.dll untouched.' -ForegroundColor Green
 Write-Host 'Resource and shader injection are OFF until a verified PC-compatible sidecar exists.'
 Write-Host "Backup: $backup"
 Write-Host 'Previous HulkPC.asi remains in place but new dinput8 proxy does not load it while HulkBridge modules are present.'
} catch {
 Write-Host "Installation refused: $($_.Exception.Message)" -ForegroundColor Red
 exit 1
}
