"""Read-only audit of possible command-line switches in the exact original PC EXE.

Only reports bounded switch tokens and offsets, never redistributes EXE bytes.
Finding a string is NOT evidence that a switch is implemented or functional.
"""
import argparse
import hashlib
from pathlib import Path
import sys

EXPECTED_SHA256 = '6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233'
SWITCHES = ('noshadows', 'texstreamnolim', 'texstreamlowres',
            'videomode', 'novsync', 'windowed')


def audit(data):
    """Return switch names with their ASCII and UTF-16LE byte offsets."""
    findings = {}
    for switch in SWITCHES:
        needle = ('-' + switch).encode('ascii')
        hits = []
        for encoding, pattern in (('ascii', needle),
                                  ('utf16le', ('-' + switch).encode('utf-16le'))):
            start = 0
            while True:
                pos = data.find(pattern, start)
                if pos < 0:
                    break
                before = data[pos - (2 if encoding == 'utf16le' else 1):pos]
                after = data[pos + len(pattern):pos + len(pattern) + (2 if encoding == 'utf16le' else 1)]
                # Exclude substring hits in longer option names.
                word = b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_'
                if encoding == 'utf16le':
                    # A UTF-16LE neighbor is two bytes; its low byte alone is
                    # insufficient (the previous implementation accepted
                    # "-noshadowsExtra" as a complete switch).
                    valid_before = len(before) < 2 or before[1] != 0 or before[0] not in word
                    valid_after = len(after) < 2 or after[1] != 0 or after[0] not in word
                else:
                    valid_before = not before or before[-1] not in word
                    valid_after = not after or after[0] not in word
                if valid_before and valid_after:
                    hits.append((encoding, pos))
                start = pos + 1
        if hits:
            findings[switch] = hits
    return findings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='private original HULK.EXE (never uploaded)')
    args = parser.parse_args(argv)
    try:
        data = args.exe.read_bytes()
    except OSError as exc:
        print(f'REFUSED: cannot read private PC EXE: {exc.strerror or type(exc).__name__}', file=sys.stderr)
        return 2
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        print('REFUSED: SHA256 does not match the approved original PC HULK.EXE', file=sys.stderr)
        return 2
    if data[:2] != b'MZ':
        print('REFUSED: invalid DOS header', file=sys.stderr)
        return 2
    pe_offset = int.from_bytes(data[0x3c:0x40], 'little')
    # Validate the COFF header and the *declared* optional-header extent.
    # Merely seeing PE\\0\\0, i386 and a PE32 magic at +24 is insufficient:
    # a truncated/forged header can otherwise be accepted.
    coff_end = pe_offset + 24
    if pe_offset < 0x40 or coff_end > len(data) or data[pe_offset:pe_offset+4] != b'PE\0\0':
        print('REFUSED: invalid PE signature or COFF header', file=sys.stderr)
        return 2
    machine = int.from_bytes(data[pe_offset+4:pe_offset+6], 'little')
    section_count = int.from_bytes(data[pe_offset+6:pe_offset+8], 'little')
    optional_size = int.from_bytes(data[pe_offset+20:pe_offset+22], 'little')
    optional_end = coff_end + optional_size
    if (machine != 0x14c or optional_size < 96 or section_count == 0 or
            optional_end > len(data) or
            int.from_bytes(data[coff_end:coff_end+2], 'little') != 0x10b or
            optional_end + section_count * 40 > len(data)):
        print('REFUSED: not a valid PE32 i386 image', file=sys.stderr)
        return 2
    for name, hits in audit(data).items():
        print(name + ': ' + ', '.join(f'{enc}@0x{offset:X}' for enc, offset in hits))
    print('String occurrences only; no runtime or D3D9 behavior verified.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
