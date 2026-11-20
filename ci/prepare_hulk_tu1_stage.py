#!/usr/bin/env python3
"""Stage verified EU Hulk TU1 for a separate, version-specific PPC codegen.

No game bytes are committed or changed in place. The "codegen-game" directory
contains the matching European XEX and its XEXP. NativeX guest addresses from
the USA codegen are NOT valid until remapped on the TU1-patched image.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import tempfile
from audit_hulk_tu1 import audit, xex_rsa_digest, digest
from audit_hulk_tu1_resource_index import (
    ARCHIVE_NAMES, decode_tu1_resource, parse_index,
)

EXPECTED_TU_SOURCE_SIGNATURE = '8ea1670d19d66c322f50a4d1a6409466a86abada'
EXPECTED_EU_BASE_SHA256 = 'ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351'
EXPECTED_TU_XEXP_SHA256 = 'f0a34fdf59112ef144782da60c64cc56fa7ee4799e95d91992f4d0da298e5aae'
EXPECTED_TU_DELTA_INDEX_SHA256 = '783dc5df04956ebf875032c9a8d46d65d03b7d4af056e48bc8491df0fa517e00'
EXPECTED_TU_FILE_COUNT = 31
EXPECTED_TU_RECORD_COUNT = 525



def export_decoded_overrides(extracted: Path, output: Path, report: dict) -> dict:
    """Stage authenticated EU TU1 *resource* replacements, not a patched game.

    The report must come from audit() of the exact user-owned LIVE package.
    No guessed archive entries, PAL/USA code or shader bytecode enter Git.
    Game consumers must independently opt in to these verified loose files.
    """
    if output.exists():
        raise FileExistsError('Refusing to overwrite EU TU1 decoded resources')
    if (report.get('xex_signature_match') is not True or
            report.get('xexp_source_rsa_sha1') != EXPECTED_TU_SOURCE_SIGNATURE):
        raise ValueError('EU XEX/TU1 source proof unavailable')
    recorded = {}
    for row in report['files']:
        name = row['path']
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or name in recorded:
            raise ValueError('Unsafe or duplicate EU TU1 resource path')
        source = (extracted / relative).read_bytes()
        sha = digest(source)
        if sha != row['sha256'] or len(source) != row['size']:
            raise ValueError('Authenticated EU TU1 resource changed after verification')
        recorded[name] = sha
    if (len(recorded) != EXPECTED_TU_FILE_COUNT or
            recorded.get('default.xexp') != EXPECTED_TU_XEXP_SHA256 or
            recorded.get('index.ind') != EXPECTED_TU_DELTA_INDEX_SHA256):
        raise ValueError('EU TU1 XEXP/index differs from authenticated inputs')
    delta = parse_index((extracted / 'index.ind').read_bytes())
    if set(delta) != set(ARCHIVE_NAMES):
        raise ValueError('Unexpected EU TU1 index categories')
    resources, groups = {}, {}
    total = 0
    for category, archive_name in ARCHIVE_NAMES.items():
        original = (extracted / archive_name).read_bytes()
        entries = {}
        for key, record in delta[category]['entries'].items():
            offset, size = record['offset'], record['size']
            if offset < 4 or not size or offset + size > len(original):
                raise ValueError('EU TU1 compressed resource index out of bounds')
            raw = original[offset:offset + size]
            # Datasets contain direct records; the other three categories
            # use multiple bounded raw-DEFLATE local ZIP blocks.
            payload = raw if category == 'Datasets' else decode_tu1_resource(raw)
            total += len(payload)
            if total > (128 << 20):
                raise ValueError('EU TU1 total decoded resource budget exceeded')
            resources[(category, key)] = payload
            entries[key] = {'size': len(payload), 'sha256': digest(payload)}
        groups[category] = {
            'signed_count': delta[category]['signed_count'], 'resources': entries,
        }
    if len(resources) != EXPECTED_TU_RECORD_COUNT:
        raise ValueError('Unexpected EU TU1 decoded resource count')
    manifest = {
        'eu_base_xex_sha256': EXPECTED_EU_BASE_SHA256,
        'eu_tu1_xexp_sha256': EXPECTED_TU_XEXP_SHA256,
        'eu_tu1_index_sha256': EXPECTED_TU_DELTA_INDEX_SHA256,
        'source_verified': True,
        'native_eor_resource_override_enabled': False,
        'eu_tu1_gameplay_tested': False,
        'resource_count': len(resources), 'decoded_bytes': total,
        'groups': groups,
    }
    # A damaged archive must never leave a partially visible override root.
    work = Path(tempfile.mkdtemp(prefix='.hulk-eu-tu1-overrides-',
                                dir=output.parent.resolve()))
    try:
        for (category, key), payload in resources.items():
            dest = work / 'resources' / category / f'{key}.bin'
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(payload)
        (work / 'overlay-manifest.json').write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + '\n',
            encoding='utf-8')
        work.rename(output)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return manifest


def arrange(extracted: Path, base_xex: Path, output: Path, report: dict,
            decoded_overrides: bool = False) -> None:
    """Copy verified inputs to isolated codegen and runtime-update roots."""
    codegen, update = output / 'codegen-game', output / 'update'
    codegen.mkdir(parents=True)
    update.mkdir()
    expected_names = {rec['path'] for rec in report['files']}
    if 'default.xexp' not in expected_names or len(expected_names) != 31:
        raise ValueError('Unexpected TU inventory: expected exactly 31 files')
    shutil.copyfile(base_xex, codegen / 'default.xex')
    shutil.copyfile(extracted / 'default.xexp', codegen / 'default.xexp')
    for name in sorted(expected_names - {'default.xexp'}):
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError(f'Invalid TU relative path {name!r}')
        if not name.lower().endswith(('.arc', '.ind')):
            raise ValueError(f'Unexpected TU file {name!r}')
        dest = update / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(extracted / name, dest)
    if xex_rsa_digest((codegen / 'default.xex').read_bytes()).hex() != EXPECTED_TU_SOURCE_SIGNATURE:
        raise ValueError('Staged XEX unexpectedly does not match TU1')
    if digest((codegen / 'default.xexp').read_bytes()) != next(
        x['sha256'] for x in report['files'] if x['path'] == 'default.xexp'):
        raise ValueError('Staged XEXP hash mismatch')
    decoded = None
    if decoded_overrides:
        decoded = export_decoded_overrides(
            extracted, output / 'decoded-update', report)
    manifest = dict(
        report,
        purpose='EU TU1 codegen and update-data research input, NOT a game release',
        decoded_original_resources=decoded is not None,
        decoded_resource_count=decoded['resource_count'] if decoded else 0,
        decoded_override_root='decoded-update' if decoded else None,
        codegen_xex='codegen-game/default.xex',
        codegen_xexp='codegen-game/default.xexp',
        update_data_root='update',
        native_xdk_hooks_verified_for_tu1=False,
        compiled_and_gameplay_tested=False,
    )
    (output / 'tu1-staging.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


def stage(archive: Path, base_xex: Path, target: Path,
          decoded_overrides: bool = False) -> Path:
    if target.exists():
        raise FileExistsError(f'Refusing to replace existing staging folder: {target}')
    if digest(base_xex.read_bytes()) != EXPECTED_EU_BASE_SHA256:
        raise ValueError('Original EU/PAL base XEX is not the pinned TU1 edition')
    parent = target.parent.resolve()
    if not parent.is_dir():
        raise NotADirectoryError(parent)
    temp = Path(tempfile.mkdtemp(prefix='.hulk-tu1-stage-', dir=parent))
    try:
        extracted = temp / 'verified'
        report = audit(archive, base_xex, extracted)
        if (report['xex_signature_match'] is not True
                or report['xexp_source_rsa_sha1'] != EXPECTED_TU_SOURCE_SIGNATURE):
            raise ValueError('Title Update does not match the exact EU XEX')
        work = temp / 'result'
        arrange(extracted, base_xex, work, report,
                decoded_overrides=decoded_overrides)
        work.rename(target)
        return target
    finally:
        shutil.rmtree(temp, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='TheIncredibleHulkEurope_TU1.zip')
    parser.add_argument('base_xex', type=Path, help='Matching original EU default.xex')
    parser.add_argument('--output', type=Path, required=True, help='NEW staging directory')
    parser.add_argument('--decoded-overrides', action='store_true',
                        help='Privately extract verified EU TU1 resource overrides')
    args = parser.parse_args()
    print('Verified Hulk EU Title Update staging:',
          stage(args.archive, args.base_xex, args.output,
                decoded_overrides=args.decoded_overrides))
    print('NativeX XDK hooks must be remapped after updated codegen.')


if __name__ == '__main__':
    main()
