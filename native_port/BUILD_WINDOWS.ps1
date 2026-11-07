# Build Hulk Native PC from sources on Windows. Native Vulkan needs an identified Hulk XDK profile.
# Usage: BUILD_WINDOWS.cmd -GameRoot "E:\Games\IncredibleHulk\games\incredible_hulk\game"
param(
  [string]$GameRoot = "",
  [switch]$NativeXLink,
  [switch]$Pm4Probe,
  [switch]$InputProbe
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Source = $PSScriptRoot
$BuildLog = Join-Path $Source 'HULK_BUILD_LOG.txt'
try { Start-Transcript -Path $BuildLog -Force | Out-Null } catch { Write-Warning 'Could not start build transcript' }
function Require-Command([string]$name) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Missing tool '$name'. Install Visual Studio C++, CMake, Ninja, Git and Python 3."
  }
}
function Invoke-Checked([string]$exe, [string[]]$arguments, [string]$dir) {
  Push-Location -LiteralPath $dir
  try {
    Write-Host ("RUN: " + $exe + " " + ($arguments -join ' '))
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $exe" }
  } finally { Pop-Location }
}
try {
  foreach ($cmd in @('git', 'cmake', 'ninja', 'clang++', 'python')) { Require-Command $cmd }
  if (-not $GameRoot) {
    $known = 'E:\Games\IncredibleHulk\games\incredible_hulk\game'
    if (Test-Path (Join-Path $known 'default.xex')) { $GameRoot = $known }
    elseif (Test-Path (Join-Path $Source 'game\default.xex')) { $GameRoot = Join-Path $Source 'game' }
    else { $GameRoot = Read-Host 'Full path to installed game folder containing default.xex' }
  }
  $GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
  $Xex = Join-Path $GameRoot 'default.xex'
  if (-not (Test-Path -LiteralPath $Xex)) { throw "default.xex not found in '$GameRoot'" }
  Write-Host "Game data: $GameRoot"

  $LocalGame = Join-Path $Source 'game'
  New-Item -ItemType Directory -Force -Path $LocalGame | Out-Null
  $LocalXex = Join-Path $LocalGame 'default.xex'
  if ($LocalXex -ne $Xex) { Copy-Item -LiteralPath $Xex -Destination $LocalXex -Force }
  # The game data can stay at its installed path. Codegen only needs default.xex.

  $Sdk = Join-Path $Source 'sdk-src'
  if (-not (Test-Path (Join-Path $Sdk 'CMakeLists.txt'))) {
    Invoke-Checked 'git' @('clone', '--branch', 'recomp', '--depth', '1', '--recurse-submodules',
      'https://github.com/ElGuslml/rexglue-sdk.git', $Sdk) $Source
  } else {
    Write-Host 'Using existing sdk-src (no automatic upgrade).'
    Invoke-Checked 'git' @('-C', $Sdk, 'submodule', 'update', '--init', '--recursive') $Source
  }
  # HULK USA DIAGNOSTIC: guard an observed potential MnK shutdown race.
  $InputPatch = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_input_lifetime.py'
  Invoke-Checked 'python' @($InputPatch, $Sdk) $Source
  # Validate a single SDK texture-fetch fix against known upstream anchors.
  # It affects shader colour math, NOT the native-GPU/XDK verification gate.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_spirv_fetch.py')) $Source
  $FetchPatch = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_spirv_fetch.py'
  Invoke-Checked 'python' @($FetchPatch, $Sdk) $Source
  # Instrument real guest VdSwap frequency to locate the source of 30 FPS.
  $FpsPatch = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_fps_probe.py'
  Invoke-Checked 'python' @($FpsPatch, $Sdk) $Source
  # The SDK carries a couple of symlinks which Git on Windows may check out as text files.
  Invoke-Checked 'python' @((Join-Path $Source 'tools\fix_symlinks.py'), $Sdk) $Source
  if ($NativeXLink) {
    # Vendored SDL forces its cache variables, so CMake -D alone cannot change them.
    $SdkPolicy = Join-Path (Split-Path $Source -Parent) 'ci\patch_sdk_native_only.py'
    Invoke-Checked 'python' @($SdkPolicy, $Sdk) $Source
    # ReXGlue already knows these PPC function entries but sometimes omits
    # CallEdge records at individual b/beq sites. Repair emission rather than
    # re-running the same function-discovery pass indefinitely.
    $SdkBranches = Join-Path (Split-Path $Source -Parent) 'ci\patch_sdk_branch_edges.py'
    Invoke-Checked 'python' @($SdkBranches, $Sdk) $Source
  }
  $SdkConfigArgs = @('--preset', 'win-amd64')
  if ($NativeXLink) {
    # Windows ReXGlue defaults to D3D12; enable the real Vulkan backend instead.
    $SdkConfigArgs += @('-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_USE_D3D12=OFF',
      '-DSDL_RENDER=ON', '-DSDL_JOYSTICK=OFF', '-DSDL_GAMEPAD=OFF', '-DSDL_VULKAN=ON')
  }
  Invoke-Checked 'cmake' $SdkConfigArgs $Sdk
  Invoke-Checked 'cmake' @('--build', 'out/build/win-amd64', '--config', 'Release', '--target', 'install', '--parallel') $Sdk
  $SdkInstall = Join-Path $Sdk 'out\install\win-amd64'
  $Codegen = Join-Path $SdkInstall 'bin\rexglue.exe'
  if (-not (Test-Path $Codegen)) { throw "SDK install did not produce $Codegen" }
  # Only real PPC code from the downloaded XEX is acceptable; no native stubs.
  $PpcTool = Join-Path (Split-Path $Source -Parent) 'ci/repair_hulk_startup.py'
  $XexSha256 = (Get-FileHash -LiteralPath $LocalXex -Algorithm SHA256).Hash.ToUpperInvariant()
  if ($env:HULK_PINNED_XEX_SHA256 -and $env:HULK_PINNED_XEX_SHA256.ToUpperInvariant() -ne $XexSha256) {
    throw "Release XEX changed: expected $env:HULK_PINNED_XEX_SHA256, got $XexSha256"
  }
  Write-Host "HULK INPUT XEX SHA256: $XexSha256"
  Invoke-Checked 'python' @($PpcTool, 'prepare', $LocalXex,
    (Join-Path $Source 'missing_funcs.toml'),
    (Join-Path $Source 'indirect_funcs.toml')) $Source
  Invoke-Checked $Codegen @('codegen', (Join-Path $Source 'incredible_hulk_manifest.toml')) $Source
  # ReXGlue codegen refreshes generated/rexglue.cmake. Its upstream
  # name sanitizer historically stripped underscores from project names,
  # resulting in an invalid incrediblehulk_manifest.toml during the Ninja
  # build. Abort here rather than waste a second 1000+ object compilation.
  # Reject missing registration or a fake PPC stub before the long C++ build.
  Invoke-Checked 'python' @($PpcTool, 'verify',
    (Join-Path $Source 'generated/default')) $Source
  $GluePath = Join-Path $Source 'generated\rexglue.cmake'
  if (-not (Test-Path -LiteralPath $GluePath)) {
    throw "ReXGlue did not write $GluePath"
  }
  $GlueText = Get-Content -LiteralPath $GluePath -Raw
  if ($GlueText -match 'incrediblehulk_manifest\.toml' -or
      $GlueText -notmatch 'incredible_hulk_manifest\.toml' -or
      $GlueText -notmatch 'incredible_hulk_codegen') {
    throw 'ReXGlue generated the wrong CMake manifest or codegen target; check ci/patch_sdk_branch_edges.py'
  }
  if (-not (Test-Path (Join-Path $Source 'generated\default\sources.cmake'))) {
    throw 'Codegen did not produce generated/default/sources.cmake; aborting before CMake configure.'
  }

  # Capture exact PPC instructions after codegen, from the SHA-pinned USA XEX.
  # This is an evidence-only developer report: NO guessed XDK signatures/hooks.
  $XdkAudit = Join-Path (Split-Path $Source -Parent) 'ci/audit_hulk_native_xdk_ppc.py'
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_audit_hulk_native_xdk_ppc.py')) $Source
  Invoke-Checked 'python' @($XdkAudit, '--generated', (Join-Path $Source 'generated/default'),
    '--candidates', (Join-Path $Source 'analysis/pm4_candidates.csv'),
    '--xex', $LocalXex, '--pinned-sha256', $XexSha256,
    '--region-lo', '0x82870000', '--region-hi', '0x828B0000',
    '--output', (Join-Path $Source 'out/analysis/hulk_6534_ppc_evidence.json')) $Source
  # Pin the renderer to the reviewed upstream release. D125 fixes an actual
  # NVIDIA/Vulkan crash while minimized by finishing hidden frames. D126 avoids
  # long pipeline-creation stalls on steady offscreen passes.
  # CI should never silently change native-GPU code mid-rebuild.
  $NativeXCommit = 'bf965e952c98e050753e91d08f32685a9f754b12'
  # Native profile unit tests include NativeXRenderer public headers.
  # Fetch them BEFORE configuring tests, not after the tests have compiled.
  if ($NativeXLink) {
    $NxrFull = Join-Path $Source 'native_x_renderer_git'
    if (-not (Test-Path (Join-Path $NxrFull 'include\\native_x_renderer\\native_x_renderer.h'))) {
      if (Test-Path $NxrFull) { throw "Incomplete NativeXRenderer checkout: $NxrFull" }
      Invoke-Checked 'git' @('clone', '--depth', '1', '--recurse-submodules',
        'https://github.com/ElGuslml/NativeXRenderer.git', $NxrFull) $Source
    }
    # Full SHA fetch works even if this shallow checkout started at a newer HEAD.
    Invoke-Checked 'git' @('-C', $NxrFull, 'fetch', '--depth', '1', 'origin', $NativeXCommit) $Source
    Invoke-Checked 'git' @('-C', $NxrFull, 'checkout', '--detach', $NativeXCommit) $Source
    Invoke-Checked 'git' @('-C', $NxrFull, 'submodule', 'update', '--init', '--recursive') $Source
    $ActualNativeX = (& git -C $NxrFull rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualNativeX -ne $NativeXCommit) {
      throw "NativeXRenderer checkout not pinned: $ActualNativeX"
    }
    Write-Host "HULK NATIVE GPU upstream bf965e9 (D125 hidden frame, D126 steady pipeline waits)"
  }
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_gen_native_x_hooks_probe.py')) $Source
  # Compile and run standalone input tests before the larger game build.
  $TestDir = Join-Path $Source 'out\input-tests'
  Invoke-Checked 'cmake' @('-S', (Join-Path $Source 'tests'), '-B', $TestDir, '-G', 'Ninja',
    '-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_BUILD_TYPE=Release') $Source
  Invoke-Checked 'cmake' @('--build', $TestDir, '--parallel') $Source
  Invoke-Checked 'ctest' @('--test-dir', $TestDir, '--output-on-failure') $Source

  $BuildDir = Join-Path $Source 'out\build\win-amd64-release'
  $cmakeArgs = @('--preset', 'win-amd64-release', '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF')
  # Explicit read-only debug instrumentation (OFF by default).
  if ($Pm4Probe) { $cmakeArgs += '-DHULK_XDK_PM4_PROBE=ON' }
  else { $cmakeArgs += '-DHULK_XDK_PM4_PROBE=OFF' }
  if ($InputProbe) { $cmakeArgs += '-DHULK_GAME_INPUT_PROBE=ON' }
  else { $cmakeArgs += '-DHULK_GAME_INPUT_PROBE=OFF' }
  if ($NativeXLink) {
    # NativeGPU requires a validated sidecar profile before guest rendering changes.
    if ($Pm4Probe) { throw 'Choose -NativeXLink or -Pm4Probe, not both' }
    $NxrFull = Join-Path $Source 'native_x_renderer_git'
    if (-not (Test-Path (Join-Path $NxrFull 'thirdparty\diligent-core\CMakeLists.txt'))) {
      if (Test-Path $NxrFull) { throw "Incomplete renderer directory: $NxrFull (delete it to retry)" }
      Invoke-Checked 'git' @('clone', '--depth', '1', '--recurse-submodules',
        'https://github.com/ElGuslml/NativeXRenderer.git', $NxrFull) $Source
    }
    $cmakeArgs += @("-DREXSDK_DIR=$Sdk", '-DHULK_WITH_NATIVE_X=ON',
      "-DHULK_NATIVE_X_SOURCE_DIR=$NxrFull", '-DREXGLUE_USE_VULKAN=ON',
      '-DREXGLUE_USE_D3D12=OFF', '-DSDL_RENDER=ON',
      '-DSDL_JOYSTICK=OFF', '-DSDL_GAMEPAD=OFF', '-DSDL_VULKAN=ON')
  } else {
    $cmakeArgs += @("-DCMAKE_PREFIX_PATH=$SdkInstall", '-DHULK_WITH_NATIVE_X=OFF')
  }
  Invoke-Checked 'cmake' $cmakeArgs $Source
  Invoke-Checked 'cmake' @('--build', $BuildDir, '--parallel') $Source
  $exe = Join-Path $BuildDir 'incredible_hulk.exe'
  if (-not (Test-Path $exe)) { throw "Build exited without producing $exe" }
  $Deliver = Join-Path $Source 'out\HULK_NATIVE_PC_TEST'
  if (Test-Path $Deliver) { Remove-Item -LiteralPath $Deliver -Recurse -Force }
  New-Item -ItemType Directory -Force -Path $Deliver | Out-Null
  Copy-Item $exe $Deliver -Force
  foreach ($dll in @('rexruntime.dll','rexgpu-xenos.dll')) {
    $candidates = @((Join-Path $BuildDir $dll), (Join-Path $SdkInstall "bin\$dll"))
    $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) { throw "Missing runtime DLL: $dll; check SDK installation" }
    Copy-Item $found $Deliver -Force
  }
  # NativeX / SDK may copy additional DLL dependencies next to the built executable.
  Get-ChildItem -LiteralPath $BuildDir -Filter '*.dll' -File -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Deliver -Force }
  # Ship one Windows executable, its runtime libraries, and one user-editable
  # TOML config. Developer scripts, INI profiles, hashes, and probe launchers
  # stay in the GitHub source tree and must NEVER enter the game ZIP.
  $Config = Join-Path $Source 'incredible_hulk.toml'
  if (-not (Test-Path -LiteralPath $Config)) {
    throw "Missing single-file game configuration: $Config"
  }
  Copy-Item -LiteralPath $Config -Destination (Join-Path $Deliver 'incredible_hulk.toml') -Force

  $ReleaseFiles = @(Get-ChildItem -LiteralPath $Deliver -File)
  $InvalidFiles = @($ReleaseFiles | Where-Object {
    $_.Extension.ToLowerInvariant() -notin @('.exe', '.dll', '.toml')
  })
  if ($InvalidFiles.Count -ne 0) {
    throw ("Unexpected developer files in release: " + ($InvalidFiles.Name -join ', '))
  }
  if (@($ReleaseFiles | Where-Object { $_.Extension -eq '.toml' }).Count -ne 1) {
    throw 'Release must contain exactly one TOML config'
  }
  if (@($ReleaseFiles | Where-Object { $_.Extension -eq '.exe' }).Count -ne 1) {
    throw 'Release must contain exactly one EXE'
  }
  if (@($ReleaseFiles | Where-Object { $_.Extension -eq '.dll' }).Count -lt 2) {
    throw 'Release is missing runtime DLL dependencies'
  }
  Write-Host "SUCCESS: portable EXE + DLLs + one TOML at $Deliver"
  Write-Host 'Run incredible_hulk.exe directly; edit game_data_root in incredible_hulk.toml if necessary.'

  try { Stop-Transcript | Out-Null } catch {}
  exit 0
} catch {
  Write-Host ("BUILD ERROR: " + $_.Exception.Message) -ForegroundColor Red
  Write-Host ("Send this file for diagnosis: " + $BuildLog)
  try { Stop-Transcript | Out-Null } catch {}
  exit 1
}

