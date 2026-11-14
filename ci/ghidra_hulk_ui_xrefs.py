#@category Hulk.ReverseEngineering
# Headless Ghidra Script (Jython, works with Ghidra's standard Script API)
# Target: original retail PC HULK.EXE, and separately a correctly loaded
# Xbox 360 USA XEX with XEXLoaderWV. DO NOT copy guest bytes to GitHub.
#
# analyzeHeadless PRIVATE_PROJECT HulkPC -import HULK.EXE \
#   -scriptPath path/to/Hulk-NativePC-Source/ci \
#   -postScript ghidra_hulk_ui_xrefs.py D:/PrivateHulk/pc-ui-xrefs.json
#
# For Xbox, install XEXLoaderWV and import SHA-verified USA default.xex.
# This script exports only whitelisted UI names and address relationships.
from __future__ import print_function

import json
import os
from jarray import array

NAMES = [
    "UIPage_Pause_Settings", "UIPage_Pause_CameraSettings",
    "uipage_pause_gamesettings", "UI_ActivateOptions",
    "UI_OptionScreen", "UI_OptionsReturn",
    "uiwidget_menuselector", "uiwidget_menuitemtxt",
    "uiwidget_menuscrollbar", "font_clean",
    "Options_SetAudioVolumeMusic_i", "Options_SetAudioVolumeSFX_i",
    "Options_SetAudioVolumeDialog_i", "Options_SetSubtitles_b",
    "Options_SetMouseSensitivity_i", "Options_SetCameraYaw_b",
    "Options_SetCameraPitch_b", "SetResolution_ii",
    "GetNextResolution_z", "GetPrevResolution_z",
]
MAX_MATCHES = 256
MAX_REFS = 128


def search_exact_ascii(program, needle):
    """Scan initialized memory (incl. metadata not auto-defined as strings)."""
    memory = program.getMemory()
    pattern = array([ord(c) for c in needle], "b")
    found = []
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        start, end = block.getStart(), block.getEnd()
        next_address = start
        while next_address is not None and next_address.compareTo(end) <= 0:
            monitor.checkCancelled()
            hit = memory.findBytes(next_address, pattern, None, True, monitor)
            if hit is None or hit.compareTo(end) > 0:
                break
            found.append(hit)
            if len(found) >= MAX_MATCHES:
                return found
            next_address = hit.add(1)
    return found


def export_item(name, program):
    refs = program.getReferenceManager()
    funcs = program.getFunctionManager()
    matches = []
    for address in search_exact_ascii(program, name):
        found = []
        iterator = refs.getReferencesTo(address)
        for ref in iterator:
            origin = ref.getFromAddress()
            func = funcs.getFunctionContaining(origin)
            found.append({
                "from_address": str(origin),
                "function_entry": str(func.getEntryPoint()) if func else None,
                "function_name": str(func.getName()) if func else None,
                "reference_type": str(ref.getReferenceType()),
            })
            if len(found) == MAX_REFS:
                break
        matches.append({
            "string_address": str(address),
            "references": found,
            "refs_truncated": len(found) == MAX_REFS,
        })
    return {
        "identifier": name,
        "occurrences": matches,
        "hits_truncated": len(matches) == MAX_MATCHES,
        "name_found": bool(matches),
    }


def main():
    arguments = getScriptArgs()
    if len(arguments) != 1:
        raise ValueError("Pass exactly one PRIVATE output JSON path")
    target = os.path.abspath(arguments[0])
    # No code segment, guest bytes or resource strings are embedded in JSON.
    result = {
        "program": str(currentProgram.getName()),
        "processor": str(currentProgram.getLanguageID()),
        "image_base": str(currentProgram.getImageBase()),
        "derived_from_loaded_ghidra_program": True,
        "no_original_code_or_assets": True,
        "xrefs_are_abi_proof": False,
        "whitelist_only": True,
        "identifiers": [export_item(name, currentProgram) for name in NAMES],
    }
    folder = os.path.dirname(target)
    if not os.path.isdir(folder):
        os.makedirs(folder)
    with open(target, "w") as stream:
        json.dump(result, stream, sort_keys=True, indent=2)
    print("HULK/EOR-UI-XREF: wrote %d known-name records to private report" %
          len(result["identifiers"]))


main()
