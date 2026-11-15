#!/usr/bin/env python3
"""Audit the *actual* ReXGlue recomp SDK platform implementations used by Hulk.

Evidence is implementation code or explicit stub exports. Source presence is
not proof that the Hulk USA XEX calls a particular service or that gameplay
works. No game bytes or proprietary symbols are collected.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

# Exact recomp source-level evidence, no speculative Hulk guest addresses.
FEATURES = {
    "audio_xma_decoder": [
        ("src/audio/xma_context.cpp", "avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES)"),
        ("src/audio/xma_decoder.cpp", "XmaDecoder::WorkerThreadMain()"),
    ],
    "audio_sdl_host": [
        ("src/audio/sdl/sdl_audio_driver.cpp", "SDL_OpenAudioDeviceStream("),
    ],
    "audio_wasapi_exclusive_host": [
        ("src/audio/wasapi/wasapi_exclusive_driver.cpp", "IAudioClient"),
    ],
    "xbox_game_vfs": [
        ("src/filesystem/virtual_file_system.cpp", "VirtualFileSystem::ResolvePath("),
        ("src/filesystem/virtual_file_system.cpp", "VirtualFileSystem::RegisterSymbolicLink("),
    ],
    "xam_content_manager": [
        ("src/system/xam/content_manager.cpp", "ContentManager::ResolvePackageRoot("),
        ("src/kernel/xam/xam_content.cpp", "XamContentCreate_entry("),
    ],
    "kernel_host_threads": [
        ("src/kernel/xboxkrnl/xboxkrnl_threading.cpp", "ExCreateThread_entry("),
        ("src/kernel/xboxkrnl/xboxkrnl_threading.cpp", "KeSetAffinityThread_entry("),
    ],
}

IMPORT_STUB = re.compile(r"REX_EXPORT_STUB\(__imp__(\w+)\s*\)")
IMPORT_IMPL = re.compile(r"REX_EXPORT\(__imp__(\w+)\s*,")
MAX_SOURCE = 2 * 1024 * 1024

def read_sources(sdk: Path) -> dict[str, str]:
    paths = sorted({relative for probes in FEATURES.values() for relative, _ in probes} |
                   {"src/kernel/xam/xam_content.cpp"})
    result = {}
    for path in paths:
        file = sdk / path
        if not file.is_file() or file.stat().st_size > MAX_SOURCE:
            result[path] = ""
            continue
        result[path] = file.read_text(encoding="utf-8-sig", errors="replace")
    return result

def audit_sdk(sdk: Path) -> dict:
    sources = read_sources(sdk)
    features = {}
    for name, requirements in FEATURES.items():
        checks = [{"source": source, "marker": marker,
                   "present": marker in sources[source]}
                  for source, marker in requirements]
        features[name] = {
            "source_implementation_present": all(x["present"] for x in checks),
            "checked": checks,
            "verified_in_hulk_gameplay": False,
        }
    xam = sources["src/kernel/xam/xam_content.cpp"]
    implemented = sorted(set(IMPORT_IMPL.findall(xam)))
    stubbed = sorted(set(IMPORT_STUB.findall(xam)))
    if set(implemented) & set(stubbed):
        raise ValueError("XAM exported method has both implementation and stub")
    # This is exported with a body, but returns an unimplemented warning.
    partial = sorted(method for method in implemented
                     if method == "XamContentResolve" and
                     "XamContentResolve unimplemented!" in xam)
    return {
        "schema": "hulk_host_service_sdk_audit_v1",
        "target": "ReXGlue recomp source (not original Hulk guest import table)",
        "services": features,
        "xam_content": {
            "exported_with_body": implemented,
            "explicit_stub_exports": stubbed,
            "exported_but_body_incomplete": partial,
            "game_reachability_proved": False,
        },
        "source_sha256": {
            name: hashlib.sha256(value.encode("utf-8")).hexdigest()
            for name, value in sources.items() if value
        },
        "conclusions": {
            "xma_reimplementation_required": False
                if features["audio_xma_decoder"]["source_implementation_present"] else None,
            "native_audio_playback_verified": False,
            "xam_save_roundtrip_verified": False,
            "original_windows_hulk_save_format_verified": False,
            "xbox_game_path_roundtrip_verified": False,
            "physics_recompiled_and_60fps_verified": False,
            "vmx128_equivalence_verified": False,
            "xex_service_call_reachability_verified": False,
        },
        "note": ("Source implementation does not imply active/imported or "
                 "correct in-game behavior. No Havok ABI or timing claim. "
                 "Hulk's existing PC/Xbox saves differ in endianness/layout."),
    }

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("sdk", type=Path)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    report = audit_sdk(args.sdk)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("[HULK/HOST-SERVICES] source implementations:",
          ", ".join(name for name, check in report["services"].items()
                    if check["source_implementation_present"]) or "none")
    print("[HULK/HOST-SERVICES] XAM stubs:",
          len(report["xam_content"]["explicit_stub_exports"]),
          "incomplete exports:",
          ", ".join(report["xam_content"]["exported_but_body_incomplete"]) or "none")
    print("[HULK/HOST-SERVICES] Runtime audio, game save and Havok/60Hz remain UNVERIFIED")

if __name__ == "__main__":
    main()
