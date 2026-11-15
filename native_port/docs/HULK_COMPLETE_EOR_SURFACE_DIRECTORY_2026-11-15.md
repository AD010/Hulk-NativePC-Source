# Hulk original EoR surface directory — native parser and index-checked material refs

Completed source-code milestone using genuine original MODELS.ARC from
the user's Xbox 360 USA and PC 2008 archives. The original game files,
shader bytecode and proprietary models remain private.

## Actual source-byte coverage (2026-11-15)

Compiled stand-alone native C++20 code with clang++ -std=c++20 -O2
-Wall -Wextra -Werror. Tested the original indexed model payload of
**every** source record on both platforms:

| Original platform | Original models parsed | Surface records parsed | Kind-3 aggregates | Failures |
|---|---:|---:|---:|---:|
| Xbox 360 | 4,174/4,174 | **11,622** | 17 | 0 |
| PC 2008 | 1,823/1,823 | **3,762** | 8 | 0 |

Original per-surface header fields consist of bounded ASCII material name,
two 32-bit platform-endian metadata words and a material-mask word.
Real model mask values include 0xFFFFFFFF, 0xFFFFFFF8,
0xFFFFFFFD and 0x00000007. The parser preserves these actual fields,
declared surface ordinals and source byte offsets and rejects truncated,
unknown-platform and malformed files.

Every source surface can now be considered for texture references using
the original indexed resource identities, NOT just each model's first.
A separate full-surface scan across both original IND and MODELS archives
confirmed the following index matches, with texture candidate words
bounded before the next surface header:

| Source | Surfaces | Surfaces with matched original texture IDs | Matched texture candidate references |
|---|---:|---:|---:|
| Xbox 360 | 11,622 | **9,396** | **42,357** |
| PC 2008 | 3,762 | **3,132** | **3,306** |

Xbox original material candidates occur at offsets +56, +60, +64,
+68, +72, +76 and +80 after the material name NUL. PC material
candidates occur at +48 and +52. Every candidate is checked against
that SAME platform's original IND "Textures" group; there is no
unverified hash fallback, guest memory or inferred GPU descriptor.
The code is in eor_model_surfaces.cpp and eor_model_surface_materials.cpp,
with exact executable source-only regression tests. The existing
manual-only Windows workflow tests compilation with MSVC.

**Caveat:** these are ORIGINAL platform-index hash correlations, not
verified shader sampler semantics. Binary surface extraction has not
yet validated whole material state or deduced the per-draw EoR
technique. The code does not pretend that
last_surface_header_end is the vertex/index buffer offset.
Original geometry streams, shader/material PSO bindings, engine
camera/Havok ABI, DLSS/FSR motion vectors and true PM4-free rendering
remain separate unresolved integration requirements. An official
Hulk-level release cannot be claimed or built on this alone.
