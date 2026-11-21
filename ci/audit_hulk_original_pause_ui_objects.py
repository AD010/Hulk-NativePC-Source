#!/usr/bin/env python3
"""Inspect owner-supplied EoR original UI object archives without copying code/assets.

The result is a source-name/method route witness, NOT a PowerPC address,
callable ABI, event dispatcher, or permission to redirect the guest page.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import zlib

PAUSE_SEQUENCE = (
    'OptionsMode_Pause', 'UIPage_Pause_Settings', 'OptionsMenuState',
    'CurrentStatusMenu', 'UpgradeMenu', 'Settings_Page',
    'uiwidget_pausemenu_z', 'ChangeState_r', 'Init_z',
    'ActivateUpgradeMenu_z', 'ActivateCurrentStatusMenu_z',
    'StateEnter_z', 'StateExit_z', 'NextTabEvent_z',
    'PreviousTabEvent_z',
)
SETTINGS_SCRIPT = 'uipage_pause_gamesettings_z'
GAME_ACTIONS = ('UI_ActivateOptions', 'UI_OptionScreen')
# Names found in the owner's ORIGINAL UI serialized object/QuickData symbols.
# This bounded allowlist yields exact source VM references, not raw script
# bytecode, guessed native PPC call targets or shader resource bindings.
PAUSE_UI_FIELD_NAMES = (
    'inputDeviceListener_Pause', 'BottomBand', 'AcceptText', 'BackText',
    'ScrollText', 'UISounds', 'HorizontalSound', 'VerticalSound',
    'LAnalogPolling_z', 'Hidden', 'CurrentStatusMenu', 'vZero',
    'MovesUpgradesText', 'NavNextText', 'NavPreviousText', 'NavCurrentText',
    'UISystem_Pause_CurrentStatus', 'TextID', 'hudman',
    'SetCanReturnToCity_b',
)
PAUSE_UI_FIELD_METHODS = (
    'Init_z', 'StateEnter_z', 'StateExit_z', 'ActivateUpgradeMenu_z',
)


def one(data: bytes, name: str) -> int:
    key = name.encode('ascii') + b'\0'
    at = data.find(key)
    if at < 0 or data.find(key, at + len(key)) >= 0:
        raise ValueError('Missing or nonunique original UI anchor: ' + name)
    return at


def source_method(data: bytes, at: int, name: str, endian: str) -> tuple[int, bytes]:
    """Check real EoR script method size and original CRC32(ASCII_UPPER).

    This is a VM script payload, NOT a native guest PowerPC function.
    """
    start = at + len(name) + 1
    if start + 8 > len(data):
        raise ValueError('Truncated EoR method header: ' + name)
    length = int.from_bytes(data[start:start + 4], endian)
    end = start + 4 + length
    if not 1 <= length <= 4096 or end + 4 > len(data):
        raise ValueError('Invalid original UI VM method range: ' + name)
    footer = int.from_bytes(data[end:end + 4], endian)
    if footer != zlib.crc32(name.upper().encode('ascii')) & 0xffffffff:
        raise ValueError('Original UI VM method CRC mismatch: ' + name)
    return length, data[start + 4:end]


def original_pause_ui_field_refs(data: bytes, platform: str) -> dict:
    """Recover VM field-hash usages from ORIGINAL Xbox/PC pause scripts.

    The actual method payload sizes and exact 32-bit hashes are verified
    independently in both platform byte orders. Values from original
    script bytecode never leave memory: output is only public-style names,
    relative offsets and 32-bit CRC32(ASCII_UPPER) identifiers.
    """
    if platform not in ('xbox_360_source', 'pc_2008_comparator'):
        raise ValueError('Unknown original EoR platform')
    endian = 'big' if platform == 'xbox_360_source' else 'little'
    anchor = one(data, 'UIPage_Pause_Settings')
    hashes = {}
    for name in PAUSE_UI_FIELD_NAMES:
        hashed = zlib.crc32(name.upper().encode('ascii')) & 0xffffffff
        if hashed in hashes:
            raise ValueError('Ambiguous original UI field CRC32')
        hashes[hashed] = name
    output = {}
    for method in PAUSE_UI_FIELD_METHODS:
        at = data.find(method.encode('ascii') + b'\0',
                       anchor, min(len(data), anchor + 2400))
        if at < 0:
            raise ValueError('Missing actual original pause VM method: ' + method)
        begin = at + len(method) + 1
        if begin + 8 > len(data):
            raise ValueError('Truncated original pause script')
        size = int.from_bytes(data[begin:begin + 4], endian)
        if not 1 <= size <= 1024 or begin + size + 8 > len(data):
            raise ValueError('Invalid original pause VM payload boundary')
        payload = data[begin + 4:begin + 4 + size]
        references = []
        for offset in range(size - 3):
            identity = int.from_bytes(payload[offset:offset + 4], endian)
            name = hashes.get(identity)
            if name is not None:
                references.append({
                    'vm_byte_offset': offset,
                    'field_name': name,
                    'source_crc32_upper': '{:08X}'.format(identity),
                })
        output[method] = {
            'original_vm_payload_bytes': size,
            'source_field_references': references,
        }
    return output


def inspect_objects(data: bytes, platform: str) -> dict:
    anchor = one(data, 'UIPage_Pause_Settings')
    start, end = max(anchor - 256, 0), min(anchor + 2400, len(data))
    scope = data[start:end]
    positions = {}
    # Some methods, like StateEnter_z, occur more than once in one object;
    # use the earliest occurrence after the previous ordered source symbol.
    last = -1
    for name in PAUSE_SEQUENCE:
        key = name.encode('ascii') + b'\0'
        at = scope.find(key, last + 1)
        if at < 0:
            raise ValueError('Incomplete original pause menu route: ' + name)
        if name == 'UIPage_Pause_Settings' and start + at != anchor:
            raise ValueError('Pause page belongs to a different object')
        positions[name] = start + at - anchor
        last = at
    # Script methods have BE length/immediates on Xbox and LE on PC.
    # Verify their actual serialized method footer name hashes, not just
    # nearby text labels. Do not publish source VM opcode buffers.
    endian = 'big' if platform == 'xbox_360_source' else 'little'
    if platform not in ('xbox_360_source', 'pc_2008_comparator'):
        raise ValueError('Unknown EoR archive endianness')
    method_lengths = {}
    change_state_operand = None
    for name in ('uiwidget_pausemenu_z', 'ChangeState_r', 'Init_z',
                 'ActivateUpgradeMenu_z', 'ActivateCurrentStatusMenu_z'):
        size, payload = source_method(data, anchor + positions[name], name, endian)
        method_lengths[name] = size
        if name == 'ChangeState_r':
            # 0x28 is an original VM bytecode word with an endian-specific
            # 32-bit operand, not a proven native C++/PPC function call.
            if size != 13 or payload[4] != 0x28:
                raise ValueError('Unrecognized original ChangeState_r VM bytecode')
            change_state_operand = '{:08X}'.format(
                int.from_bytes(payload[5:9], endian))
    # The immediate used by ChangeState_r is a SHARED VM operation:
    # in the owner's full Xbox/PC archives the exact 0x28+operand sequence
    # also occurs in unrelated HUD, frontend, and AI script methods.
    # Count these actual serialized sites so it can never be mislabelled
    # a unique Pause Settings handler or installed as a PPC hook.
    common_vm_word = bytes([0x28]) + int(
        change_state_operand, 16).to_bytes(4, endian)
    shared_vm_uses = data.count(common_vm_word)
    if not shared_vm_uses:
        raise ValueError('Original ChangeState VM operation absent from archive')
    # The settings script is a distinct original VM object, not a guest
    # PowerPC handler. Its Init_z must belong to the same local object span.
    settings_at = one(data, SETTINGS_SCRIPT)
    method_at = data.find(b'Init_z\0', settings_at, min(len(data), settings_at + 256))
    if method_at < 0:
        raise ValueError('Missing original game-settings VM Init_z')
    return {
        'platform_label': platform,
        'source_sha256': hashlib.sha256(data).hexdigest(),
        'pause_route_offsets_from_page': positions,
        'game_settings_script_present': True,
        'game_settings_init_relative': method_at - settings_at,
        'pause_vm_method_payload_lengths': method_lengths,
        'change_state_vm_opaque_0x28_operand': change_state_operand,
        'shared_opcode_0x28_vm_uses': shared_vm_uses,
        'opaque_operand_shared_across_unrelated_scripts': shared_vm_uses > 16,
        'opaque_operand_is_pause_specific': False,
        'opaque_operand_is_verified_guest_function': False,
        'verified_original_serialized_ui_symbols': True,
        'guest_ppc_event_handler_abi_verified': False,
        'native_windows_pause_menu_binding_installed': False,
    }


def inspect_actions(data: bytes) -> dict:
    first = one(data, GAME_ACTIONS[0]); second = one(data, GAME_ACTIONS[1])
    if not 0 < second - first <= 80:
        raise ValueError('Original UI option action names are not colocated')
    return {
        'source_sha256': hashlib.sha256(data).hexdigest(),
        'original_action_symbol_order': list(GAME_ACTIONS),
        'relative_string_distance': second - first,
        'action_to_guest_function_abi_verified': False,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbox-objects', type=Path, required=True)
    parser.add_argument('--pc-objects', type=Path)
    parser.add_argument('--quick-data', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    xbox_data = args.xbox_objects.read_bytes()
    xbox = inspect_objects(xbox_data, 'xbox_360_source')
    xbox['pause_ui_field_hash_references'] = original_pause_ui_field_refs(
        xbox_data, 'xbox_360_source')
    result = {'schema': 'hulk_original_eor_pause_ui_source_witness_v1',
              'xbox': xbox,
              'evidence_kind': 'verified_serialized_original_ui_metadata_only',
              'not_runnable_original_game_handler': True}
    if args.pc_objects:
        pc_data = args.pc_objects.read_bytes()
        pc = inspect_objects(pc_data, 'pc_2008_comparator')
        pc['pause_ui_field_hash_references'] = original_pause_ui_field_refs(
            pc_data, 'pc_2008_comparator')
        if (pc['pause_ui_field_hash_references'] !=
                xbox['pause_ui_field_hash_references']):
            raise ValueError('Original Xbox/PC Pause UI VM field ABI disagrees')
        if list(pc['pause_route_offsets_from_page']) != list(xbox['pause_route_offsets_from_page']):
            raise ValueError('PC/Xbox original pause script symbol order differs')
        if (pc['pause_vm_method_payload_lengths'] !=
                xbox['pause_vm_method_payload_lengths'] or
                pc['change_state_vm_opaque_0x28_operand'] !=
                xbox['change_state_vm_opaque_0x28_operand']):
            raise ValueError('PC/Xbox original pause script VM method ABI differs')
        result['pc'] = pc
    if args.quick_data:
        result['quick_data'] = inspect_actions(args.quick_data.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    print('[HULK/ORIGINAL-PAUSE] original script/UI object routes found; '
          'guest PPC callback ABI NOT proven')


if __name__ == '__main__':
    main()
