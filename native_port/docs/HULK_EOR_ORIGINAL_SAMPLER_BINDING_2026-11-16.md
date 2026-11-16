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
