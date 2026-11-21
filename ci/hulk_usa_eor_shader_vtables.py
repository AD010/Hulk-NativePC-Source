#!/usr/bin/env python3
"""Source-only identity audit of real SHA-checked USA mapped guest PPC image.

The original Xbox executable memory image is a private user input.
Outputs only original shader class names/vtable addresses and guest method
entrypoint metadata; NEVER original instructions, buffers or game content.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path

USA_MAPPED_PPC_SHA256 = (
    "fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386"
)
GUEST_IMAGE_BASE = 0x82000000
GUEST_TEXT_LO = 0x821B2000
GUEST_TEXT_HI = 0x82D61523
VTABLE_METHODS = (0x82643368, 0x826431B0, 0x82643270)
# Exact three adjacent original common class methods in the original
# image's .rdata; following method slots are independently sourced there.
METHOD_SIGNATURE = b"".join(x.to_bytes(4, "big") for x in VTABLE_METHODS)
PUBLIC_WITNESS = (
    "native_port/analysis/hulk_usa_eor_shader_vtables_2026-11-21.json"
)
HIGHLIGHTS = {
    "Decal": (0x82066504, 0x8265B420),
    "TexturedRect": (0x8206EBFC, 0x82668D50),
    "EScreenRectShader": (0x8206ECAC, 0x82669630),
    "EFontShader": (0x8206ED6C, 0x8266A0C0),
    "ESkyShader": (0x8206F5DC, 0x8266CC28),
    "EFlatShadowShader": (0x8206F7CC, 0x8266DDD0),
}


def inspect_private_mapped_image(data: bytes) -> dict:
    if hashlib.sha256(data).hexdigest() != USA_MAPPED_PPC_SHA256:
        raise ValueError("Unknown user-provided mapped USA PPC image")
    if data[:2] != b"MZ" or len(data) < GUEST_TEXT_HI - GUEST_IMAGE_BASE:
        raise ValueError("Not a full original guest PPC memory image")
    def ptr_at(address):
        at = address - GUEST_IMAGE_BASE
        return int.from_bytes(data[at:at + 4], "big")
    results = []
    pos = 0
    while True:
        idx = data.find(METHOD_SIGNATURE, pos, 0xC1800)
        if idx < 0:
            break
        pos = idx + len(METHOD_SIGNATURE)
        start = GUEST_IMAGE_BASE + idx - 4
        if idx % 4 or idx < 4:
            continue
        entries = [ptr_at(start + slot * 4) for slot in range(14)]
        if not all(GUEST_TEXT_LO <= entry < GUEST_TEXT_HI
                   for entry in entries):
            continue
        owner = ""
        owner_at = None
        for slot in (12, 13, 14):
            offset = start + 4 * slot - GUEST_IMAGE_BASE
            match = re.match(rb"([A-Za-z][A-Za-z0-9_]{2,48})\x00",
                             data[offset:offset + 72])
            if match:
                owner = match.group(1).decode("ascii")
                owner_at = start + 4 * slot
                break
        if not owner:
            continue
        results.append({
            "original_shader_class_name": owner,
            "original_guest_vtable": "0x%08X" % start,
            "original_guest_method_slot_4": "0x%08X" % entries[4],
            "original_guest_method_slot_7": "0x%08X" % entries[7],
            "original_class_name_record": "0x%08X" % owner_at,
            "original_shader_vtable_source_verified": True,
            "original_effect_material_sampler_abi_verified": False,
        })
    names = [record["original_shader_class_name"] for record in results]
    if len(results) != 52 or len(set(names)) != len(names):
        raise ValueError("Source original class-vtable corpus is incomplete")
    by_name = {record["original_shader_class_name"]: record
               for record in results}
    for name, (vtable, method) in HIGHLIGHTS.items():
        row = by_name.get(name)
        if not row or (row["original_guest_vtable"] != "0x%08X" % vtable or
                       row["original_guest_method_slot_4"] !=
                       "0x%08X" % method):
            raise ValueError("Original USA shader vtable identity drift: " + name)
    return {
        "schema": "hulk_usa_original_eor_shader_dispatch_v1",
        "original_mapped_guest_image_sha256": USA_MAPPED_PPC_SHA256,
        "guest_image_base": "0x%08X" % GUEST_IMAGE_BASE,
        "vtable_class_count": len(results),
        "original_shader_vtables": results,
        "original_runtime_eor_model_material_abi_verified": False,
        "original_pause_ui_event_dispatch_abi_verified": False,
        "all_guest_code_and_original_assets_remain_private": True,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--private-usa-mapped-image", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    report = inspect_private_mapped_image(args.private_usa_mapped_image.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("[HULK/EOR-CLASS-DISPATCH] 52 authentic USA class vtables; "
          "material/event owner ABI still requires live crosswalk")


if __name__ == "__main__":
    main()
