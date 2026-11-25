# Build Hulk Native PC from sources on Windows. Native Vulkan needs an identified Hulk XDK profile.
# Usage: BUILD_WINDOWS.cmd -GameRoot "E:\Games\IncredibleHulk\games\incredible_hulk\game"
param(
  [string]$GameRoot = "",
  [switch]$NativeXLink,
  [switch]$Pm4Probe,
  [switch]$InputProbe,
  [switch]$EuTu1Analysis,
  [string]$EuTu1Zip = "",
  [string]$EuRexGlue = ""
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Source = $PSScriptRoot
$BuildLog = Join-Path $Source 'HULK_BUILD_LOG.txt'
try { Start-Transcript -Path $BuildLog -Force | Out-Null } catch { Write-Warning 'Could not start build transcript' }
# One validated entrypoint for 90 ordered NativeX patch/test stages.
# Preserve source ABI checks and in-between C++ render-target guards.
function Invoke-HulkNativeXPatch(
    [ValidatePattern('^(test_)?patch_native_x_hulk_[a-z0-9_]+[.]py$')]
    [string]$ScriptName) {
  $CiDir = Join-Path (Split-Path $Source -Parent) 'ci'
  $Script = Join-Path $CiDir $ScriptName
  $Dispatcher = Join-Path $CiDir 'native_x_d140_dispatch.py'
  if (-not (Test-Path -LiteralPath $Script -PathType Leaf) -or
      -not (Test-Path -LiteralPath $Dispatcher -PathType Leaf)) {
    throw "Missing reviewed NativeX script or dispatcher: $ScriptName"
  }
  if ($ScriptName.StartsWith('test_')) {
    Invoke-Checked 'python' @($Dispatcher, '--test', $Script) $Source
  } else {
    Invoke-Checked 'python' @($Dispatcher, '--patch', $Script, $NxrFull) $Source
  }
}
# A verified NativeX transformation is atomic: run its regression first,
# then apply exactly the same-named patch through the pinned SHA dispatcher.
# No graphics source will be mutated if the associated test fails.
function Invoke-HulkNativeXCheckedPatch(
    [ValidatePattern('^patch_native_x_hulk_[a-z0-9_]+[.]py$')]
    [string]$ScriptName) {
  Invoke-HulkNativeXPatch ("test_" + $ScriptName)
  Invoke-HulkNativeXPatch $ScriptName
}
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
  # EU PAL+TU1 is a NEW PPC codegen target, NOT a drop-in USA XEX.
  # Keep its original executable, real 30 overlay archives and decoded
  # delta resources isolated from main's USA generated C++, profile and ZIP.
  # This branch never enters the USA NativeX/XDK/Release build path.
  if ($EuTu1Analysis) {
    if ($NativeXLink -or $Pm4Probe -or $InputProbe) {
      throw 'EU TU1 codegen cannot reuse USA NativeX/PM4/XDK hooks or probes'
    }
    Require-Command 'python'
    if (-not $GameRoot -or -not $EuTu1Zip) {
      throw 'EU TU1 analysis requires -GameRoot EU base folder and -EuTu1Zip'
    }
    $EuXex = Join-Path $GameRoot 'default.xex'
    if (-not (Test-Path -LiteralPath $EuXex -PathType Leaf)) {
      throw 'Original EU/PAL default.xex missing'
    }
    $EuSha = (Get-FileHash -LiteralPath $EuXex -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($EuSha -ne 'ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351') {
      throw 'EU TU1 base XEX is not the user-verified retail edition'
    }
    if (-not (Test-Path -LiteralPath $EuTu1Zip -PathType Leaf)) {
      throw 'Signed original EU TU1 ZIP missing'
    }
    if (-not $EuRexGlue) {
      $EuRexGlue = Join-Path $Source 'sdk-src/out/install/win-amd64/bin/rexglue.exe'
    }
    if (-not (Test-Path -LiteralPath $EuRexGlue -PathType Leaf)) {
      throw 'The pinned ReXGlue codegen executable is required for EU TU1 PPC comparison'
    }
    $EuOut = Join-Path $Source 'out/analysis/eu-tu1'
    $EuWork = Join-Path $EuOut 'codegen'
    $EuReport = Join-Path $EuOut 'eu-tu1-diff.json'
    if ((Test-Path -LiteralPath $EuWork) -or (Test-Path -LiteralPath $EuReport)) {
      throw 'Refusing to overwrite existing private EU TU1 source analysis'
    }
    New-Item -ItemType Directory -Path $EuOut -Force | Out-Null
    $EuRunner = Join-Path (Split-Path $Source -Parent) 'ci/run_hulk_tu1_dual_codegen.py'
    Invoke-Checked 'python' @($EuRunner, '--rexglue', $EuRexGlue,
      '--eu-xex', $EuXex, '--tu-zip', $EuTu1Zip, '--work', $EuWork,
      '--report', $EuReport) $Source
    Write-Host "EU TU1 PPC codegen and 525 private update resources verified: $EuReport"
    Write-Host 'This is codegen analysis, NOT a rebuilt EU game; EU hook ABI and Vulkan must be remapped.'
    try { Stop-Transcript | Out-Null } catch {}
    exit 0
  }
  foreach ($cmd in @('git', 'cmake', 'ctest', 'ninja', 'clang++', 'python')) { Require-Command $cmd }
  # Fail early if a future update reintroduces PAL data acceptance or unbounded GPU.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_us_gpu_defaults.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_pc_menu.py')) $Source
  # Source-only original EoR PC/Xbox index regression (no game archives in Git).
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_audit_hulk_pc_x360_ui_assets.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_pc_menu_win32.py')) $Source
  # Real compiler-signature check before 1000+ C++ units; #42484697484 regression.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_win32_cursor_compile.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_audit_hulk_world_visuals.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_map_hulk_world_stream_trace.py')) $Source
  # Independent, synthetic-only PM4 provenance and visual-regression triage.
  # They never read copyrighted game bytes and cannot modify GPU state.
  # Shader disassembly must never access outside diagnostic filter tables.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_fetch_disasm_bounds.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_cf_bounds.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_pm4_register_oracle.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_render_log_audit.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_audit_hulk_tu1_resource_index.py')) $Source
  # EU TU1 water fix: source-only XEX signature and changed-PPC comparator
  # tests; no proprietary TU data is bundled or applied to USA codegen.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_diff_hulk_tu1_guest_ppc.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_run_hulk_tu1_dual_codegen.py')) $Source
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
  # Local Windows builds must enforce the same USA input and PPC entry
  # preparation as the private CI runner, before spending time on SDK builds.
  $ExpectedUsaXex = '346A73690303F6633BFDAF9850E78793FE07CDBFB8F849B89FBEE320630B11F0'
  $ActualUsaXex = (Get-FileHash -LiteralPath $LocalXex -Algorithm SHA256).Hash.ToUpperInvariant()
  if ($ActualUsaXex -ne $ExpectedUsaXex) {
    throw 'USA NativePC build requires the pinned original USA XEX. Use -EuTu1Analysis for EU analysis.'
  }
  $SeedPpcTool = Join-Path (Split-Path $Source -Parent) 'ci/seed_hulk_ppc_tails.py'
  Invoke-Checked 'python' @($SeedPpcTool, (Join-Path $Source 'missing_funcs.toml'),
    (Join-Path $Source 'indirect_funcs.toml')) $Source

  # Exact compatible ReXGlue SDK fork. Do not silently upgrade this source
  # to official v0.11's different module/driver ABI or floating recomp HEAD.
  $SdkCommit = '4348a344eb86ae456e1ccf7d393085d720cc6ce6'
  $Sdk = Join-Path $Source 'sdk-src'
  if (-not (Test-Path (Join-Path $Sdk 'CMakeLists.txt'))) {
    Invoke-Checked 'git' @('clone', '--branch', 'recomp', '--depth', '1', '--no-checkout',
      'https://github.com/ElGuslml/rexglue-sdk.git', $Sdk) $Source
    Invoke-Checked 'git' @('-C', $Sdk, 'fetch', '--depth', '1', 'origin', $SdkCommit) $Source
    Invoke-Checked 'git' @('-C', $Sdk, 'checkout', '--detach', $SdkCommit) $Source
  } else {
    Write-Host 'Using existing sdk-src only if it matches the reviewed SHA.'
  }
  $ActualSdkSha = (& git -C $Sdk rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0 -or $ActualSdkSha -ne $SdkCommit) {
    throw "Unreviewed ReXGlue SDK $ActualSdkSha; expected $SdkCommit. Use a fresh sdk-src."
  }
  Invoke-Checked 'git' @('-C', $Sdk, 'submodule', 'update', '--init', '--recursive') $Source
  # Official v0.11 PPC semantics; source-checked, ABI-preserving backport.
  $SdkBackportTest = Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_upstream_v011_ppc.py'
  $SdkBackport = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_upstream_v011_ppc.py'
  Invoke-Checked 'python' @($SdkBackportTest, $Sdk) $Source
  Invoke-Checked 'python' @($SdkBackport, $Sdk) $Source
  # Source-proven host services: XMA/SDL/WASAPI/XAM/VFS/threads.
  # Report raw SDK support and explicit XAM stubs WITHOUT claiming that
  # the USA XEX reaches those APIs or that gameplay is correct.
  $ServicesAudit = Join-Path (Split-Path $Source -Parent) 'ci/audit_hulk_sdk_services.py'
  Invoke-Checked 'python' @($ServicesAudit, $Sdk, '--output',
    (Join-Path $Source 'out/analysis/hulk_sdk_services_audit.json')) $Source
  # HULK USA DIAGNOSTIC: guard an observed potential MnK shutdown race.
  $InputPatch = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_input_lifetime.py'
  Invoke-Checked 'python' @($InputPatch, $Sdk) $Source
  # Read-only guest XThread creation/affinity evidence, OFF unless requested.
  # This neither schedules Havok jobs nor changes 30/60 Hz simulation.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_thread_probe.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_thread_probe.py'), $Sdk) $Source
  # Protect save package names before any host filesystem operation.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_save_names.py'), $Sdk) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_save_names.py'), $Sdk) $Source
   Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_native_save_root.py'), $Sdk) $Source
   Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_native_save_root.py'), $Sdk) $Source
  # Reused index ranges must invalidate CPU primitive conversions, including
  # exact rewrites and writes covering the entire cached range.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_index_cache.py'), $Sdk) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_index_cache.py'), $Sdk) $Source
  # Xenia Edge 4ad875: add (do not multiply) SIMD co-alignment bytes
  # when sizing converted-index pages; avoid 16x GPU index pool waste.
  # One synthetic-only test suite for all three Xenia Edge source adapters.
  # Each staged patch checks the CURRENT pinned SDK source before mutation.
  # Three SDK adaptations occur at DIFFERENT source stages. The real
  # shader-cache version here is 12, then 13 after spirv-fetch and 14
  # after computed-lod; testing scalar's stage-14 version here would
  # falsely fail before those required earlier transformations. Run
  # synthetic regressions now; each staged patch validates its current
  # exact real SDK source at the moment it is applied.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_xenia_edge.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_xenia_edge.py'), 'index-pool', $Sdk) $Source
  # Xenia Edge 344fca7: exact Xenos packed mip0 page and 2x1 tail
  # addressing, also used by the Legacy/NativeX GPU texture utility.

  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_xenia_edge.py'), 'packed-mip', $Sdk) $Source
  # Hulk original city: locate real low/high LOD archive ranges, no rendering changes.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_world_stream_trace.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_world_stream_trace.py'), $Sdk) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_city_distance.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_city_distance.py'), $Sdk) $Source
  # The original Xenos vertex disassembler can format a null format name;
  # this is not a US XEX string-table offset. Keep shader analysis live.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_shader_disasm.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_shader_disasm.py'), $Sdk) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_fetch_disasm_bounds.py'), $Sdk) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_cf_bounds.py'), $Sdk) $Source
  # Validate a single SDK texture-fetch fix against known upstream anchors.
  # It affects shader colour math, NOT the native-GPU/XDK verification gate.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_spirv_fetch.py')) $Source
  $FetchPatch = Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_spirv_fetch.py'
  Invoke-Checked 'python' @($FetchPatch, $Sdk) $Source
  # Preserve pixel shader getCompTexLOD via native OpImageQueryLod instead
  # of returning a fabricated zero for contradictory optional LOD flags.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_computed_lod.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_computed_lod.py'), $Sdk) $Source
  # Xenia Edge 09d405f: one-component Xenos vertex fetches are scalar
  # SPIR-V OpFMul, not OpVectorTimesScalar. The latter can crash NVIDIA.
  # Keep the pinned SDK and invalidate the original translator cache.

  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/patch_sdk_hulk_xenia_edge.py'), 'spirv-scalar', $Sdk) $Source
  # FPS control must be a real registered SDK cvar/limiter, not just
  # decorative F10 buttons. Validate the source patch before game codegen.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_patch_sdk_hulk_fps_probe.py')) $Source
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
  # Same SSE4.2 baseline for the SDK and generated native PPC target.
  # SIMDe provides the portable implementation when AVX is unavailable.
  $CpuBaseline = '-march=x86-64-v2 -mno-avx -mno-avx2'
  $CpuAudit = Join-Path (Split-Path $Source -Parent) 'ci/audit_hulk_cpu_baseline.py'
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_audit_hulk_cpu_baseline.py')) $Source
  $SdkConfigArgs = @('--preset', 'win-amd64',
    "-DCMAKE_C_FLAGS=$CpuBaseline", "-DCMAKE_CXX_FLAGS=$CpuBaseline",
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
  if ($NativeXLink) {
    # Windows ReXGlue defaults to D3D12; enable the real Vulkan backend instead.
    $SdkConfigArgs += @('-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_USE_D3D12=OFF',
      '-DSDL_RENDER=ON', '-DSDL_JOYSTICK=OFF', '-DSDL_GAMEPAD=OFF', '-DSDL_VULKAN=ON')
  }
  Invoke-Checked 'cmake' $SdkConfigArgs $Sdk
  Invoke-Checked 'python' @($CpuAudit, (Join-Path $Sdk 'out/build/win-amd64/compile_commands.json'), $Sdk) $Source
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
  # Source-only reverse PowerPC call graph: identify genuine Edge of Reality
  # scene dispatchers ABOVE Draw/Resolve/Present for the future D3D11 backend.
  # Report contains addresses only; no game bytes leave the private runner.
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_engine_input_callgraph.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_engine_render_callgraph.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_eor_xdk_callsite_crosswalk.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_ghidra_xdk_crosscheck.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/hulk_engine_render_callgraph.py'),
    '--generated', (Join-Path $Source 'generated/default'),
    '--source-xex', $LocalXex,
    '--profile', (Join-Path $Source 'hulk_native_x_profile.ini'),
    '--output', (Join-Path $Source 'out/analysis/hulk_engine_render_callgraph.json')) $Source
  # When a real Hulk PC owner runtime trace is available, triangulate its
  # exact PPC LR with genuine USA guest direct-bl callsites. This diagnostic
  # never interprets r3..r10 as material/camera ABI and never changes GPU state.
  if ($env:HULK_EOR_XDK_RUNTIME_LOG) {
    if (-not (Test-Path -LiteralPath $env:HULK_EOR_XDK_RUNTIME_LOG -PathType Leaf)) {
      throw "HULK_EOR_XDK_RUNTIME_LOG not found"
    }
    Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/hulk_eor_xdk_callsite_crosswalk.py'),
      '--generated', (Join-Path $Source 'generated/default'),
      '--source-xex', $LocalXex,
      '--profile', (Join-Path $Source 'hulk_native_x_profile.ini'),
      '--log', $env:HULK_EOR_XDK_RUNTIME_LOG,
      '--output', (Join-Path $Source 'out/analysis/hulk_eor_xdk_verified_callsites.json')) $Source
  }
  # Optional independently imported XEX Ghidra 12 edge list, produced
  # locally with tools/ghidra/HulkUSA_XDKCallsiteAudit.java. Three exact
  # witnesses are required: SHA-pinned XEX, ReXGlue PPC bl, runtime LR.
  if ($env:HULK_EOR_GHIDRA_EDGES_JSON) {
    if (-not $env:HULK_EOR_XDK_RUNTIME_LOG) {
      throw 'Ghidra crosscheck requires HULK_EOR_XDK_RUNTIME_LOG'
    }
    if (-not (Test-Path -LiteralPath $env:HULK_EOR_GHIDRA_EDGES_JSON -PathType Leaf)) {
      throw 'Ghidra USA XEX address-only evidence JSON missing'
    }
    Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/hulk_ghidra_xdk_crosscheck.py'),
      '--ghidra', $env:HULK_EOR_GHIDRA_EDGES_JSON,
      '--runtime', (Join-Path $Source 'out/analysis/hulk_eor_xdk_verified_callsites.json'),
      '--output', (Join-Path $Source 'out/analysis/hulk_eor_triple_verified_callsites.json')) $Source
  }
  # Pin the renderer to the reviewed upstream release. D125 fixes an actual
  # NVIDIA/Vulkan crash while minimized by finishing hidden frames. D126 avoids
  # long pipeline-creation stalls on steady offscreen passes.
  # CI should never silently change native-GPU code mid-rebuild.
  # NativeX D140 integration candidate, fully source-checked on its true SHA.
  # Do not replace the existing main/Release pin until this compiles and plays.
  # Each patch validates the checkout revision; never fake the pinned git HEAD.
  $NativeXCommit = 'fa19ea85fec5c7f7f8a2df82d6c4a86548817b52'
  $env:HULK_NATIVEX_SOURCE_SHA = $NativeXCommit
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
    Write-Host "HULK NATIVE GPU upstream fa19ea85 (D140; Hulk source-only patch chain verified)"
    # The crash dump proved a null mapped buffer after dynamic heap exhaustion;
    # fail closed on a changed upstream checkout, do not bump the heap to mask it.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_heap_guard.py'
    # Log 087: recover frame completion via inner Swap, keep Resolve/front metadata.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_present_semantics.py'
    # Diagnostic black-frame repair: use actual recent guest color Resolve
    # if the XDK front object's fetch pointer never matches a Resolve.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_front_fallback.py'
    # Log099 GPU observer: 1035/1397 pixel shader and packet-state mismatches.
    # Scan authentic PM4/IM_LOAD between the ten identified USA XDK hooks (Clear/ClearF included).
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_pm4_gap.py'
    # Hulk log101: direct PM4 draws can still happen outside the ten XDK hooks.
    # Capture complete single unhooked draws through NativeX's Vulkan command queue.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_gap_draw.py'
    # Replay ordered Xenos PM4 draws, including indirect buffers, into native Vulkan.
    # This augments the earlier single-direct-draw path without another workflow.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_gap_stream.py'
    # Only complete Xenos DRAW_INDX(_2) packets can reach the native Vulkan callback.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_pm4_draw_payload.py'
    # Logs 115/116: guest 1280x720 inside 2560x1440 host, screenshot quarter-frame.
    # Audit native viewport, resolve rectangles and final blit without faking scaling.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_frame_path_probe.py'
    # Hulk log119: guest 640x360 x4 MSAA scene occupies only one quarter of
    # native 1280x720 output. Map its 2x2 sample grid to host pixels, no EDRAM.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_msaa_grid.py'
    # Scene Resolve must retain the exact 2560x1440 native host image
    # (not just the original 640x360 guest descriptor) before any C++ build.
    foreach ($NxrFile in @('src/renderer.cpp','src/targets.cpp','src/targets.h')) {
      $Path = Join-Path $NxrFull $NxrFile
      if (-not (Get-Content -LiteralPath $Path -Raw).Contains('HULK/TRUE-RESOLVE-SCALE')) {
        throw "Hulk native scene Resolve scale contract missing from $NxrFile"
      }
    }
    # Hulk log 124: full-output NativeX draw clipped to 640 guest columns.
    # Scissor repair affects only 1280x720 half-width fullscreen passes.
    # Audit live 640x360 4x-MSAA scene resolves, not just intro boot frames.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_output_scissor.py'
    # Hulk AA: only on detected 640x360/4x -> 1280x720 scene resolve,
    # never on the finished front buffer with the HUD already attached.
    # GPU geometry: drop invalid PM4 synthetic primitive types 0/48/49.
    Invoke-HulkNativeXPatch 'test_patch_native_x_hulk_scene_aa_geometry.py'
    Invoke-HulkNativeXPatch 'patch_native_x_hulk_scene_aa.py'
    Invoke-HulkNativeXPatch 'patch_native_x_hulk_geometry_guard.py'
    # Non-EDRAM native scene to postprocess: preserve each source and output
    # host pixel scale instead of clipping high-resolution samples to a
    # smaller target. Trace real Resolve callers for later exact profile.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_resolve_scale.py'
    # Hulk actual Xenos 1280/1x and 640/4x scene surfaces share EDRAM
    # base/pitch layout, but must share the newest native Vulkan pixel data.
    # Resolve the 1x view from the last-written matching 4x native target,
    # without reintroducing EDRAM emulation or guest surface mutations.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_reverse_alias.py'
    # Pixel-exact 4x EDRAM depth can still require a packed stencil Resolve.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_depth_stencil_grid.py'
    # Physically identical 1x/4x Xbox sample-grid views must share native Vulkan
    # image storage across Draw/Clear/Resolve, including depth. Fail closed.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_shared_storage.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_shared_write_order.py'
    # Hulk 137/RenderDoc 3: synthetic PM4 draws must use each packet's GPU RT/depth identity.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_packet_surfaces.py'
    # Hulk 137: skipped color/depth clears must not advance shared Vulkan image ownership.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_clear_ownership.py'
    # Log146: recover genuine Vulkan draw packets rejected by unvalidated strict RT identity.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_surface_fallback.py'
    # Source-only CI preflight has already applied this COMPLETE patch chain
    # to the exact pinned NativeX revision before expensive private codegen.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_packet_first.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_draw_write_ownership.py'
    # Hulk 158: nonzero color masks on NEVER bound RT1..3 are not MRT attachments.
    # Keep strict matching of RT0, depth and every observed real MRT.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_unbound_mrt.py'
    # Before the guest command-buffer ring is reused, preserve only actual
    # bounded DMA-index bytes for packet and ordinary XDK indexed captures.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_packet_dma_snapshot.py'
    # Hulk log 167: actual GPU 640x360/4x view can differ from real XDK
    # 1280x720/1x surface header at the SAME EDRAM base and format.
    # Recover only that exact proven pair; reject all other mismatches.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_sample_grid_packet_binding.py'
    # Log 173: verify the packet-time RT0 EDRAM base correction actually
    # reached the generated NativeX C++ before the expensive Windows link.
    $HulkNativeRendererSource = Get-Content -LiteralPath (Join-Path $NxrFull 'src/renderer.cpp') -Raw
    if (-not $HulkNativeRendererSource.Contains('[HULK/PM4-RT0-REBASE]')) {
      throw 'Hulk log173 PM4 RT0 EDRAM rebase missing from compiled renderer.'
    }
    if (-not $HulkNativeRendererSource.Contains('[HULK/PM4-DMA-REJECT]') -or
        -not $HulkNativeRendererSource.Contains('HULK/DIRECT-DMA-SNAPSHOT') -or
        -not $HulkNativeRendererSource.Contains('!CapturePacketDmaIndices(tracker, base, c, packet_surfaces != nullptr))')) {
      throw 'Hulk log173 incomplete DMA capture rejection absent from compiled renderer.'
    }
    $HulkNativeDrawSource = Get-Content -LiteralPath (Join-Path $NxrFull 'src/draw.cpp') -Raw
    if (-not $HulkNativeDrawSource.Contains('HULK/DMA-RESTORE-AFTER-AHEAD')) {
      throw 'Hulk DMA restoration after speculative uploads absent from compiled renderer.'
    }
    if (-not $HulkNativeDrawSource.Contains('[HULK/EMPTY-DMA-INDEX]')) {
      throw 'Hulk log173 empty DMA DrawInternal guard missing from compiled renderer.'
    }
    Remove-Variable HulkNativeRendererSource
    Remove-Variable HulkNativeDrawSource
    # Snapshot the actual missing per-draw RT/depth identities from Hulk 160.
    # Diagnostic-only: no invented surfaces or forced drawing.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_surface_miss_trace.py'
    # F10 belongs to Hulk's real PC menu, never steal it for the SDK observer.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_verify_key.py'
    # Reconstruct real Xenos BIN-predicated commands and preserve shader
    # microcode, nested indirect draws and GPU register write boundaries.
    # These are retail renderer corrections, not tests shipped with the game.
    Invoke-HulkNativeXPatch 'patch_native_x_hulk_bin_predication.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_guest_memory_bounds.py'
    # Xenos SET_CONSTANT and LOAD_ALU_CONSTANT execute in PM4 packet order:
    # do not overwrite later fetch descriptors with earlier shadow values.
    Invoke-HulkNativeXPatch 'patch_native_x_hulk_register_order.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_gap_shader_snapshot.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_gap_register_snapshot.py'
    # Do not Present pending Vulkan frames while the window has no client area.
    Invoke-HulkNativeXPatch 'patch_native_x_hulk_minimized_pending_swap.py'
    # Reject impossible Vulkan images before allocating GPU memory:
    # log176's 1280x720 cube texture cannot be created by Vulkan.
    # Keep the game data and NativeX missing-texture fallback unchanged.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_texture_resource_guard.py'
    if (-not (Get-Content -LiteralPath (Join-Path $NxrFull 'src/guest_textures.cpp') -Raw).Contains('HULK/TEXTURE-RESOURCE-GUARD')) {
      throw 'Hulk Vulkan invalid resource descriptor guard missing.'
    }
    # Validate resolved Vulkan textures as well as the distinct guest-texture path.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_resolved_resource_guard.py'
    if (-not (Get-Content -LiteralPath (Join-Path $NxrFull 'src/targets.cpp') -Raw).Contains('HULK/RESOLVE-RESOURCE-GUARD')) {
      throw 'Hulk resolved Vulkan descriptor guard missing.'
    }
    # Crash #57: validate generated shader bytecode shape before the NVIDIA
    # Vulkan driver and correlate shader/pipeline creation boundaries by hash.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_spirv_compiler_guard.py'
    if (-not (Get-Content -LiteralPath (Join-Path $NxrFull 'src/shaders.cpp') -Raw).Contains('HULK/NVIDIA-COMPILE-BOUNDARY')) {
      throw 'Hulk shader creation boundary and SPIR-V validator not in compiled NativeX.'
    }
    # Build 67 evidence: scene shaders/pipelines were skipped 1297/1098 times
    # by frame 1200. Reuse D101's bounded wait for the real world pass too.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_scene_pipeline_wait.py'
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_scene_wait_budget.py'
    # Log 185: no draw with invalid shader-used Xenos texture/vertex fetch.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_draw_fetch_guard.py'
    # Actual NativeX GPU-memory upload fingerprints establish archive mip identity
    # before any future replacement, with zero rendering changes by default.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_live_texture_fingerprint.py'
    # Hulk 185 NVIDIA nvgpucomp64.dll crash containment (optional, one shared host mutex).
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_driver_compile_serial.py'
    # Do not apply incomplete XDK PM4 packet state to a native Draw.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_incomplete_xdk_draw.py'
    # Capture actual shader origins when GPU verifier reports 49 PS mismatches (log192).
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_ps_program_origin.py'
    # Shader cache stores first origin; capture the latest *executed* PS load separately.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_ps_recent_load.py'
    # Upstream D139, reviewed for Hulk: memoize only byte-identical
    # owned non-immediate IM_LOAD. Avoid D140 command-queue ABI migration.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_d139_shader_memo.py'
    # Log194: the XDK Draw ranges are generally valid, but 130k draws were
    # rejected as incomplete; report first exact PM4 failure site and header.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_pm4_first_failure.py'
    # #211: authentic PM4 gap draw state comes from ONE validated first pass,
    # never from recycled XDK command memory on a second scan.
    Invoke-HulkNativeXCheckedPatch 'patch_native_x_hulk_gap_owned_draws.py'
    # Preserve bounded, reviewed NativeX pipeline scheduling. The old
    # unhooked wait-all probe was unused and has been removed from main.
    # Do not introduce per-Draw multi-second waits or unverified branches.
  }
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_gen_native_x_hooks_probe.py')) $Source
  Invoke-Checked 'python' @((Join-Path (Split-Path $Source -Parent) 'ci/test_hulk_present_hook_profile.py')) $Source
  # Compile and run standalone input tests before the larger game build.
  $TestDir = Join-Path $Source 'out\input-tests'
  $RequireNativeProfile = if ($NativeXLink) { 'ON' } else { 'OFF' }
  Invoke-Checked 'cmake' @('-S', (Join-Path $Source 'tests'), '-B', $TestDir, '-G', 'Ninja',
    '-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_BUILD_TYPE=Release',
    "-DHULK_REQUIRE_NATIVE_PROFILE_TEST=$RequireNativeProfile") $Source
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
      throw "Pinned NativeX D140 DiligentCore checkout is incomplete: $NxrFull"
    }
    $NativeXAtConfigure = (& git -C $NxrFull rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $NativeXAtConfigure -ne $NativeXCommit) {
      throw 'NativeX checkout changed after the reviewed patch chain; refusing CMake configure.'
    }
    $cmakeArgs += @("-DREXSDK_DIR=$Sdk", '-DHULK_WITH_NATIVE_X=ON',
      "-DHULK_NATIVE_X_SOURCE_DIR=$NxrFull", '-DREXGLUE_USE_VULKAN=ON',
      '-DREXGLUE_USE_D3D12=OFF', '-DSDL_RENDER=ON',
      '-DSDL_JOYSTICK=OFF', '-DSDL_GAMEPAD=OFF', '-DSDL_VULKAN=ON')
  } else {
    $cmakeArgs += @("-DCMAKE_PREFIX_PATH=$SdkInstall", '-DHULK_WITH_NATIVE_X=OFF')
  }
  Invoke-Checked 'cmake' $cmakeArgs $Source
  Invoke-Checked 'python' @($CpuAudit, (Join-Path $BuildDir 'compile_commands.json'), $Source) $Source
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

