# Original Xbox EoR indexed geometry decoder (2026-11-15)

A real, executable, host-native C++20 converter now reads original Xbox
360 MODEL mesh payloads into native CPU-side XYZ vertices and triangle
indices. Original game files remain exclusively on the owner's machine.

**Exact original format decoded**:
- Header begins `01 00 00 00 00` followed by count of original vertex
  attributes, vertex stride, reserved byte, and 4 bytes per attribute.
- 24-bit **big-endian** triangle index count; 32-bit **big-endian** vertex
  count immediately afterwards.
- Index stream: 16-bit **big-endian**. Index count is multiple of 3.
- Vertex stream: original specified stride, 32-bit **big-endian** IEEE754
  position X,Y,Z at byte offsets 0/4/8; other vertex attributes are
  preserved byte-for-byte in private CPU memory, not guessed.
- Output: host-native `std::vector<uint32_t>` triangle-list indices,
  `std::vector<NativePosition>` XYZ, plus exact original attribute
  descriptors/source bytes for future normals, UV, vertex skinning.

Full **genuine source byte** test, clang++ -std=c++20 -O2 -Wall -Wextra
-Werror (every source indexed Xbox model):
- Original `MODELS.ARC`: **4,174** resource records checked.
- **2,093 models** contain a decoded triangle-mesh chunk.
- **5,427 decoded, validated mesh chunks**.
- **545,736 positions** and **1,240,944 original indices**, converted.
- Valid vertex strides: 24 (267 meshes), 28 (332), 32 (4,476),
  36 (248), 40 (19), 44 (84), 48 (1).
- In every accepted chunk, all indices are within the original
  vertex buffer, and the maximum index equals `vertexCount - 1`.
- Every decoded source XYZ triplet in every accepted chunk is finite
  and bounded; no erroneous header patterns are accepted.
- Original 2,081 remaining models use another geometry variant, are
  intentionally empty/nontriangular, or lack the validated marker.
  **No invalid mesh is fabricated for these.**

A synthetic C++20 regression also validates both real packed counts
and real stream endian, triangle list and corruption fail-closed.
The new standalone source is
`native_port/src/eor_native_mesh_geometry.{h,cpp}`. The existing
manual-only Windows workflow compiles and tests with MSVC. No second
workflow, private original game data, copied original executable,
or game ZIP is published by this change.

**No false completion claim:** decoding triangles is a major real
native EoR geometry milestone but does not yet identify the exact
per-mesh EoR material/technique pass or translate normals, UVs,
skinning, texture sampling, all missing mesh variants, original
GPU render states, camera/Havok ABI or a true working host Vulkan
backend. The source geometry can now be fed into a native GPU
vertex/index buffer without Xenia/ReXGlue PM4; this does not mean the
game currently does so at runtime. Only the full, tested port should
receive a final release ZIP.
