#include "eor_aot_draw_bridge.h"
#include <stdexcept>
#include <vector>

namespace hulk::eor::aot {
namespace {
class ForwardingBackend final : public native::Backend {
 public:
  ForwardingBackend(std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const ReflectionBank& reflection,
                    ShaderBindingBackend& target)
      : draws_(draws), bank_(bank), reflection_(reflection), target_(target) {}
  bool BeginScene(native::Size size, uint32_t samples) override {
    return target_.BeginScene(size,samples);
  }
  void AbortScene() noexcept override { target_.AbortScene(); }
  bool DrawIndexed(const native::IndexedDraw& draw) override {
    if (position_ >= draws_.size())
      throw std::logic_error("EoR draw/compiled shader pairing overflow");
    const auto& pair = draws_[position_++];
    // Prevent original shader identity from being paired with another draw.
    if (pair.draw.vertex_buffer != draw.vertex_buffer ||
        pair.draw.index_buffer != draw.index_buffer ||
        pair.draw.vertex_shader != draw.vertex_shader ||
        pair.draw.pixel_shader != draw.pixel_shader ||
        pair.draw.first_index != draw.first_index ||
        pair.draw.index_count != draw.index_count)
      throw std::invalid_argument("EoR command/shader sequence mismatch");
    const auto* vs = bank_.Find(pair.vertex_source_sha256, Stage::Vertex);
    const auto* ps = bank_.Find(pair.pixel_source_sha256, Stage::Pixel);
    const auto* vr = reflection_.Find(pair.vertex_source_sha256, Stage::Vertex);
    const auto* pr = reflection_.Find(pair.pixel_source_sha256, Stage::Pixel);
    if (!vs || !ps || !vr || !pr)
      throw std::logic_error("Unknown original Xenos shader or constant layout");
    return target_.BindOriginalEffect(pair.effect_hash, pair.pass_index) &&
           target_.BindOriginalConstants(*vr, *pr) &&
           target_.BindOfflineShaders(vs->spirv, ps->spirv) &&
           target_.BindOriginalSamplers(pair.original_samplers) &&
           target_.DrawIndexed(draw);
  }
  bool ResolveScene(native::Size size) override {
    if (position_ != draws_.size())
      throw std::logic_error("Incomplete original shader-paired EoR Draw sequence");
    return target_.ResolveScene(size);
  }
  bool Composite(native::Rect rect, native::Size size) override {
    if (position_ != draws_.size())
      throw std::logic_error("Incomplete original shader-paired EoR Draw sequence");
    return target_.Composite(rect,size);
  }
 private:
  std::span<const BoundIndexedDraw> draws_;
  const ShaderBank& bank_;
  const ReflectionBank& reflection_;
  ShaderBindingBackend& target_;
  size_t position_ = 0;
};
}

void ReplayAotScene(const native::FrameGeometry& frame,
                    std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const EffectCatalog& effects,
                    const ReflectionBank& reflection,
                    ShaderBindingBackend& backend) {
  if (effects.EffectCount() == 0 ||
      effects.ShaderCount() != bank.expected())
    throw std::logic_error("Original EoR effect catalog/shader bank mismatch");
  if (reflection.size() != bank.expected())
    throw std::logic_error("Original EoR VS/PS constant reflection is incomplete");
  if (!bank.HasFullArchiveCoverage())
    throw std::logic_error("Incomplete original shader archive AOT coverage");
  if (draws.empty())
    throw std::logic_error("No original EoR material Draw submissions");
  // All program identities must resolve BEFORE beginning an irreversible GPU
  // render pass. No placeholder shaders or compatibility GPU fallback.
  std::vector<native::IndexedDraw> commands;
  commands.reserve(draws.size());
  for (const auto& value : draws) {
    gpu::ValidateOriginalEorSamplers(value.original_samplers);
    for (const auto& sampler : value.original_samplers) {
      if (sampler.original_effect_hash != value.effect_hash ||
          sampler.original_pass_index != value.pass_index)
        throw std::invalid_argument("Original shader sampler belongs to a different Draw pass");
    }
    if (!value.effect_hash ||
        !effects.HasPair(value.effect_hash, value.pass_index,
                         value.vertex_source_sha256, value.pixel_source_sha256))
      throw std::logic_error("Shader programs do not belong to EoR material effect");
    if (!reflection.Find(value.vertex_source_sha256, Stage::Vertex) ||
        !reflection.Find(value.pixel_source_sha256, Stage::Pixel))
      throw std::logic_error("Original shader constant register layout unavailable");
    if (!bank.Find(value.vertex_source_sha256, Stage::Vertex) ||
        !bank.Find(value.pixel_source_sha256, Stage::Pixel))
      throw std::logic_error("Uncompiled Xenos shader pair in native EoR draw");
    commands.push_back(value.draw);
  }
  ForwardingBackend forward(draws,bank,reflection,backend);
  native::ReplayNativeScene(frame,commands,forward);
}
}  // namespace hulk::eor::aot
