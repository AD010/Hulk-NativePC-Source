# Original Hulk EoR model-to-texture references — private asset audit

The owner's original Xbox 360 MODELS.ARC, PC MODELS(1).ARC and platform
IND indices were exhaustively audited READ-ONLY, 2026-11-15.

There are **3,831** original Xbox models with a first surface label and
**1,560** PC models with a first surface label. Reading big-endian or
little-endian u32 at platform-dependent offsets after the first-surface
label gives genuine resource hashes from the corresponding original
platform's `Textures` group:

| Offset relative to first-surface data | Xbox matches | PC matches |
| ---: | ---: | ---: |
| +48 | 0 (not an Xbox candidate) | **1,388** |
| +52 | 0 | **34** |
| +56 | **3,432** | 0 (not a PC candidate) |
| +60 | **3,148** | 0 |
| +64 | **2,739** | 0 |
| +68 | **2,681** | 0 |
| +72 | **878** | 0 |
| +76 | **315** | 0 |
| +80 | **211** | 0 |

**3,432/3,831 Xbox models** and **1,388/1,560 PC models**
have >=1 original indexed texture hash in their selected first-surface
candidate offsets. The Xbox first-surface structures often have four
consecutive indexed texture hashes, while PC first surfaces often have
one. These are **candidate texture reference slots**, not confirmed
per-Draw material descriptor binding semantics, and do not imply the
Xbox texture encoding is decodable as native Vulkan images.

The actual C++20 parser `eor_model_material_refs.cpp` reads only these
observed locations, checks both model platform and original IND byte
order, and rejects hashes absent from that platform's Textures group.
It uses the existing original model resource parser and is integrated
in the existing CMake model target. Standalone C++20 clang -Werror
test passed with synthetic Xbox and PC model/texture pairs. The existing
single manual Windows release workflow adds a MSVC source test (not a
gameplay build).

IMPORTANT: no decoded geometry, vertex layout, skinning, GPU pipeline,
descriptor binding, physically correct scene motion or complete
independent Vulkan port is claimed. This closes a specific EoR
resource-provenance gap, not the entire rendering subsystem.
