#!/usr/bin/env python3
"""Hulk strict adapter for hedge-dev/XenosRecomp source: no dropped shader fetches.

Upstream XenosRecomp currently returns silently for every TextureFetchOpcode
except TextureFetch/GetTextureWeights. Hulk has real getCompTexLOD and SET
gradient/LOD instructions: simply using Sonic's converter would compile a
syntactically valid Vulkan shader with MISSING original instructions.
Until accurate EoR/Xenos semantics are implemented, fail conversion rather
than rendering corrupted materials with incomplete programs.
Apply only to an explicit source checkout of hedge-dev/XenosRecomp.
"""
from __future__ import annotations
import argparse
from pathlib import Path

PATH = 'XenosRecomp/shader_recompiler.cpp'
INCLUDE = '#include "shader_recompiler.h"\n#include "shader_common.h"'
INCLUDE_FIXED = INCLUDE + '\n#include <stdexcept>'
OLD = '''    if (instr.opcode != FetchOpcode::TextureFetch && instr.opcode != FetchOpcode::GetTextureWeights)
        return;'''
NEW = '''    // HULK/XENOSRECOMP-FETCH-STRICT: a valid SPIR-V module is useless
    // when the original shader program has missing Xenos fetch operations.
    // getCompTexLOD (17) and set-register-gradient operations (24..26)
    // require EoR-specific semantics. Never silently ignore them.
    if (instr.opcode != FetchOpcode::TextureFetch &&
        instr.opcode != FetchOpcode::GetTextureWeights) {
        throw std::runtime_error(fmt::format(
            "HULK/XENOSRECOMP-UNSUPPORTED-FETCH opcode={} (no silent shader loss)",
            static_cast<uint32_t>(instr.opcode)));
    }'''


def patch(source: str) -> tuple[str, bool]:
    if NEW in source and INCLUDE_FIXED in source:
        return source, False
    if 'HULK/XENOSRECOMP-FETCH-STRICT' in source or INCLUDE_FIXED in source:
        raise ValueError('Partial EoR Xenos fetch strictness patch')
    if source.count(INCLUDE) != 1 or source.count(OLD) != 1:
        raise ValueError('Unexpected XenosRecomp source ABI/implementation')
    return source.replace(INCLUDE, INCLUDE_FIXED, 1).replace(OLD, NEW, 1), True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path)
    args = parser.parse_args()
    file = args.checkout / PATH
    before = file.read_text(encoding='utf-8')
    after, changed = patch(before)
    if changed:
        file.write_text(after, encoding='utf-8', newline='\n')
    print('Hulk Xenos shader missing-fetch strictness:',
          'installed' if changed else 'already installed')


if __name__ == '__main__':
    main()
