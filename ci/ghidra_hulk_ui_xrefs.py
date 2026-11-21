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
import struct
import zlib
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

# These names/CRC32(ASCII_UPPER(name)) pairs are verified against the
# owner's original PC and Xbox 360 index.ind records. The proprietary
# resources themselves are NEVER exported. See the source-only map.
# UIPage_* event names below are not assumed to share this hash table.
VERIFIED_UI_RESOURCE_HASHES = (
    ("Fonts", "font_clean", 0xA796ECFA),
    ("Textures", "ui_menuitems_da", 0x2F95A2B1),
    ("Textures", "ui_menubands_da", 0x20CE9F3C),
    ("Textures", "ui_key_wasd_da", 0x6274200A),
    ("Textures", "ui_key_space_da", 0xD813EA9A),
    ("Textures", "ui_mouse_leftbutton_da", 0x79FFD3E8),
    ("Textures", "ui_mouse_wheelforward_da", 0x672B004B),
    ("Textures", "ui_xbox360_a_da", 0x16FEB6A7),
    ("Textures", "ui_xbox360_b_da", 0x044B1949),
    ("Textures", "ui_xbox360_x_da", 0x3BEFAEB2),
    ("Textures", "ui_xbox360_y_da", 0x8353C9D7),
    ("Textures", "ui_xbox360_rt_da", 0x063884F1),
    ("Textures", "ui_xbox360_lt_da", 0xD9E8AD12),
)


def search_exact_binary(program, needle, limit=MAX_MATCHES):
    """Bounded binary pattern scan; works with Ghidra's signed Java bytes.

    A matching 32-bit constant is ONLY an address-level candidate, never
    proof of a menu dispatcher or of the PPC ABI.
    """
    memory = program.getMemory()
    pattern = array([ord(c) if ord(c) < 128 else ord(c) - 256
                     for c in needle], "b")
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
            if len(found) >= limit:
                return found
            next_address = hit.add(1)
    return found


def identifier_byte_signatures(identifier):
    """Source UI names may be ASCII or UTF-16 in either Xbox/PC byte order.

    Name matches are candidates: no menu callback or PPC ABI is inferred.
    """
    return (("ASCII", identifier.encode("ascii")),
            ("UTF-16LE", identifier.encode("utf-16-le")),
            ("UTF-16BE", identifier.encode("utf-16-be")))


def search_exact_ascii(program, needle):
    """Legacy ASCII-only scan for callers needing this exact convention."""
    return search_exact_binary(program, needle)


def search_identifier_encodings(program, identifier):
    """Scan the exact whitelisted identifier, bounded over all encodings."""
    matches = []
    seen = set()
    for encoding, needle in identifier_byte_signatures(identifier):
        remaining = MAX_MATCHES - len(matches)
        if remaining <= 0:
            break
        for address in search_exact_binary(program, needle, remaining):
            key = (str(address), encoding)
            if key not in seen:
                seen.add(key)
                matches.append((address, encoding))
    return matches


def export_item(name, program):
    refs = program.getReferenceManager()
    funcs = program.getFunctionManager()
    matches = []
    for address, encoding in search_identifier_encodings(program, name):
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
            "string_encoding": encoding,
            "references": found,
            "refs_truncated": len(found) == MAX_REFS,
        })
    return {
        "identifier": name,
        "occurrences": matches,
        "hits_truncated": len(matches) == MAX_MATCHES,
        "name_found": bool(matches),
    }


def resource_hash_candidates(program):
    """Find referenced EoR resource IDs in both executable byte orders.

    Validated archive index hashes are distinct from *verified runtime*
    object owners. Results must be correlated with original guest code.
    """
    candidates = []
    refs = program.getReferenceManager()
    funcs = program.getFunctionManager()
    for group, name, expected_crc in VERIFIED_UI_RESOURCE_HASHES:
        actual = zlib.crc32(name.upper()) & 0xFFFFFFFF
        if actual != expected_crc:
            raise ValueError("Original EoR index hash mismatch: " + name)
        locations = []
        for endian, order in ((">I", "big"), ("<I", "little")):
            for addr in search_exact_binary(
                    program, struct.pack(endian, expected_crc), 96):
                monitor.checkCancelled()
                links = []
                for ref in refs.getReferencesTo(addr):
                    frm = ref.getFromAddress()
                    fn = funcs.getFunctionContaining(frm)
                    links.append({
                        "from_address": str(frm),
                        "function_entry": (str(fn.getEntryPoint())
                                           if fn else None),
                        "function_name": (str(fn.getName())
                                          if fn else None),
                        "reference_type": str(ref.getReferenceType()),
                    })
                    if len(links) >= MAX_REFS:
                        break
                locations.append({
                    "constant_address": str(addr),
                    "constant_byte_order": order,
                    "references": links,
                    "refs_truncated": len(links) >= MAX_REFS,
                })
        candidates.append({
            "group": group,
            "canonical_name": name,
            "verified_original_index_hash": "%08X" % expected_crc,
            "occurrences": locations,
            "candidate_only": True,
            "proves_menu_callback_or_guest_abi": False,
        })
    return candidates


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
        "original_ui_hash_constant_candidates": resource_hash_candidates(currentProgram),
        "hash_reference_is_not_ui_dispatch_proof": True,
    }
    folder = os.path.dirname(target)
    if not os.path.isdir(folder):
        os.makedirs(folder)
    with open(target, "w") as stream:
        json.dump(result, stream, sort_keys=True, indent=2)
    print("HULK/EOR-UI-XREF: wrote %d known-name records to private report" %
          len(result["identifiers"]))


main()
