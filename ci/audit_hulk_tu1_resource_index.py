#!/usr/bin/env python3
"""Read-only Hulk Xbox 360 index (base and TU1 delta) audit.

Usage:
 python ci/audit_hulk_tu1_resource_index.py BASE_US_INDEX TU1_INDEX TU1_EXTRACTED_FOLDER

Never modifies the archives, guest PPC, XEXP, or original game installation.
TU1 delta indexes use signed entry counts in some categories. Each resource
table has 32-bit BE count, abs(count) sorted 32-bit BE hashes, and
abs(count) BE triples (offset, size, unknown). The US base has 52 tables
(26 categories), the European TU1 has 8 (4 categories).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ARCHIVE_NAMES = {"Objects": "OBJECTS.ARC", "QuickDatas": "QUICKDAT.ARC",
                 "Models": "MODELS.ARC", "Datasets": "DATASETS.ARC"}
EXPECTED_US_INDEX_SHA = "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d"


def parse_index(raw: bytes) -> dict:
    if len(raw) < 12:
        raise ValueError("Index shorter than header")
    block_count = struct.unpack_from(">I", raw, 0)[0]
    if block_count % 2 or not 2 <= block_count <= 128:
        raise ValueError("Unexpected Hulk index category count")
    if len(raw) < 4 + 4 * (block_count + 1):
        raise ValueError("Truncated category offset table")
    offsets = struct.unpack_from(f">{block_count + 1}I", raw, 4)
    header_end = 4 + 4 * (block_count + 1)
    if offsets[0] < header_end or offsets[-1] > len(raw):
        raise ValueError("Offsets outside index")
    if any(x > y for x, y in zip(offsets, offsets[1:])):
        raise ValueError("Nonmonotonic index offsets")
    result = {}
    for group in range(block_count // 2):
        name_start, table_start, table_end = offsets[2*group:2*group+3]
        name_bytes = raw[name_start:table_start].split(b"\0", 1)[0]
        name = name_bytes.decode("ascii")
        if not name or name in result:
            raise ValueError("Missing/duplicate category name")
        count = struct.unpack_from(">i", raw, table_start)[0]
        n = abs(count)
        if n > 100000 or table_end - table_start != 4 + n*16:
            raise ValueError(f"Category {name} malformed")
        hashes = struct.unpack_from(f">{n}I", raw, table_start+4) if n else ()
        if hashes != tuple(sorted(set(hashes))):
            raise ValueError(f"Category {name} resource hashes unsorted or repeated")
        base = table_start+4+4*n
        entries = {}
        for i, h in enumerate(hashes):
            offset, size, unknown = struct.unpack_from(">III", raw, base+i*12)
            entries[f"{h:08X}"] = {"offset":offset, "size":size, "unknown":unknown}
        result[name] = {"signed_count":count, "entries":entries}
    return result


def audit(base_raw: bytes, delta_raw: bytes, files: dict[str, bytes]) -> dict:
    base, delta = parse_index(base_raw), parse_index(delta_raw)
    info = {"base_index_sha256": hashlib.sha256(base_raw).hexdigest(),
            "tu1_index_sha256": hashlib.sha256(delta_raw).hexdigest(),
            "groups": {}, "delta_records": 0, "hashes_absent_from_us": 0,
            "all_delta_ranges_in_bounds": True,
            "code_patch_compatible_with_us": False}
    for name, group in delta.items():
        original = base.get(name, {}).get("entries", {})
        data = files.get(name)
        count = len(group["entries"])
        absent = [h for h in group["entries"] if h not in original]
        bad_ranges = []
        for h, record in group["entries"].items():
            start, size = record["offset"], record["size"]
            if data is not None and (size == 0 or start < 4 or start + size > len(data)):
                bad_ranges.append(h)
        info["groups"][name] = {
            "signed_count": group["signed_count"], "count": count,
            "found_in_us": count-len(absent), "absent_from_us": absent,
            "out_of_bounds": bad_ranges,
            "archive_size": len(data) if data is not None else None}
        info["delta_records"] += count
        info["hashes_absent_from_us"] += len(absent)
        info["all_delta_ranges_in_bounds"] &= not bool(bad_ranges) and data is not None
    return info


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("us_index", type=Path)
    p.add_argument("tu1_index", type=Path)
    p.add_argument("tu1_folder", type=Path)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    files = {group: (args.tu1_folder/filename).read_bytes()
             for group,filename in ARCHIVE_NAMES.items()
             if (args.tu1_folder/filename).is_file()}
    result = audit(args.us_index.read_bytes(), args.tu1_index.read_bytes(), files)
    print(json.dumps(result, indent=2))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    if not result["all_delta_ranges_in_bounds"]:
        return 2
    # A mismatch is informative, not a basis for mixing the EU XEXP with US PPC.
    return 0


if __name__ == "__main__":
    sys.exit(main())
