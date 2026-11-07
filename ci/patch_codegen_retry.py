#!/usr/bin/env python3
"""Install bounded ReXGlue PPC codegen recovery.

Patches the existing BUILD_WINDOWS.ps1 in place.
"""

from __future__ import annotations

import pathlib
import sys

OLD = (
    "  Invoke-Checked $Codegen @('codegen', "
    "(Join-Path $Source 'incredible_hulk_manifest.toml')) $Source"
)

MARKER = "# HULK_REXGLUE_RECOVERY_V2"

NEW = r'''
  # HULK_REXGLUE_RECOVERY_V2
  $Manifest = Join-Path $Source 'incredible_hulk_manifest.toml'
  $Missing = Join-Path $Source 'missing_funcs.toml'
  $Indirect = Join-Path $Source 'indirect_funcs.toml'
  $MissingTool = Join-Path $Source 'tools/missing_funcs.py'
  $RepairTool = Join-Path (Split-Path $Source -Parent) 'ci/repair_codegen_overlaps.py'
  $LogDir = Join-Path $Source 'runs/codegen_ci'

  New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

  foreach ($required in @($Manifest, $Missing, $Indirect, $MissingTool)) {
    if (!(Test-Path -LiteralPath $required)) {
      throw "Required ReXGlue input missing: $required"
    }
  }

  function Get-DiscoveryState {
    $a = (Get-FileHash -LiteralPath $Missing -Algorithm SHA256).Hash
    $b = (Get-FileHash -LiteralPath $Indirect -Algorithm SHA256).Hash
    return "$a`:$b"
  }

  $MaxAttempts = 12
  $Succeeded = $false
  $SeenStates = New-Object 'System.Collections.Generic.HashSet[string]'

  for ($Attempt = 1; $Attempt -le $MaxAttempts; $Attempt++) {
    $before = Get-DiscoveryState

    if (-not $SeenStates.Add($before)) {
      throw "ReXGlue discovery cycle detected; no further progress."
    }

    $name = 'codegen_{0:D2}' -f $Attempt
    $stdout = Join-Path $LogDir "$name.stdout.log"
    $stderr = Join-Path $LogDir "$name.stderr.log"
    $merged = Join-Path $LogDir "$name.log"

    Write-Host "ReXGlue codegen pass $Attempt/$MaxAttempts"

    $proc = Start-Process `
      -FilePath $Codegen `
      -ArgumentList @('codegen', ('"{0}"' -f $Manifest)) `
      -WorkingDirectory $Source `
      -Wait -PassThru -NoNewWindow `
      -RedirectStandardOutput $stdout `
      -RedirectStandardError $stderr

    $lines = @(
      Get-Content -LiteralPath $stdout, $stderr `
        -ErrorAction SilentlyContinue
    )
    $lines | Set-Content -LiteralPath $merged -Encoding UTF8

    if ($proc.ExitCode -eq 0) {
      $Succeeded = $true
      Write-Host "ReXGlue codegen succeeded on pass $Attempt"
      break
    }

    $exitCode = [uint32]($proc.ExitCode -band 0xFFFFFFFFL)
    $hexExit = '0x{0:X8}' -f $exitCode
    $output = Get-Content -LiteralPath $merged -Raw

    Write-Host "Codegen exit: $hexExit"

    if ($exitCode -eq 0xC0000409) {
      Get-Content -LiteralPath $merged -Tail 80
      throw "ReXGlue native crash 0xC0000409; refusing speculative stub recovery."
    }
    elseif ($output -match '\[config\] Overlapping boundaries:') {
      & python $RepairTool $merged $Missing $Indirect
      if ($LASTEXITCODE -ne 0) { throw "PPC overlap repair failed" }
    }
    elseif ($output -match 'no CallTarget in FunctionNode|Unresolved function 0x[0-9A-Fa-f]+ from') {
      throw "ReXGlue missing FunctionGraph CallEdge; SDK emitter patch required. See $merged"
    }
    elseif (
      $output -match 'Unresolved conditional branch to' -or
      $output -match 'target not in any function' -or
      $output -match 'unregistered function at guest address' -or
      $output -match 'has no blocks - generating stub'
    ) {
      & python $MissingTool $merged
      if ($LASTEXITCODE -ne 0) { throw "PPC function discovery failed" }
    }
    else {
      Get-Content -LiteralPath $merged -Tail 60
      throw "Unhandled ReXGlue error $hexExit"
    }

    $after = Get-DiscoveryState
    if ($after -eq $before) {
      throw "Recovery did not change PPC manifests. Stopping."
    }
  }

  if (-not $Succeeded) {
    throw "ReXGlue codegen did not succeed after $MaxAttempts passes."
  }
'''


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: patch_codegen_retry.py native_port/BUILD_WINDOWS.ps1", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    if not path.is_file():
        print(f"Missing build script: {path}", file=sys.stderr)
        return 1

    source = path.read_text(encoding="utf-8-sig")

    if MARKER in source:
        print("ReXGlue recovery V2 already installed.")
        return 0

    if source.count(OLD) != 1:
        print("Original codegen invocation not found. Refusing unsafe patch.", file=sys.stderr)
        return 1

    updated = source.replace(OLD, NEW.strip("\n"))
    path.write_text(updated, encoding="utf-8", newline="\n")

    print(f"ReXGlue recovery V2 installed: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
