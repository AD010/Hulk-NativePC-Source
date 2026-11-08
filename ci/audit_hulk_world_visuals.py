#!/usr/bin/env python3
"""Hulk 2008 Xbox360 skyline/fog schema audit (no game data modification).

Use the user's own original DATASETS.ARC and USA INDEX.IND. Compressed EoR
resources are chains of raw-deflate 64KiB chunks with local ZIP headers.
This prints metadata field names and decoded-stream offsets ONLY; it
must NOT be mistaken for live in-game variables or writable addresses.

Usage: python ci/audit_hulk_world_visuals.py USA_INDEX.IND DATASETS.ARC
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from audit_hulk_tu1_resource_index import parse_index

US_INDEX_SHA = "ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d"
FIELDS = ("CameraFarPlane", "CameraNearPlane", "FogNear", "FogFar",
          "FogDensityFunction", "HeightAmbientFalloffStart",
          "HeightAmbientFalloffEnd", "ZoneStreamManager",
          "ZoneStreamManagerLayer", "HiResDataset", "ZoneLowLod",
          "LODDistSq")
EFFECTS = ("GroundFogGlobal", "VolumetricFogBox", "DistantWater",
           "SkyDomeDynamic", "SSAO")
RESOURCE_HASHES = {"_app_": "721AAFDE", "_shader_effects_": "2485AB96",
                   "_engine_": "CA54CBC0"}


def decode_chunks(payload: bytes) -> tuple[bytes, int]:
    # EoR big-endian resource header precedes a chain of raw-deflate PK blocks.
    p = payload.find(b"PK\x03\x04", 0, 64)
    if p < 0:
        return payload, 0
    output = bytearray()
    count = 0
    while p < len(payload):
        if payload[p:p+4] != b"PK\x03\x04":
            nxt = payload.find(b"PK\x03\x04", p, min(len(payload), p+8))
            if nxt < 0:
                if any(payload[p:]):
                    raise ValueError("Non-zero data after last deflate block")
                break
            p = nxt
        if p+30 > len(payload):
            raise ValueError("Truncated deflate header")
        sig, _, flags, method, _, _, _, csize, usize, fn, extra = struct.unpack_from("<IHHHHHIIIHH", payload, p)
        if sig != 0x04034B50 or flags or method != 8 or csize > 65536 or usize > 65536:
            raise ValueError("Unexpected EoR deflate block")
        begin = p+30+fn+extra
        if begin+csize > len(payload):
            raise ValueError("Truncated deflate content")
        plain = zlib.decompress(payload[begin:begin+csize], -15)
        if len(plain) != usize:
            raise ValueError("Decompression length mismatch")
        output.extend(plain)
        if len(output) > 64*1024*1024:
            raise ValueError("Unexpected dataset decompression size")
        p = begin+csize
        count += 1
    return bytes(output), count


def audit(index_bytes: bytes, arc_path: Path) -> dict:
    digest = hashlib.sha256(index_bytes).hexdigest()
    if digest != US_INDEX_SHA:
        raise ValueError("Only the user-supplied USA INDEX.IND is supported")
    entries = parse_index(index_bytes)["Datasets"]["entries"]
    report = {
        "usa_index_sha256": digest,
        "decoded_blocks": {},
        "resources_sha256": {},
        "candidate_schema_fields_not_live_values": {},
        "shader_effect_names_present": {},
        "limits": [
            "Definitions do not provide runtime field values or writable addresses.",
            "Increasing CameraFarPlane alone does not stream HiResDataset farther.",
            "Disabling fog alone reveals low-detail skyline; world LOD and fog must be coordinated.",
        ],
    }
    with arc_path.open("rb") as arc:
        arc.seek(0, 2)
        size = arc.tell()
        for name, h in RESOURCE_HASHES.items():
            e = entries[h]
            start, count = e["offset"], e["size"]
            if not count or start+count > size:
                raise ValueError(name + " has an invalid DATASETS.ARC range")
            arc.seek(start)
            payload = arc.read(count)
            report["resources_sha256"][name] = hashlib.sha256(payload).hexdigest()
            plain, n = decode_chunks(payload)
            report["decoded_blocks"][name] = n
            if name == "_app_":
                for field in FIELDS:
                    needle = field.encode("ascii") + b"\0"
                    positions = []
                    p = 0
                    while len(positions) < 32:
                        p = plain.find(needle, p)
                        if p < 0:
                            break
                        positions.append(p)
                        p += len(needle)
                    report["candidate_schema_fields_not_live_values"][field] = positions
            elif name == "_shader_effects_":
                for effect in EFFECTS:
                    report["shader_effect_names_present"][effect] = effect.encode("ascii")+b"\0" in plain
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("us_index", type=Path)
    parser.add_argument("datasets_arc", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(audit(args.us_index.read_bytes(), args.datasets_arc),
                        indent=2, ensure_ascii=False)+"\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    print(result)


if __name__ == "__main__":
    main()
