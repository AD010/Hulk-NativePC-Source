#!/usr/bin/env python3
"""Read-only XEX2 XDK metadata audit, based on public XEX2 layout descriptions.

References: xenia-project/xenia src/xenia/kernel/util/xex2_info.h,
reverseengineering.wiki/Format:XEX2, landaire/acceleration xex2 crate.
This does NOT extract proprietary Xbox SDK files, reconstruct guest code,
or give a NativeXRenderer device structure by itself.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXECUTION_INFO = 0x00040006
STATIC_LIBRARIES = 0x000200FF
IMPORT_LIBRARIES = 0x000103FF
EXPECTED_TITLE = 0x534507F1


def be32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"XEX offset out of range: {offset:#x}")
    return struct.unpack_from(">I", data, offset)[0]


def inspect(data: bytes) -> dict:
    if len(data) < 0x18 or data[:4] != b"XEX2":
        raise ValueError("Not an Xbox 360 XEX2 file")
    optional_count = be32(data, 0x14)
    if optional_count > 1024 or 0x18 + optional_count * 8 > len(data):
        raise ValueError("Corrupt XEX optional header count")
    pe_offset = be32(data, 0x08)
    security_offset = be32(data, 0x10)
    if not 0x18 + optional_count * 8 <= security_offset < pe_offset <= len(data):
        raise ValueError("Corrupt XEX2 security and PE offsets")
    # A region/format audit must never consume optional-header garbage as
    # signed security metadata. XEX2 security fields are big-endian; the
    # retail PAL file verified in the user's archive has mask 0x00FF0000.
    if security_offset + 0x180 > pe_offset:
        raise ValueError("Truncated XEX2 security region/media metadata")
    game_region = be32(data, security_offset + 0x178)
    allowed_media_types = be32(data, security_offset + 0x17C)
    headers = {}
    for n in range(optional_count):
        key, value = struct.unpack_from('>II', data, 0x18 + 8 * n)
        if key in headers:
            raise ValueError(f"Duplicate XEX2 header {key:#x}")
        headers[key] = value
    if EXECUTION_INFO not in headers or STATIC_LIBRARIES not in headers:
        raise ValueError("XEX lacks required execution/static-library headers")
    exec_off = headers[EXECUTION_INFO]
    if exec_off + 24 > len(data):
        raise ValueError('Truncated XEX execution info')
    media_id, version, base_version, title_id = struct.unpack_from('>IIII', data, exec_off)
    block_off = headers[STATIC_LIBRARIES]
    block_len = be32(data, block_off)
    if block_len < 20 or (block_len - 4) % 16 or block_off + block_len > len(data):
        raise ValueError('Malformed XEX2 static libraries header')
    libraries = []
    for pos in range(block_off + 4, block_off + block_len, 16):
        raw = data[pos:pos + 16]
        name = raw[:8].split(b'\0', 1)[0].decode('ascii', errors='replace')
        major, minor, build, flags = struct.unpack('>HHHH', raw[8:16])
        libraries.append(dict(name=name, version=f'{major}.{minor}.{build}',
                              major=major, minor=minor, build=build, flags=f'0x{flags:04X}'))
    format_info = None
    if 0x000003FF in headers:
        pos = headers[0x000003FF]
        if pos < 0 or pos + 8 > pe_offset:
            raise ValueError("Invalid XEX2 base-file format offset")
        info_length = be32(data, pos)
        if info_length < 8 or pos + info_length > pe_offset:
            raise ValueError("Invalid XEX2 base-file format length")
        encryption_type, compression_type = struct.unpack_from('>HH', data, pos + 4)
        if encryption_type not in (0, 1) or compression_type not in (0, 1, 2, 3):
            raise ValueError("Unsupported XEX2 encryption/compression type")
        format_info = dict(encryption_type=encryption_type,
                           compression_type=compression_type)
    return dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                pe_offset=pe_offset, title_id=f'{title_id:08X}',
                media_id=f'{media_id:08X}', version=version, base_version=base_version,
                game_region_mask=f'{game_region:08X}',
                allowed_media_types=f'{allowed_media_types:08X}',
                base_file_format=format_info,
                static_libraries=libraries, has_import_libs=IMPORT_LIBRARIES in headers)


def verify_xdk_6534(result: dict) -> None:
    if result['title_id'] != f'{EXPECTED_TITLE:08X}':
        raise ValueError(f"Wrong title id {result['title_id']}; expected {EXPECTED_TITLE:08X}")
    names = {x['name']: x for x in result['static_libraries']}
    for required in ('D3D9', 'XGRAPHC'):
        entry = names.get(required)
        if entry is None:
            raise ValueError(f'Missing {required} static library')
        if (entry['major'], entry['minor'], entry['build']) != (2, 0, 6534):
            raise ValueError(f'{required} library version not 2.0.6534: {entry["version"]}')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('xex', type=Path)
    p.add_argument('--expected-sha256', default=None)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = inspect(args.xex.read_bytes())
    if args.expected_sha256 and result['sha256'].lower() != args.expected_sha256.lower():
        raise ValueError('Input XEX differs from the version pinned by CI')
    verify_xdk_6534(result)
    print(f'[HULK/XDK] verified D3D9 + XGRAPHC XDK 2.0.6534, title={result["title_id"]}, '
          f'media={result["media_id"]}, sha256={result["sha256"]}')
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    else:
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
