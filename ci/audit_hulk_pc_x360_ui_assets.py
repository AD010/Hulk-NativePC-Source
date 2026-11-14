#!/usr/bin/env python3
"""Read-only cross-platform EoR archive/UI resource audit for Hulk (2008).

Usage (private local inputs; NEVER check game bytes into public Git):
  python ci/audit_hulk_pc_x360_ui_assets.py \
    --pc-index indexPC.ind --xbox-index indexXBOX360.ind \
    --pc-archive "Fonts=FONTS(1).ARC" \
    --pc-archive "Textures=textures(1).arc" \
    --xbox-archive "Fonts=FONTS.ARC" \
    --xbox-archive "Textures=TEXTURES.ARC" \
    --pc-app _pc_app_decoded --xbox-app _x360_app_decoded \
    --output private-ui-assets-audit.json

The index has n interleaved archive-name / resource-table blocks. Each
table contains count BE/LE hashes followed by count triples of
(offset, size, reserved). Does not modify, repack or expose guest files.
Results contain only bounds/identity metadata and a strict whitelist of
existing menu identifier names; no textures, microcode or game strings
outside the known UI namespace are exported.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from typing import BinaryIO

UI_MARKER = re.compile(
    rb"(?:uipage_[a-z0-9_]{3,96}|uiwidget_[a-z0-9_]{3,96}|"
    rb"ui_[a-z0-9_]{3,96}|(?:txfl|lfxt)ui_[a-z0-9_]{3,96}|"
    rb"font_clean(?:\.fdf)?)", re.IGNORECASE
)
CHUNK_SIZE = 1 << 20
MAX_UI_HITS = 128


def parse_index(raw: bytes) -> dict:
    """Strictly parse both PC LE and Xbox 360 BE Hulk index.ind files."""
    if len(raw) < 24:
        raise ValueError("Index too short")
    le, be = struct.unpack_from("<I", raw), struct.unpack_from(">I", raw)
    if 2 <= le[0] <= 128 and le[0] % 2 == 0:
        endian, count = "<", le[0]
    elif 2 <= be[0] <= 128 and be[0] % 2 == 0:
        endian, count = ">", be[0]
    else:
        raise ValueError("Unrecognized index byte order/block count")
    header_end = 4 + (count + 1) * 4
    if header_end > len(raw):
        raise ValueError("Truncated index offset table")
    offsets = struct.unpack_from(endian + f"{count + 1}I", raw, 4)
    if offsets[0] < header_end or offsets[-1] != len(raw) - 4:
        raise ValueError("Invalid index first block/footer offset")
    if any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("Overlapping or unordered index blocks")
    groups: dict[str, list[dict]] = {}
    for i in range(count // 2):
        name_b, table_b, table_e = offsets[2*i:2*i+3]
        name_bytes = raw[name_b:table_b].split(b"\0", 1)[0]
        if not name_bytes or len(name_bytes) > 64:
            raise ValueError("Missing/overlong archive name")
        name = name_bytes.decode("ascii")
        if name in groups:
            raise ValueError(f"Duplicate archive category: {name}")
        if table_e - table_b < 4:
            raise ValueError(f"Truncated category: {name}")
        n = struct.unpack_from(endian + "I", raw, table_b)[0]
        if n > 100_000 or 4 + 16 * n != table_e - table_b:
            raise ValueError(f"Malformed table for {name}")
        keys = struct.unpack_from(endian + f"{n}I", raw, table_b + 4) if n else ()
        if any(a >= b for a, b in zip(keys, keys[1:])):
            raise ValueError(f"Unsorted/duplicate resource hashes in {name}")
        meta_b = table_b + 4 + 4*n
        records = []
        for j, key in enumerate(keys):
            offset, size, reserved = struct.unpack_from(endian + "III", raw, meta_b + 12*j)
            records.append({"hash": f"{key:08X}", "offset": offset,
                            "size": size, "reserved": reserved})
        groups[name] = records
    footer = struct.unpack_from(endian + "I", raw, offsets[-1])[0]
    return {"endianness": "little" if endian == "<" else "big",
            "footer": footer, "groups": groups}


def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 * CHUNK_SIZE):
            hasher.update(block)
    return hasher.hexdigest()


def sha256_span(stream: BinaryIO, start: int, size: int) -> bytes:
    hasher = hashlib.sha256()
    stream.seek(start)
    left = size
    while left:
        block = stream.read(min(left, CHUNK_SIZE))
        if not block:
            raise ValueError("Archive truncated during resource hashing")
        hasher.update(block)
        left -= len(block)
    return hasher.digest()


def collect_ui_tokens(stream: BinaryIO, records: list[dict],
                      probe_bytes: int = 4096) -> list[dict]:
    """Probe only small record prefixes; no assertion of exhaustive extraction."""
    hits: list[dict] = []
    for row in records:
        if row["size"] <= 0:
            continue
        stream.seek(row["offset"])
        head = stream.read(min(probe_bytes, row["size"]))
        names = sorted({match.group(0).decode("ascii").lower()
                        for match in UI_MARKER.finditer(head)})
        if names:
            hits.append({"hash": row["hash"], "ui_identifiers": names[:12]})
        if len(hits) >= MAX_UI_HITS:
            break
    return hits


def audit_archive(path: Path, records: list[dict]) -> dict:
    if not path.is_file():
        raise FileNotFoundError(path)
    byte_size = path.stat().st_size
    bad = [x["hash"] for x in records
           if x["offset"] < 4 or x["size"] == 0
           or x["offset"] + x["size"] > byte_size]
    if bad:
        raise ValueError(f"{path.name}: {len(bad)} out-of-bounds/empty records (first {bad[0]})")
    ordered = sorted(records, key=lambda r: r["offset"])
    overlaps = [ordered[i]["hash"] for i in range(1, len(ordered))
                if ordered[i]["offset"] < ordered[i-1]["offset"] + ordered[i-1]["size"]]
    if overlaps:
        raise ValueError(f"{path.name}: {len(overlaps)} overlapping indexed resources")
    max_end = max((r["offset"] + r["size"] for r in ordered), default=4)
    with path.open("rb") as stream:
        tokens = collect_ui_tokens(stream, records)
    return {
        "archive_name": path.name, "bytes": byte_size,
        "sha256": sha256_file(path),
        "resource_count": len(records), "last_indexed_end": max_end,
        "unindexed_tail_bytes": byte_size - max_end,
        "indexed_coverage_fraction": round(max_end / byte_size, 6) if byte_size else 0,
        "sampled_prefix_ui_candidates": tokens,
        "probe_is_exhaustive": False,
        "warning": ("Large unindexed suffix; verify platform assignment against "
                    "the correct PC/Xbox index before decoding")
                   if byte_size > 0 and max_end / byte_size < 0.75 else None,
    }


def compare_shared_records(pc_path: Path, xbox_path: Path,
                           pc: list[dict], xbox: list[dict]) -> dict:
    left = {x["hash"]: x for x in pc}
    right = {x["hash"]: x for x in xbox}
    shared = sorted(left.keys() & right.keys())
    equal, different = 0, 0
    with pc_path.open("rb") as fs_pc, xbox_path.open("rb") as fs_xbox:
        for key in shared:
            p, x = left[key], right[key]
            if p["size"] != x["size"]:
                different += 1
            elif (sha256_span(fs_pc, p["offset"], p["size"]) ==
                  sha256_span(fs_xbox, x["offset"], x["size"])):
                equal += 1
            else:
                different += 1
    return {"shared_hashes": len(shared), "byte_identical": equal,
            "payloads_different": different, "pc_only": len(left)-len(shared),
            "xbox_only": len(right)-len(shared)}


def scan_ui_app(path: Path, index_groups: dict[str, list[dict]] | None = None,
                byte_order: str = "little") -> dict:
    """Decode only *candidate* links near UI names in a private _app_ dump.

    A 32-bit index hash found within 64 bytes is a lead, not proof that
    the owning UI widget references the asset. No guest bytes are exported.
    """
    if path.stat().st_size > 64 * CHUNK_SIZE:
        raise ValueError("Unexpectedly large decoded _app_")
    data = path.read_bytes()
    markers = list(UI_MARKER.finditer(data))
    names = sorted({m.group(0).decode("ascii").lower() for m in markers})
    output = {"decoded_app_size": len(data),
              "ui_identifier_count": len(names),
              "ui_identifiers": names[:512], "truncated": len(names) > 512,
              "nearby_hashes_are_verified_bindings": False}
    if not index_groups:
        return output
    known: dict[int, tuple[str, str]] = {}
    for category in ("Textures", "Fonts", "Objects"):
        for record in index_groups.get(category, []):
            known[int(record["hash"], 16)] = (category, record["hash"])
    if not known:
        output["index_hash_proximity_candidates"] = []
        return output
    byteorder = "little" if byte_order == "little" else "big"
    hits: dict[tuple[str, str, str], tuple[int, int]] = {}
    for marker in markers[:2048]:
        identifier = marker.group(0).decode("ascii").lower()
        lo = max(0, marker.start() - 64)
        hi = min(len(data) - 4, marker.end() + 64)
        for pos in range(lo, hi + 1):
            value = int.from_bytes(data[pos:pos+4], byteorder)
            match = known.get(value)
            if match is None:
                continue
            category, hashed = match
            distance = min(abs(pos - marker.start()), abs(pos - marker.end()))
            key = (identifier, category, hashed)
            frequency, best = hits.get(key, (0, 9999))
            hits[key] = (frequency + 1, min(best, distance))
    candidates = [
        {"ui_identifier": key[0], "resource_group": key[1],
         "indexed_hash": key[2], "observations": value[0],
         "closest_byte_distance": value[1], "verified_link": False}
        for key, value in hits.items()
    ]
    candidates.sort(key=lambda row: (-row["observations"],
                                     row["closest_byte_distance"],
                                     row["ui_identifier"], row["indexed_hash"]))
    output["index_hash_proximity_candidates"] = candidates[:128]
    output["hash_candidates_truncated"] = len(candidates) > 128
    return output


def labeled_path(item: str) -> tuple[str, Path]:
    if "=" not in item:
        raise argparse.ArgumentTypeError("Expected Category=path")
    category, name = item.split("=", 1)
    if not category or not name:
        raise argparse.ArgumentTypeError("Expected Category=path")
    return category, Path(name)


def run(pc_index: Path, xbox_index: Path,
        pc_archives: dict[str, Path], xbox_archives: dict[str, Path],
        pc_app: Path | None = None, xbox_app: Path | None = None) -> dict:
    pc = parse_index(pc_index.read_bytes())
    xbox = parse_index(xbox_index.read_bytes())
    if pc["endianness"] != "little" or xbox["endianness"] != "big":
        raise ValueError("PC/Xbox index byte order incorrect or arguments swapped")
    result: dict = {
        "format": "Hulk 2008 EoR private, read-only archive comparison",
        "pc_index_sha256": sha256_file(pc_index),
        "xbox_index_sha256": sha256_file(xbox_index),
        "group_counts": {cat: {"pc": len(pc["groups"].get(cat, [])),
                               "xbox": len(xbox["groups"].get(cat, []))}
                         for cat in sorted(pc["groups"].keys() | xbox["groups"].keys())},
        "archives": {"pc": {}, "xbox": {}},
        "resource_comparisons": {},
        "game_bytes_exported": False,
        "original_ui_hooked": False,
    }
    for name, group, filenames in [
            ("pc", pc["groups"], pc_archives),
            ("xbox", xbox["groups"], xbox_archives)]:
        for category, archive in filenames.items():
            if category not in group:
                raise ValueError(f"Unknown {name} archive group {category!r}")
            result["archives"][name][category] = audit_archive(archive, group[category])
    for category in sorted(pc_archives.keys() & xbox_archives.keys()):
        result["resource_comparisons"][category] = compare_shared_records(
            pc_archives[category], xbox_archives[category],
            pc["groups"][category], xbox["groups"][category])
    if pc_app:
        result["pc_ui_app"] = scan_ui_app(pc_app, pc["groups"], "little")
    if xbox_app:
        result["xbox_ui_app"] = scan_ui_app(xbox_app, xbox["groups"], "big")
    if pc_app and xbox_app:
        a = set(result["pc_ui_app"]["ui_identifiers"])
        b = set(result["xbox_ui_app"]["ui_identifiers"])
        result["shared_ui_identifier_count"] = len(a & b)
        result["shared_ui_identifiers"] = sorted(a & b)[:256]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pc-index", type=Path, required=True)
    parser.add_argument("--xbox-index", type=Path, required=True)
    parser.add_argument("--pc-archive", type=labeled_path, action="append", default=[])
    parser.add_argument("--xbox-archive", type=labeled_path, action="append", default=[])
    parser.add_argument("--pc-app", type=Path, help="Optional privately decoded PC _app_")
    parser.add_argument("--xbox-app", type=Path, help="Optional privately decoded Xbox _app_")
    parser.add_argument("--output", type=Path, help="Sanitized JSON; never raw game bytes")
    args = parser.parse_args()
    results = run(args.pc_index, args.xbox_index,
                  dict(args.pc_archive), dict(args.xbox_archive),
                  args.pc_app, args.xbox_app)
    result = json.dumps(results, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
