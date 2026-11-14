#!/usr/bin/env python3
"""Extract original Xbox 360 EoR ShaderEffects into a PRIVATE offline bank.

This is NOT XenosRecomp input yet: records contain EoR effect metadata,
techniques and shader entries, not a proven standalone Xenos shader container.
Never publish game bytes, executable code or effect payloads to a Git repo.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
import tempfile
from audit_hulk_pc_x360_ui_assets import parse_index

MAX_RECORD = 2 << 20
MAX_ARCHIVE = 64 << 20

# XenosRecomp (hedge-dev) ShaderContainer: 9 big-endian uint32 fields.
# The EoR archive embeds real containers in its effect records, preceded by
# effect/technique labels. The standalone container begins at 0x102A1100/01.
def scan_xenos_containers(effect: bytes):
    pos = 0
    while True:
        at = effect.find(b"\x10\x2a\x11", pos)
        if at < 0:
            return
        if at + 36 > len(effect):
            raise ValueError("Truncated original Xenos shader container header")
        (flags, virtual, physical, _field_c, constant_table, definition_table,
         shader_offset, field_1c, field_20) = struct.unpack_from(">9I", effect, at)
        if (flags & 0xFFFFFF00) != 0x102A1100:
            pos = at + 1
            continue
        total = virtual + physical
        if (virtual < 36 or physical == 0 or total > len(effect) - at or
            constant_table < 36 or constant_table >= virtual or
            shader_offset < 36 or shader_offset >= virtual or
            (definition_table and not 36 <= definition_table < virtual) or
            field_1c or field_20):
            raise ValueError("Malformed indexed Xenos shader container")
        yield (at, "VS" if flags & 1 else "PS", effect[at:at + total])
        pos = at + total

def make_bank(index: Path, archive: Path, destination: Path, *,
              repo_root: Path, extract: bool = True, extract_xenos: bool = False) -> dict:
    root = repo_root.resolve()
    output = destination.resolve()
    if output == root or root in output.parents:
        raise ValueError("Private EoR shader records cannot be stored in the public repository")
    ind = parse_index(index.read_bytes())
    if ind['endianness'] != 'big':
        raise ValueError("Expected the Xbox 360 big-endian index")
    records = ind['groups'].get('ShaderEffects')
    if not records:
        raise ValueError("Xbox ShaderEffects category absent or empty")
    payload = archive.read_bytes()
    if len(payload) < 4 or len(payload) > MAX_ARCHIVE:
        raise ValueError("Unreasonable EoR effects archive size")
    entries = []
    for row in records:
        offset, length = row['offset'], row['size']
        if row['reserved'] or offset < 4 or length <= 0 or length > MAX_RECORD:
            raise ValueError("Invalid or unsupported original effect resource")
        if offset > len(payload) or length > len(payload)-offset:
            raise ValueError("Indexed original effect resource is out of bounds")
        entries.append((offset, offset + length, row['hash']))
    ordered = sorted(entries)
    if any(ordered[i][0] < ordered[i-1][1] for i in range(1, len(ordered))):
        raise ValueError("Overlapping original EoR ShaderEffects entries")
    meta = []
    # Verify *all* records and hashes before writing any private files.
    for start, end, key in ordered:
        raw = payload[start:end]
        meta.append({'hash': key, 'bytes': len(raw),
                     'sha256': hashlib.sha256(raw).hexdigest(),
                     'private_file': f'{key}.effect.bin'})
    containers = []
    shader_files = {}
    for start, end, effect_hash in ordered:
        for relative_offset, stage, code in scan_xenos_containers(payload[start:end]):
            digest = hashlib.sha256(code).hexdigest()
            name = digest + ".xenos.bin"
            containers.append({"effect": effect_hash, "relative_offset": relative_offset,
                               "stage": stage, "bytes": len(code),
                               "sha256": digest, "private_file": "Xenos/" + name})
            existing = shader_files.get(name)
            if existing is not None and existing != code:
                raise ValueError("SHA-256 collision in Xenos shader sources")
            shader_files[name] = code
    if extract_xenos and not extract:
        raise ValueError("Cannot write shader containers in inventory-only mode")
    if extract:
        output.mkdir(parents=True, exist_ok=True)
        for (start, end, _), item in zip(ordered, meta):
            file = output / item['private_file']
            # Atomic local write, never blindly overwrite a mismatching bank.
            if file.exists():
                if hashlib.sha256(file.read_bytes()).hexdigest() != item['sha256']:
                    raise ValueError("Existing private effect file has a different SHA-256")
                continue
            with tempfile.NamedTemporaryFile(dir=output, prefix='.hulk_effect_',
                                             delete=False) as tmp:
                temp_path = Path(tmp.name)
                tmp.write(payload[start:end])
            try:
                os.replace(temp_path, file)
            finally:
                temp_path.unlink(missing_ok=True)
        if extract_xenos:
            shader_dir = output / "Xenos"
            shader_dir.mkdir(exist_ok=True)
            # Validate the existing bank before touching any shader file.
            for name, code in shader_files.items():
                dest = shader_dir / name
                if dest.exists() and dest.read_bytes() != code:
                    raise ValueError("Existing Xenos shader container differs from original")
            for name, code in shader_files.items():
                dest = shader_dir / name
                if not dest.exists():
                    with tempfile.NamedTemporaryFile(dir=shader_dir, prefix=".xenos_",
                                                     delete=False) as tmp:
                        temp_path = Path(tmp.name)
                        tmp.write(code)
                    try:
                        os.replace(temp_path, dest)
                    finally:
                        temp_path.unlink(missing_ok=True)
    result = {'bank_version': 1, 'platform': 'Xbox360',
              'format': 'raw Edge of Reality ShaderEffect records',
              'ready_for_xenosrecomp': False,
              'reason': 'EoR effect record is not a verified standalone Xenos shader container',
              'original_effects': len(meta), 'xenos_containers': len(containers),
              'xenos_vertex_shaders': sum(1 for row in containers if row['stage'] == 'VS'),
              'xenos_pixel_shaders': sum(1 for row in containers if row['stage'] == 'PS'),
              'xenosrecomp_input_ready': bool(containers), 'offline_compiled': False,
              'archive_sha256': hashlib.sha256(payload).hexdigest(),
              'index_sha256': hashlib.sha256(index.read_bytes()).hexdigest(),
              'records': meta, 'shader_containers': containers}
    if extract:
        target = output / 'manifest.json'
        temp_path = output / '.manifest.json.tmp'
        temp_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        os.replace(temp_path, target)
    return result

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbox-index', required=True, type=Path)
    parser.add_argument('--xbox-effects', required=True, type=Path)
    parser.add_argument('--private-dir', required=True, type=Path)
    parser.add_argument('--no-extract', action='store_true')
    parser.add_argument('--extract-xenos', action='store_true',
                        help='Privately export real standalone Xenos shader containers')
    args = parser.parse_args()
    result = make_bank(args.xbox_index, args.xbox_effects, args.private_dir,
                       repo_root=Path(__file__).resolve().parents[1],
                       extract=not args.no_extract, extract_xenos=args.extract_xenos)
    print(json.dumps({'effect_count': result['original_effects'],
                      'xenos_containers': result['xenos_containers'],
                      'xenos_vertex_shaders': result['xenos_vertex_shaders'],
                      'xenos_pixel_shaders': result['xenos_pixel_shaders'],
                      'ready_for_xenosrecomp': result['ready_for_xenosrecomp'],
                      'index_sha256': result['index_sha256'],
                      'archive_sha256': result['archive_sha256']}))

if __name__ == '__main__':
    main()
