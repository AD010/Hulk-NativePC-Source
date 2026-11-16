# EoR original Xbox shader texture-binding guard — 2026-11-16

The original owner's ten TexStreams raw-record reports prove only the
big-endian four-byte remaining-length envelope; they do not prove texture
format/tiling/mips. Original GPU Xenos `LFXT` metadata and the live shader
fetch ownership must be recovered independently. See the source-authentic
sampler preflight modules.

`InspectSpirvImageResources` inspects actual AOT SPIR-V OpVariable,
OpTypeImage/OpTypeSampledImage, OpDecorate Binding/DescriptorSet, and
OpName. Every source image declared in an original compiled shader must
have a matching exact-name, exact-stage, provenance-verified EoR
material binding **before BeginScene**. Missing shaders, duplicate
descriptors, missing variable names and unmatched host resources are
rejected. This is deliberately conservative to prevent substituting a
random material texture or blindly using an Xenos register ID as a
Vulkan descriptor binding number.

The original Xenos fetch slot ID and host descriptor-binding ID are
**distinct address spaces**; the bridge compares the actual shader
resource name with separately audited original material ownership,
never treats their numbers as interchangeable.

The DiligentCore host `BindVerifiedOriginalEorTextures` resolves ALL
pixel/vertex resource variables first, validates actual resident SRVs,
and only then sets the batch. A true runtime backend still has to
create the matching PSO/SRB, commit the resources, capture the original
EoR material/fetch mapping and drive the actual USA Draw/Resolve path.
No Vulkan runtime validation or fixed graphics is claimed.

The original USA `INDEX.IND` SHA-256 is pinned in assets_locator.cpp.
The separately submitted legacy stream manifest didn't supply its
index SHA, and the first ten LFXT bytes alone are insufficient to
deduce Xenos block addressing/format or per-mip placement.

The existing single manual-only Windows CI workflow includes
CPU-only synthetic tests. A source commit is not a successful CI
artifact; trigger the existing manual build only when a functional
game test is warranted. Do not generate placeholder shader or mip
data to force a screenshot.

## Current checkpoint after image-binding preflight

Code (committed in the existing main branch):
- `fe21ca4b`: strict original 4-byte big-endian TexStreams
  envelope and source-offset bounds checks, tests based on ten genuine record
  lengths from owner-provided TXT.
- `19a46270`: explicit original material/fetch ownership validation and
  DiligentCore shader-resource variable setter for resident texture SRVs.
- `65e4c57b`: sampler ownership metadata handed through the source-verified
  geometry -> AOT shader -> native Draw pipeline before submitting geometry.
- `58a22858`: regression test for good, wrong-owner and failed backend
  shader-resource binding conditions.
- `8675437c`: SPIR-V shader image descriptor declarations are preflighted
  against original stage/name binding ownership before BeginScene.
- `1de62b53`: strengthen synthetic shader-module tests and skip irrelevant
  non-image OpName debug metadata during validation.
- `647eb9f6`: cache SPIR-V image descriptors once per shader digest per
  frame, avoiding a shader-bytecode scan for each repeated Draw.
- `8e8b4924`: downloadable browser ZIP:
  `tools/Hulk_USA_LFXT_Header_Collector_PC.zip`.
  This emits `Hulk_USA_LFXT_Header_Report.txt` for the ten texture
  resources, requiring the user's existing USA index(2).ind and Xbox
  TEXTURES.ARC, without uploading those archives.

**Still not proven / no production claim:** exact original Xenos format,
tiled image dimensions, packed mip offsets and per-pass sampler ownership
from the game's running USA EoR engine; a fully functional Diligent
`ShaderBindingBackend` integrated with the live original guest Draw;
Windows CI execution on the current HEAD; GPU screenshot/RenderDoc validation;
correct color/material/LOD/flicker in the released game.

**Runtime rule:** The new shader SRB function is a callable host binding,
not a live PSO implementation or automatic inference of proprietary engine
fields. Do not substitute inferred sampler slots, fake textures, or incomplete
OriginalStream records. No original game bytes entered GitHub.

**Next evidence:** ingest `Hulk_USA_LFXT_Header_Report.txt` from the above
Windows collector. The previous TexStreams record TXT already proved
10/10 big-endian size prefixes but not their Xenos texture descriptor
layouts. After actual LFXT parameters are verified, connect exact image
dimensions, GPU format, tiling, and mip addresses, then validate source
shader descriptors against an original live USA Draw capture. Only then
run the one existing manual-only Release workflow and check the game.
