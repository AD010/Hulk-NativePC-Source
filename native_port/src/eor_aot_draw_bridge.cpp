#include "eor_aot_draw_bridge.h"
#include "eor_spirv_texture_bindings.h"
#include <stdexcept>
#include <map>
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
           target_.BindVerifiedProgramPair(pair.vertex_source_sha256, *vs,
                                          pair.pixel_source_sha256, *ps) &&
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
  // SPIR-V descriptor parsing is O(shader word count), so do it only once
  // per unique shader digest per stage for this frame, NOT for every Draw.
  std::map<Digest,std::vector<SpirvImageResource>> vsImageCache,psImageCache;
  for (const auto& value : draws) {
    gpu::ValidateOriginalEorSamplers(value.original_samplers);
    // Fail closed for ORIGINAL sampled images that have no resident material
    // ownership. The shader's Vulkan resource name is not its Xenos register.
    for (const auto stage : {Stage::Vertex,Stage::Pixel}) {
      const auto* program=bank.Find(
          stage==Stage::Vertex ? value.vertex_source_sha256 :
                                 value.pixel_source_sha256,stage);
      if(!program)
        throw std::logic_error("Original source shader stage unavailable");
      const auto& key=stage==Stage::Vertex ? value.vertex_source_sha256 :
                                              value.pixel_source_sha256;
      auto& cache=stage==Stage::Vertex ? vsImageCache : psImageCache;
      auto found=cache.find(key);
      if(found==cache.end())
        found=cache.emplace(key,InspectSpirvImageResources(program->spirv)).first;
      const auto& images=found->second;
      for(const auto& image:images) {
        size_t matches=0;
        for(const auto& slot:value.original_samplers)
          if(slot.exact_shader_variable_name==image.exact_variable_name &&
             slot.stage==(stage==Stage::Vertex ?
                   gpu::OriginalSamplerStage::Vertex :
                   gpu::OriginalSamplerStage::Pixel))
            ++matches;
        if(matches!=1)
          throw std::invalid_argument("Original material lacks exact sampled SPIR-V shader resource");
      }
      for(const auto& slot:value.original_samplers) {
        if(slot.stage!=(stage==Stage::Vertex ? gpu::OriginalSamplerStage::Vertex :
                                               gpu::OriginalSamplerStage::Pixel))continue;
        bool exists=false;
        for(const auto& image:images)
          if(image.exact_variable_name==slot.exact_shader_variable_name)
            exists=true;
        if(!exists)
          throw std::invalid_argument("Unmatched original material shader sampler name/stage");
      }
    }
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
