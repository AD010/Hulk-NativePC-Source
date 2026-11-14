#@category Hulk.ReverseEngineering
# Original Hulk 2008 USA camera / projection / pause menu evidence collector.
# Ghidra Jython only. Run locally against SHA-verified USA default.xex
# (XEXLoaderWV), and separately the official 2008 PC Hulk.exe.
#
# analyzeHeadless PRIVATE_PROJECT HulkUSA -import default.xex \
#   -scriptPath path/to/Hulk-NativePC-Source/ci \
#   -postScript ghidra_hulk_camera_projection.py PRIVATE/camera_xrefs.json
#
# This exports ONLY candidates and cross-reference addresses, not XEX code,
# shaders, resource data, or guessed camera-write offsets.
from __future__ import print_function
import json
import os
import struct
from jarray import array

KNOWN_UI = (
    "UIPage_Pause_Settings",
    "UIPage_Pause_CameraSettings",
    "UI_ActivateOptions",
    "UI_OptionScreen",
    "UI_OptionsReturn",
    "SetResolution_ii",
    "GetNextResolution_z",
    "GetPrevResolution_z",
    "Options_SetCameraYaw_b",
    "Options_SetCameraPitch_b",
)
CAMERA_HINTS = (
    "camera", "view", "projection", "perspective", "frustum",
    "fov", "fieldofview", "aspect", "pause", "options",
)
# Original 16:9, 4:3 and 16:9 inverse; big-endian PPC float values
# in DATA only. A match is NOT proof of camera instructions or a hook.
SCALAR_HINTS = (
    ("sixteen_by_nine", 16.0 / 9.0),
    ("four_by_three", 4.0 / 3.0),
    ("inverse_sixteen_by_nine", 9.0 / 16.0),
)
MAX_OCCURRENCES = 96
MAX_REFERENCES = 96
MAX_FUNCTIONS = 256

def byte_pattern(ascii_or_bytes):
    if isinstance(ascii_or_bytes, str):
        items = [ord(c) for c in ascii_or_bytes]
    else:
        items = [ord(c) for c in ascii_or_bytes]
    return array([c if c < 128 else c - 256 for c in items], "b")

def search_initialized(needle):
    found = []
    memory = currentProgram.getMemory()
    pattern = byte_pattern(needle)
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        end = block.getEnd()
        cursor = block.getStart()
        while cursor is not None and cursor.compareTo(end) <= 0:
            monitor.checkCancelled()
            match = memory.findBytes(cursor, pattern, None, True, monitor)
            if match is None or match.compareTo(end) > 0:
                break
            found.append(match)
            if len(found) >= MAX_OCCURRENCES:
                return found
            cursor = match.add(1)
    return found

def refs_to(addr):
    refs = currentProgram.getReferenceManager()
    manager = currentProgram.getFunctionManager()
    result = []
    for ref in refs.getReferencesTo(addr):
        frm = ref.getFromAddress()
        fn = manager.getFunctionContaining(frm)
        result.append({
            "from": str(frm),
            "function_entry": str(fn.getEntryPoint()) if fn else None,
            "function_name": str(fn.getName()) if fn else None,
            "reference_type": str(ref.getReferenceType()),
        })
        if len(result) >= MAX_REFERENCES:
            break
    return result

def strings_with_references():
    result = []
    for value in KNOWN_UI:
        occurrence = []
        for addr in search_initialized(value):
            occurrence.append({"address": str(addr), "references": refs_to(addr)})
        result.append({"identifier": value, "locations": occurrence})
    return result

def float_references():
    output = []
    for label, value in SCALAR_HINTS:
        signature = struct.pack(">f", value)
        occurrence = []
        for addr in search_initialized(signature):
            # Raw float patterns may be ordinary data, even if there
            # are references. Never infer M00, row/column major, ABI.
            occurrence.append({"address": str(addr), "references": refs_to(addr)})
        output.append({"hint": label, "locations": occurrence})
    return output

def function_name_hints():
    manager = currentProgram.getFunctionManager()
    result = []
    for fn in manager.getFunctions(True):
        monitor.checkCancelled()
        name = str(fn.getName())
        if any(hint in name.lower() for hint in CAMERA_HINTS):
            result.append({
                "name": name,
                "entry": str(fn.getEntryPoint()),
                "is_thunk": bool(fn.isThunk()),
            })
            if len(result) >= MAX_FUNCTIONS:
                break
    return result

def main():
    args = getScriptArgs()
    if len(args) != 1:
        raise ValueError("One PRIVATE report output path required")
    output_path = os.path.abspath(args[0])
    report = {
        "target": str(currentProgram.getName()),
        "processor": str(currentProgram.getLanguageID()),
        "image_base": str(currentProgram.getImageBase()),
        "original_usa_xex_verified_by_this_script": False,
        "candidate_addresses_only": True,
        "abi_proven": False,
        "native_16_10_camera_hook_enabled": False,
        "pause_menu_hook_enabled": False,
        "functions_with_name_hints": function_name_hints(),
        "ui_event_xrefs": strings_with_references(),
        "projection_scalar_xrefs": float_references(),
        "interpretation": [
            "Verify XEX SHA, PPC function arguments, register save/restore and object lifetime.",
            "Distinguish a 3D view projection from shadow, reflection, UI and movie matrices.",
            "Confirm original camera FOV semantics and update exactly once per view.",
            "Only then enable native screen fill and preserve HUD aspect independently.",
        ],
    }
    folder = os.path.dirname(output_path)
    if folder and not os.path.isdir(folder):
        os.makedirs(folder)
    with open(output_path, "w") as fp:
        json.dump(report, fp, sort_keys=True, indent=2)
    print("HULK/USA-CAMERA: %d UI identifiers; %d projection constants; "
          "no guest ABI claimed" % (len(KNOWN_UI), len(SCALAR_HINTS)))

main()
