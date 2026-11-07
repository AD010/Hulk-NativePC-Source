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

EXPECTED_TU_SOURCE_SIGNATURE = '8ea1670d19d66c322f50a4d1a6409466a86abada'


def arrange(extracted: Path, base_xex: Path, output: Path, report: dict) -> None:
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
    manifest = dict(
        report,
        purpose='EU TU1 codegen and update-data research input, NOT a game release',
        codegen_xex='codegen-game/default.xex',
        codegen_xexp='codegen-game/default.xexp',
        update_data_root='update',
        native_xdk_hooks_verified_for_tu1=False,
        compiled_and_gameplay_tested=False,
    )
    (output / 'tu1-staging.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


def stage(archive: Path, base_xex: Path, target: Path) -> Path:
    if target.exists():
        raise FileExistsError(f'Refusing to replace existing staging folder: {target}')
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
        arrange(extracted, base_xex, work, report)
        work.rename(target)
        return target
    finally:
        shutil.rmtree(temp, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='TheIncredibleHulkEurope_TU1.zip')
    parser.add_argument('base_xex', type=Path, help='Matching original EU default.xex')
    parser.add_argument('--output', type=Path, required=True, help='NEW staging directory')
    args = parser.parse_args()
    print('Verified Hulk EU Title Update staging:',
          stage(args.archive, args.base_xex, args.output))
    print('NativeX XDK hooks must be remapped after updated codegen.')


if __name__ == '__main__':
    main()
