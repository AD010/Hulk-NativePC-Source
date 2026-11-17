#!/usr/bin/env python3
"""Validate source-named SPIR-V image descriptors for native EoR Vulkan AOT.

The native material binder resolves the current Diligent PSO's exact shader
variable name. Stripping OpName or losing DescriptorSet/Binding silently made
high-resolution original textures unbindable even if the SPIR-V module was
accepted by Vulkan. This deliberately verifies only structural resource
identity, not shader semantics or image correctness.
"""
from __future__ import annotations
import struct

MAGIC = 0x07230203
_IMAGE = {25, 27}  # OpTypeImage / OpTypeSampledImage
_ALIAS = {28, 29, 32}  # array/runtime array/pointer


def validate_named_spirv_images(data: bytes) -> int:
    """Return number of uniquely named shader image variables; fail closed."""
    if len(data) < 20 or len(data) % 4 or len(data) > 8 << 20:
        raise ValueError("Invalid SPIR-V image-module size")
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != MAGIC or not 0 < words[3] <= 1_000_000 or words[4]:
        raise ValueError("Invalid SPIR-V module header")
    bound = words[3]
    image_types: set[int] = set()
    aliases: dict[int, int] = {}
    names: dict[int, str] = {}
    sets: dict[int, int] = {}
    bindings: dict[int, int] = {}
    uniform_constants: list[tuple[int, int]] = []
    cursor = 5
    while cursor < len(words):
        header = words[cursor]
        op, count = header & 0xffff, header >> 16
        if count < 1 or count > len(words) - cursor:
            raise ValueError("Truncated native EoR SPIR-V instruction")
        args = words[cursor + 1:cursor + count]
        if op in _IMAGE | _ALIAS:
            if len(args) < (2 if op in (27, 28, 29) else 3 if op == 32 else 1):
                raise ValueError("Truncated SPIR-V image type declaration")
            ident = args[0]
            if not 0 < ident < bound or ident in image_types or ident in aliases:
                raise ValueError("Duplicate/out-of-bounds SPIR-V resource type")
            if op in _IMAGE:
                image_types.add(ident)
            else:
                aliases[ident] = args[2] if op == 32 else args[1]
        elif op == 59:  # OpVariable(result type, result ID, storage class)
            if len(args) < 3:
                raise ValueError("Truncated SPIR-V variable")
            if args[2] == 0:
                if not 0 < args[1] < bound:
                    raise ValueError("Invalid SPIR-V UniformConstant ID")
                uniform_constants.append((args[0], args[1]))
        elif op == 5:  # OpName(target, ASCII bytes with NUL)
            if len(args) < 2 or not 0 < args[0] < bound or args[0] in names:
                raise ValueError("Invalid or duplicate SPIR-V debug resource name")
            raw = struct.pack("<" + "I" * (len(args) - 1), *args[1:])
            if b"\0" not in raw:
                raise ValueError("Nonterminated SPIR-V resource name")
            raw = raw.split(b"\0", 1)[0]
            # The native Diligent shader variable API requires a stable,
            # printable current-module symbol (no generated fallback name).
            if not raw or len(raw) > 128 or any(c < 33 or c > 126 or c in (47, 92) for c in raw):
                raise ValueError("Unsafe SPIR-V shader variable name")
            names[args[0]] = raw.decode("ascii")
        elif op == 71:  # OpDecorate(target, decoration, literal)
            if len(args) >= 3 and args[1] in (33, 34):
                target = bindings if args[1] == 33 else sets
                if not 0 < args[0] < bound or args[0] in target:
                    raise ValueError("Ambiguous SPIR-V descriptor decoration")
                target[args[0]] = args[2]
        cursor += count

    def is_image(ty: int) -> bool:
        visited: set[int] = set()
        for _ in range(16):
            if ty in image_types:
                return True
            if ty in visited:
                raise ValueError("Cyclic SPIR-V resource type")
            visited.add(ty)
            if ty not in aliases:
                return False
            ty = aliases[ty]
        raise ValueError("Too-deep SPIR-V resource alias")

    occupied: set[tuple[int, int]] = set()
    symbol_keys: set[str] = set()
    found = 0
    for pointer, ident in uniform_constants:
        if not is_image(pointer):
            continue
        if ident not in names or ident not in bindings or ident not in sets:
            raise ValueError("Original EoR sampled image lost native descriptor/name")
        pair = (sets[ident], bindings[ident])
        if pair in occupied or names[ident] in symbol_keys:
            raise ValueError("Duplicate EoR sampled image descriptor or symbol")
        occupied.add(pair)
        symbol_keys.add(names[ident])
        found += 1
    return found
