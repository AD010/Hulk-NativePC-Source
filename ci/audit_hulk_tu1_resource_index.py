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
import zlib

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



# TU1 Objects/QuickDatas/Models ARC entries are not directly interchangeable
# with the base ARC bytes. Datasets delta entries are *uncompressed* records,
# and must NOT be fed into this ZIP decoder.
# Format: u32be(total_decompressed), u32be(0x10000), repeated blocks of
# u32be(local_zip_length) + ZIP LOCAL header (no central directory) +
# raw DEFLATE bytes. Blocks can span 64 KiB uncompressed. The first
# block is also prefixed by its length, so do not strip only 12+30 bytes
# from a large asset and silently lose later chunks.
MAX_TU_RESOURCE_BYTES = 64 * 1024 * 1024


def decode_tu1_resource(blob: bytes, max_output: int = MAX_TU_RESOURCE_BYTES) -> bytes:
    """Strict, bounded decoder for the verified 2008 Hulk TU1 resource wrapper."""
    if len(blob) < 42 or max_output < 1:
        raise ValueError("Truncated/invalid TU1 compressed resource")
    expected, block_capacity = struct.unpack_from(">II", blob, 0)
    if block_capacity != 0x10000 or not (0 < expected <= max_output):
        raise ValueError("Unreasonable TU1 resource output size or block capacity")
    cursor = 8
    chunks = []
    total = 0
    while cursor < len(blob):
        if cursor + 4 > len(blob):
            raise ValueError("Truncated TU1 block size")
        span = struct.unpack_from(">I", blob, cursor)[0]
        cursor += 4
        if span < 30 or cursor + span > len(blob):
            raise ValueError("TU1 ZIP local block extends beyond resource")
        block = blob[cursor:cursor + span]
        cursor += span
        (magic, version, flags, method, mtime, mdate, crc,
         compressed_len, uncompressed_len, filename_len,
         extra_len) = struct.unpack_from("<IHHHHHIIIHH", block, 0)
        start = 30 + filename_len + extra_len
        if (magic != 0x04034B50 or flags != 0 or method != 8 or
                start > len(block) or start + compressed_len != len(block) or
                not (0 < uncompressed_len <= block_capacity) or
                uncompressed_len > expected - total):
            raise ValueError("Malformed TU1 raw-DEFLATE local ZIP header")
        # Avoid flush() after a max-length decompression: when a malicious
        # stream expands beyond the declared 64 KiB, flush may otherwise
        # inflate the entire remaining zip bomb before we reject it.
        stream = zlib.decompressobj(-15)
        piece = stream.decompress(block[start:], uncompressed_len + 1)
        if (not stream.eof or stream.unused_data or stream.unconsumed_tail or
                len(piece) != uncompressed_len):
            raise ValueError("Invalid or oversized TU1 ZIP block contents")
        chunks.append(piece)
        total += len(piece)
    if cursor != len(blob) or total != expected:
        raise ValueError("Incomplete TU1 resource block chain")
    return b"".join(chunks)


def compare_tu1_resource_payloads(
        base_index: bytes, tu1_index: bytes,
        base_archives: dict[str, bytes], tu1_archives: dict[str, bytes]) -> dict:
    """Compare actual decoded resources, never return or print game bytes.

    The EU TU1 and USA base are region-distinct. A match is byte-equality
    evidence only, NOT a licence to automatically mount the remaining changes.
    Missing base archive categories are reported as unexamined, never zero.
    """
    original = parse_index(base_index)
    update = parse_index(tu1_index)
    report = {
        "format": "Objects/QuickDatas/Models: local ZIP raw DEFLATE blocks; Datasets: direct uncompressed records",
        "groups": {}, "total_equal": 0, "total_different": 0,
        "total_tu1_only": 0, "unexamined_groups": [],
        "usa_runtime_overlay_enabled": False,
        "water_logic_fix_installed": False,
    }
    for name, group in update.items():
        base = base_archives.get(name)
        patched = tu1_archives.get(name)
        if base is None or patched is None:
            report["unexamined_groups"].append(name)
            continue
        equal, different, added, lods = [], [], [], []
        for key, item in group["entries"].items():
            off, size = item["offset"], item["size"]
            if not size or off < 4 or off + size > len(patched):
                raise ValueError(f"TU1 {name}/{key}: archive range invalid")
            payload = (patched[off:off + size] if name == "Datasets"
                       else decode_tu1_resource(patched[off:off + size]))
            text_name = payload.split(b"\0", 1)[0][:120].decode(
                "ascii", errors="ignore")
            if name == "QuickDatas" and text_name.startswith("city_zone_") and text_name.endswith("__lod"):
                lods.append({"hash": key, "resource_name": text_name})
            old = original.get(name, {}).get("entries", {}).get(key)
            if old is None:
                added.append(key)
                continue
            base_off, base_size = old["offset"], old["size"]
            if base_size <= 0 or base_off < 4 or base_off + base_size > len(base):
                raise ValueError(f"USA {name}/{key}: archive range invalid")
            if payload == base[base_off:base_off + base_size]:
                equal.append(key)
            else:
                different.append(key)
        report["groups"][name] = {
            "tu1_records": len(group["entries"]),
            "decoded_byte_identical_to_usa": len(equal),
            "decoded_different_from_usa": len(different),
            "tu1_only_hashes": sorted(added),
            "changed_hashes": sorted(different),
            "city_low_lod_records": sorted(lods, key=lambda item: item["resource_name"]),
            "all_records_decoded": True,
        }
        report["total_equal"] += len(equal)
        report["total_different"] += len(different)
        report["total_tu1_only"] += len(added)
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("us_index", type=Path)
    p.add_argument("tu1_index", type=Path)
    p.add_argument("tu1_folder", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--base-archive-root", type=Path,
                   help="Optional local USA ARC folder; compare DECOMPRESSED TU1 payloads without mounting them")
    args = p.parse_args()
    files = {group: (args.tu1_folder/filename).read_bytes()
             for group,filename in ARCHIVE_NAMES.items()
             if (args.tu1_folder/filename).is_file()}
    base_index = args.us_index.read_bytes()
    tu1_index = args.tu1_index.read_bytes()
    result = audit(base_index, tu1_index, files)
    if args.base_archive_root is not None:
        base_files = {
            name: (args.base_archive_root / filename).read_bytes()
            for name, filename in ARCHIVE_NAMES.items()
            if (args.base_archive_root / filename).is_file()
        }
        result["decoded_payloads"] = compare_tu1_resource_payloads(
            base_index, tu1_index, base_files, files)
    print(json.dumps(result, indent=2))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    if not result["all_delta_ranges_in_bounds"]:
        return 2
    # A mismatch is informative, not a basis for mixing the EU XEXP with US PPC.
    return 0


if __name__ == "__main__":
    sys.exit(main())
