#!/usr/bin/env python3
"""Compile source-authenticated private Xbox model/texture surface candidates.

This is a PRE-FETCH HINT bank, NOT independently verified EoR live GPU
material identity. Never publish generated HEORCND1 or game assets in Git.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zipfile

MAGIC = b"HEORCND1"
HEADER = struct.Struct("<8sIIII32s32s32s")
LINK = struct.Struct("<III")


def load_source(path: Path) -> dict:
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            names = [name for name in archive.namelist()
                     if Path(name).name == "private_static_material_catalog.json"]
            if len(names) != 1:
                raise ValueError("Missing unique original material candidate catalog")
            return json.loads(archive.read(names[0]))
    return json.loads(path.read_text(encoding="utf-8"))


def records(catalog: dict):
    if (not isinstance(catalog, dict) or
            "NO XDK Draw ownership" not in catalog.get("provenance_level", "")):
        raise ValueError("Expected original archive-only source candidate data")
    names = ("original_index_sha256", "original_models_sha256",
             "original_textures_sha256")
    digests = []
    for name in names:
        value = catalog.get(name)
        if not isinstance(value, str) or len(value) != 64:
            raise ValueError(f"Invalid source archive digest: {name}")
        digests.append(bytes.fromhex(value))
    models = catalog.get("catalog")
    if (not isinstance(models, list) or len(models) != 4174 or
            catalog.get("models_decoded") != len(models)):
        raise ValueError("Original USA Xbox model census incomplete")
    links = []
    surfaces = 0
    seen = set()
    for model in models:
        model_hash = model["hash"]
        if (not isinstance(model_hash, int) or not 0 < model_hash < 2**32 or
                model_hash in seen):
            raise ValueError("Duplicate or invalid original model hash")
        seen.add(model_hash)
        if not isinstance(model["surfaces"], list):
            raise ValueError("Invalid source material surface list")
        for ordinal, surface in enumerate(model["surfaces"]):
            if surface["ordinal"] != ordinal or not surface["name"]:
                raise ValueError("Original model surface ordinal mismatch")
            surfaces += 1
            for source in surface["indexed_texture_candidates"]:
                texture = source["texture_hash"]
                if (not isinstance(texture, int) or not 0 < texture < 2**32 or
                        source["texture_archive_size"] <= 0):
                    raise ValueError("Unverified indexed texture source")
                links.append((texture, model_hash, ordinal))
    if (surfaces != catalog["totals"]["surfaces"] or
            len(links) != catalog["totals"]["candidate_slots"]):
        raise ValueError("Original material candidate counts disagree")
    links = sorted(set(links))
    if not links:
        raise ValueError("Empty source material candidate bank")
    header = HEADER.pack(MAGIC, 1, len(models), surfaces, len(links), *digests)
    report = {
        "original_models": len(models),
        "original_surfaces": surfaces,
        "exact_indexed_texture_links": len(links),
        "original_index_sha256": digests[0].hex(),
        "draw_ownership_verified": False,
        "native_vulkan_frame_verified": False,
    }
    return header, links, report


def build(source: Path, output: Path, public_repository: Path) -> dict:
    root = public_repository.resolve()
    target = output.resolve()
    if target == root or root in target.parents:
        raise ValueError("Private source metadata must stay outside public Git")
    header, links, report = records(load_source(source))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
            "wb", dir=output.parent, prefix=".hulk_original_candidates_",
            delete=False) as out:
        temporary = Path(out.name)
        out.write(header)
        for link in links:
            out.write(LINK.pack(*link))
    try:
        data = temporary.read_bytes()
        if len(data) != HEADER.size + len(links) * LINK.size:
            raise ValueError("Truncated source material candidate bank")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    report["private_index_sha256"] = hashlib.sha256(data).hexdigest()
    report["private_index_size"] = len(data)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--public-repository", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(build(args.source, args.out, args.public_repository),
                     indent=2))


if __name__ == "__main__":
    main()
