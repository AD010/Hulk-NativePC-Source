#pragma once
// Original USA LFXT+TexStreams mip source RANGE preflight.
// Structural source bounds only: this does NOT infer Xenos BC variant,
// byte-swapping, swizzle, tile mode or texture fetch register values.
#include "eor_original_texture_streams.h"
#include "eor_xenos_texture_mips.h"
#include <cstddef>
#include <span>
namespace hulk::eor::textures {
// Preconditions for passing any source level into untile/host Vulkan upload:
// 1. Full archived TexStreams record has its validated BE 4-byte size.
// 2. LFXT has authentic variable-length nested section boundaries.
// 3. The original size table and all padded per-mip storage sizes match.
// 4. Physical level ranges start after the archive envelopes and cannot
//    overlap the observed 52 surplus bytes at each original record's END.
// 5. The stream and resident level byte ranges are contiguous, with no
//    data skipped/duplicated. Endian, BC variant and PSO remain explicit.
void ValidateOriginalMipPayloadRanges(
    std::span<const std::byte> complete_original_lfxt,
    std::span<const std::byte> complete_original_stream,
    const OriginalLfxtInlineSection& inline_section,
    const OriginalLfxtMipPlan& plan,
    XenosTextureFormat explicit_gpu_format,
    std::span<const SourceMipLayout> mip_layouts);
} // namespace hulk::eor::textures
