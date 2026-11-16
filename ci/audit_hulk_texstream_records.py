#!/usr/bin/env python3
"""Read-only diagnostic of owner-provided original Xbox 360 TexStreams .bin records.

A manifest from an arbitrary Xbox IND does NOT prove USA source identity.
Do NOT use byte-pattern guesses as authoritative Xenos format, mip layout
or native Vulkan shader sampler bindings. Output contains metadata and
very short original header hex samples; never commit actual report.
Usage: python ci/audit_hulk_texstream_records.py /path/to/records --output /path/to/private_report.json
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

RECORD = re.compile(
    r"^([A-Za-z0-9_-]+) hash=0x([A-Fa-f0-9]{8}) offset=(\d+) size=(\d+)$")
EXPECTED_US_INDEX_SHA = (
    "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d")
MAX_RECORD = 64 * 1024 * 1024

def read_manifest(path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    extent = None
    records = []
    used = set()
    for line in lines:
        if line.startswith("Source file size: "):
            if extent is not None:
                raise ValueError("Duplicate original ARC extent")
            extent = int(line.split(":", 1)[1].strip())
        item = RECORD.fullmatch(line.strip())
        if item is None:
            continue
        name, key, start, size = item.groups()
        key = key.upper()
        off, length = int(start), int(size)
        if (key in used or not 4 <= off or not 1 <= length <= MAX_RECORD
                or off + length > (extent or 0)):
            raise ValueError("Invalid/duplicate manifest hash or source bounds")
        used.add(key)
        records.append(dict(name=name, hash=key, offset=off, size=length))
    if extent is None or not 4 <= extent <= 8 * 1024**3 or not records:
        raise ValueError("Original TexStreams manifest missing valid index records")
    return extent, records

def inspect_record(path, indexed):
    length = path.stat().st_size
    if length != indexed["size"]:
        raise ValueError("Record length differs from original IND extent: " + path.name)
    with path.open("rb") as source:
        head = source.read(min(128, length))
        source.seek(0)
        sha = hashlib.sha256()
        while block := source.read(1024 * 1024):
            sha.update(block)
    if len(head) != min(128, length):
        raise ValueError("Original TexStreams record truncated")
    words = [
        dict(at=i, be=struct.unpack_from(">I", head, i)[0],
             le=struct.unpack_from("<I", head, i)[0])
        for i in range(0, min(56, len(head)) - 3, 4)
    ]
    # 0x38 is a hypothesis from consistent source lengths, not a decoded
    # Xenos header. Both 0x38 and 0x40 are retained as alternatives.
    hypotheses = {
        str(pos): dict(bytes_after_header=length-pos,
                       multiple_4096=(length-pos) % 4096 == 0,
                       multiple_2048=(length-pos) % 2048 == 0)
        for pos in (0, 16, 32, 48, 56, 64) if length >= pos
    }
    return dict(name=indexed["name"], hash=indexed["hash"],
                source_offset=indexed["offset"], record_bytes=length,
                sha256=sha.hexdigest(), first_56_hex=head[:56].hex(),
                payload_after_56_first_64_hex=head[56:120].hex(),
                first_56_be_le_words=words,
                header_boundary_hypotheses=hypotheses)

def audit(folder, *, index_sha256=None):
    source_size, records = read_manifest(folder / "manifest.txt")
    expected = {r["name"]+"_"+r["hash"]+".bin" for r in records}
    existing = {p.name for p in folder.glob("*.bin")}
    if expected != existing:
        raise ValueError(
            "Manifest .bin sample set mismatch: missing="
            + repr(sorted(expected-existing)) + " extra="
            + repr(sorted(existing-expected)))
    result = [inspect_record(folder / (r["name"]+"_"+r["hash"]+".bin"), r)
              for r in records]
    all_56 = all((r["record_bytes"] - 56) % 4096 == 0
                 for r in result)
    if index_sha256 is not None and (
            not re.fullmatch("[0-9a-fA-F]{64}", index_sha256)):
        raise ValueError("Invalid original INDEX.IND SHA256")
    return dict(schema="HULK-TEXSTREAM-RECORD-DIAGNOSTICS/1",
                source_arc_size=source_size, record_count=len(result),
                declared_index_sha256=index_sha256,
                original_index_is_sha256_pinned_usa=(
                    index_sha256 is not None and
                    index_sha256.lower() == EXPECTED_US_INDEX_SHA),
                index_region_confirmed=index_sha256 is not None,
                all_records_have_possible_56_byte_prefix_and_4k_body=all_56,
                original_56_byte_header_meaning_verified=False,
                xenos_gpu_format_verified=False,
                original_mip_chain_verified=False,
                live_draw_sampler_binding_verified=False,
                records=result)

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--index-sha256", help="optional SHA256 of the exact original INDEX.IND")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    report = audit(args.folder, index_sha256=args.index_sha256)
    txt = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(txt, encoding="utf-8")
    else:
        print(txt, end="")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print("HULK/TEXSTREAMS sample audit failed: " + str(exc), file=sys.stderr)
        sys.exit(2)
