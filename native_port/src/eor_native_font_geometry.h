#pragma once
// Source-faithful transient EFontShader Draw -> host-native vertex/index data.
// Input comes from the REAL USA XDK Draw's immutable VB/IB snapshot and an
// independently authenticated original vertex-declaration/material layout.
// This does not substitute a static MODELS.ARC mesh or emulate Xenos PM4.
#include "eor_xdk_draw_capture.h"
#include "eor_native_attributes.h"
#include "eor_native_mesh_upload.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hulk::eor::ui {
struct VerifiedFontVertexLayout {
  uint32_t original_vertex_declaration_guest=0;
  uint32_t original_stride=0;
  model::VerifiedAttributeLayout position{};
  model::VerifiedAttributeLayout texcoord{};
  std::optional<model::VerifiedAttributeLayout> color;
  // Genuine XDK source sampler slot; a fetch register is NOT this slot.
  uint8_t original_pixel_texture_slot=0;
  uint32_t original_font_atlas_hash=0;
  // Both flags must originate from the actual USA vertex declaration and
  // original material/texture setter ABI. An offline font texture hash
  // alone does NOT authorize a live Draw or an assumed UV layout.
  bool original_vertex_declaration_layout_verified=false;
  bool original_font_sampler_owner_verified=false;
  // Original bound index-buffer format is independently source-authenticated.
  // For nonindexed DrawVertices this stays zero.
  uint8_t original_index_element_bytes=0; // 0, 2 or 4
  bool original_index_big_endian=true;
  bool original_index_encoding_verified=false;
};
std::optional<VerifiedFontVertexLayout> ResolveUsaInlineFontLayout(const xdk::RawDraw& call);
// Exact USA Font_Main elements and current SetStreamSource window on buffered Draws.
std::optional<VerifiedFontVertexLayout> ResolveUsaBufferedFontLayout(const xdk::RawDraw& call);
struct LiveFontGeometry {
  uint64_t original_frame_sequence=0;
  uint32_t original_draw_ordinal=UINT32_MAX;
  uint32_t original_shader_instance_guest=0;
  uint32_t original_guest_vertex_declaration=0;
  uint32_t original_font_atlas_hash=0;
  uint32_t original_effect_hash=0;
  uint32_t original_pass_index=UINT32_MAX;
  // Actual source-decoded BE shader inputs, not fake Latin text or quads.
  // Position slot retains X/Y/Z/W; shader reflection selects components.
  std::vector<model::HostAttribute> positions;
  std::vector<model::HostAttribute> texcoords;
  std::vector<model::HostAttribute> colors;
  std::vector<uint32_t> triangle_indices;
};
// Decode the geometry bytes of USA Font_Main. This overload by itself does
// not attribute the glyphs to a live material, frame, or UI page.
LiveFontGeometry DecodeUsaInlineGlyphPayload(std::span<const std::byte> payload);
// Decode exactly the live source XDK vertex/index window; a glyph triangle
// fan is lowered to a host-native triangle list without rasterizing text on
// the CPU. Missing shader, atlas, layout, guest bytes or duplicate sampler
// ownership always rejects the entire Draw before GPU allocation.
LiveFontGeometry DecodeOriginalLiveFontDraw(
    const xdk::RawDraw& source,const VerifiedFontVertexLayout& verified,
    uint64_t exact_frame_sequence,uint32_t original_draw_ordinal);

// Render-thread-only GPU resource ownership. Uses native immutable float4
// attributes and index32 buffers; no guest GPU buffer handle is passed as
// a Vulkan resource. Owner must outlive GPU submission/completion fence.
class UploadedLiveFontGeometry final {
 public:
  UploadedLiveFontGeometry()=default;
  UploadedLiveFontGeometry(const UploadedLiveFontGeometry&)=delete;
  UploadedLiveFontGeometry& operator=(const UploadedLiveFontGeometry&)=delete;
  UploadedLiveFontGeometry(UploadedLiveFontGeometry&&) noexcept;
  UploadedLiveFontGeometry& operator=(UploadedLiveFontGeometry&&) noexcept;
  ~UploadedLiveFontGeometry() noexcept;
  void Reset() noexcept;
  uint64_t position_handle=0, texcoord_handle=0, color_handle=0, index_handle=0;
  uint32_t vertex_count=0,index_count=0;
 private:
  friend UploadedLiveFontGeometry UploadOriginalLiveFontDraw(
      const LiveFontGeometry&,model::NativeMeshUploadBackend&);
  model::NativeMeshUploadBackend* gpu_=nullptr;
};
UploadedLiveFontGeometry UploadOriginalLiveFontDraw(
    const LiveFontGeometry& source,model::NativeMeshUploadBackend& host);
} // namespace hulk::eor::ui
