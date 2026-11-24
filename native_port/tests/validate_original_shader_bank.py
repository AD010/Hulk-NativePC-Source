#!/usr/bin/env python3
"""Validate every original HEORSPV1 module and every cube-gamma subset.
No original shaders are saved outside a temporary private directory.
"""
import argparse
import pathlib
import struct
import subprocess
import tempfile


def entries(data):
    if len(data) < 16 or data[:8] != b"HEORSPV1":
        raise ValueError("Invalid original SPIR-V bank")
    expected, compiled = struct.unpack_from("<II", data, 8)
    if expected != 5194 or compiled != expected:
        raise ValueError("Incomplete USA original bank")
    at = 16
    seen = set()
    for _ in range(compiled):
        if at + 40 > len(data):
            raise ValueError("Truncated original shader header")
        digest = data[at:at + 32]
        stage, count = struct.unpack_from("<II", data, at + 32)
        at += 40
        if stage not in (1, 2) or digest in seen or count < 5 or at + count * 4 > len(data):
            raise ValueError("Invalid original shader record")
        seen.add(digest)
        yield stage, data[at:at + count * 4]
        at += count * 4
    if at != len(data):
        raise ValueError("Unexpected original shader bank tail")


def cube_slots(data):
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V magic")
    at = 5
    mask = 0
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        if not count or at + count > len(words):
            raise ValueError("Invalid instruction length")
        if opcode == 5 and count >= 3:
            name = struct.pack("<%dI" % (count - 2), *words[at + 2:at + count]).split(b"\0", 1)[0]
            prefix = b"HulkTextureCube_s"
            if name.startswith(prefix):
                slot = int(name[len(prefix):])
                if not 0 <= slot < 16:
                    raise ValueError("Invalid cube slot")
                mask |= 1 << slot
        at += count
    return mask


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bank", type=pathlib.Path)
    parser.add_argument("lowerer", type=pathlib.Path)
    parser.add_argument("validator", type=pathlib.Path)
    args = parser.parse_args()
    lowerer = args.lowerer.resolve()
    validator = args.validator.resolve()
    originals = variants = 0
    with tempfile.TemporaryDirectory(prefix="hulk-private-spv-") as temp:
        original = pathlib.Path(temp) / "original.spv"
        specialized = pathlib.Path(temp) / "gamma.spv"
        def validate(path):
            subprocess.run([str(validator), "--target-env", "vulkan1.3", str(path)], check=True)
        for stage, module in entries(args.bank.read_bytes()):
            original.write_bytes(module)
            validate(original)
            originals += 1
            if stage != 2:
                continue
            mask = cube_slots(module)
            subset = mask
            while subset:
                subprocess.run([str(lowerer), str(original), str(subset), str(specialized)], check=True)
                validate(specialized)
                variants += 1
                subset = (subset - 1) & mask
    if originals != 5194 or variants != 248:
        raise RuntimeError(f"Unexpected validation coverage: {originals}, {variants}")
    print(f"SPIRV-Tools: {originals} originals and {variants} gamma variants validated")


if __name__ == "__main__":
    main()
