#include "eor_aot_draw_bridge.h"
#include "eor_spirv_texture_bindings.h"
#include <bit>
#include <stdexcept>
#include <map>
#include <vector>

namespace hulk::eor::aot {
namespace {
// Genuine guest triangle lists and frontend triangle fans can be lowered
// into host DrawIndexed only after verifying the complete original host
// vertex/index correspondence and material ownership. Never accept UP.
bool MatchesOriginalXdkDraw(
    const xdk::RawDraw& source,const BoundIndexedDraw& host) noexcept {
  const auto& draw=host.draw;
  if(!draw.guest_vs_matrices)return false;
  if(const auto indexed=xdk::DecodeOriginalIndexedXdkCall(source))
    return !host.source_nonindexed_linearization_verified &&
           indexed->guest_device==draw.guest_vs_matrices->original_guest_device &&
           indexed->index_count==draw.index_count;
  if(source.kind==xdk::Kind::EndVertices) {
    const auto glyphs=xdk::DecodeOriginalFontVertices(source);
    return glyphs && host.original_dynamic_font_draw &&
           host.source_nonindexed_linearization_verified &&
           glyphs->guest_device==draw.guest_vs_matrices->original_guest_device &&
           glyphs->vertex_count/4u*6u==draw.index_count;
  }
  const auto vertices=xdk::DecodeOriginalVerticesXdkCall(source);
  const auto indices=vertices ?
      xdk::VerifiedTriangleListCount(vertices->primitive,vertices->vertex_count) :
      std::nullopt;
  return indices && host.source_nonindexed_linearization_verified &&
         vertices->guest_device==draw.guest_vs_matrices->original_guest_device &&
         *indices==draw.index_count;
}
class ForwardingBackend final : public native::Backend {
 public:
  ForwardingBackend(std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank,const ReflectionBank& reflection,
                    ShaderBindingBackend& target,const xdk::Frame* source,
                    bool* scene_begin_attempted)
      :draws_(draws),bank_(bank),reflection_(reflection),
       target_(target),source_(source),scene_begin_attempted_(scene_begin_attempted){}
  bool BeginScene(native::Size size,uint32_t samples) override {
    // Only NOW can the original source begin altering host color/depth.
    // A shader/owner/PSO rejection before this point must retain NativeX.
    // Mark before delegating: a failing driver bind may already touch RTs.
    if(scene_begin_attempted_)*scene_begin_attempted_=true;
    return target_.BeginScene(size,samples);
  }
  void AbortScene() noexcept override {target_.AbortScene();}
  bool DrawIndexed(const native::IndexedDraw& draw) override {
    if(position_>=draws_.size())
      throw std::logic_error("EoR draw/compiled shader pairing overflow");
    const auto& pair=draws_[position_];
    // Reject mismatched source geometry BEFORE replaying any guest XDK
    // render-target or clear events into the live Vulkan context.
    if(pair.draw.vertex_buffer!=draw.vertex_buffer ||
       pair.draw.index_buffer!=draw.index_buffer ||
       pair.draw.vertex_shader!=draw.vertex_shader ||
       pair.draw.pixel_shader!=draw.pixel_shader ||
       pair.draw.first_index!=draw.first_index ||
       pair.draw.index_count!=draw.index_count ||
       pair.draw.base_vertex!=draw.base_vertex ||
       pair.draw.source_frame!=draw.source_frame ||
       pair.draw.source_ordinal!=draw.source_ordinal)
      throw std::invalid_argument("EoR command/shader sequence mismatch");
    const auto* vs=bank_.Find(pair.vertex_source_sha256,Stage::Vertex);
    const auto* ps=bank_.Find(pair.pixel_source_sha256,Stage::Pixel);
    const auto* vr=reflection_.Find(pair.vertex_source_sha256,Stage::Vertex);
    const auto* pr=reflection_.Find(pair.pixel_source_sha256,Stage::Pixel);
    if(!vs || !ps || !vr || !pr)
      throw std::logic_error("Unknown original Xenos shader or constant layout");
    // Missing original shader/reflection evidence must not replay GPU events.
    if(!ReplayBeforeDraw(draw))return false;
    ++position_;
    return target_.BindOriginalSurface(pair.original_model_hash,
                                       pair.original_surface_ordinal) &&
           target_.BindOriginalEffect(pair.effect_hash,pair.pass_index) &&
           target_.BindOriginalConstants(*vr,*pr) &&
           target_.BindVerifiedProgramPair(pair.vertex_source_sha256,*vs,
                                           pair.pixel_source_sha256,*ps) &&
           target_.BindOriginalSamplers(pair.original_samplers) &&
           target_.DrawIndexed(draw);
  }
  bool ResolveScene(native::Size size) override {
    if(position_!=draws_.size())
      throw std::logic_error("Incomplete source shader-paired Draw sequence");
    if(source_) {
      const auto& events=source_->commands;
      while(cursor_+1<events.size()) {
        const auto& event=events[cursor_++];
        if(event.draw_ordinal!=UINT32_MAX ||
           !target_.ReplayOriginalXdkCommand(event))
          return false;
      }
      if(cursor_+1!=events.size() ||
         events[cursor_].kind!=xdk::CommandKind::Present)
        return false;
    }
    return target_.ResolveScene(size);
  }
  bool Composite(native::Rect rect,native::Size size) override {
    if(position_!=draws_.size())
      throw std::logic_error("Incomplete source shader-paired Draw sequence");
    if(source_) {
      // The terminal Present is part of the original source chronology.
      // Never expose the host frame before its source command succeeds.
      if(cursor_>=source_->commands.size() ||
         cursor_+1!=source_->commands.size())
        return false;
      const auto& terminal=source_->commands[cursor_];
      if(terminal.kind!=xdk::CommandKind::Present ||
         terminal.draw_ordinal!=UINT32_MAX ||
         !target_.ReplayOriginalXdkCommand(terminal))
        return false;
      ++cursor_;
    }
    return target_.Composite(rect,size);
  }
 private:
  bool ReplayBeforeDraw(const native::IndexedDraw& draw) {
    if(!source_)return true;
    const auto& events=source_->commands;
    while(cursor_<events.size()) {
      const auto& event=events[cursor_++];
      if(event.draw_ordinal!=UINT32_MAX) {
        return event.draw_ordinal==position_ &&
               draw.source_frame==source_->sequence &&
               draw.source_ordinal==event.draw_ordinal &&
               position_<source_->calls.size();
      }
      // The actual XDK render-target/clear/resolve commands cannot vanish
      // when the original Draw list is converted to host indexed geometry.
      if(event.kind==xdk::CommandKind::Present ||
         !target_.ReplayOriginalXdkCommand(event))
        return false;
    }
    return false;
  }
  std::span<const BoundIndexedDraw> draws_;
  const ShaderBank& bank_;
  const ReflectionBank& reflection_;
  ShaderBindingBackend& target_;
  const xdk::Frame* source_=nullptr;
  bool* scene_begin_attempted_=nullptr;
  size_t position_=0,cursor_=0;
};}

std::optional<OriginalDrawVsMatrices> ExtractOriginalDrawVsMatrices(
    const ReflectedShader& vertex,const native::IndexedDraw& draw) noexcept {
  if(vertex.stage!=Stage::Vertex || !draw.guest_vs_matrices)
    return std::nullopt;
  const auto& source=*draw.guest_vs_matrices;
  if(draw.source_frame && (!source.original_guest_device ||
                            !source.original_vs_guest_object))
    return std::nullopt;
  OriginalDrawVsMatrices result{};
  auto extract=[&](RegisterSpan span,
                   std::optional<xdk::MatrixRegisters>& target) noexcept {
    if(!span.Present())return true;
    target=xdk::ReadOriginalMatrix(source,span.index,span.count);
    return target.has_value();
  };
  if(!extract(vertex.view_projection,result.view_projection) ||
     !extract(vertex.world,result.world) ||
     !extract(vertex.previous_world,result.previous_world) ||
     !extract(vertex.sky_view_projection,result.sky_view_projection))
    return std::nullopt;
  return result;
}
bool ValidateCapturedOriginalShaderRegisters(
    const ReflectedShader& vertex,const native::IndexedDraw& draw) noexcept {
  if(vertex.stage!=Stage::Vertex || !draw.guest_vs_matrices ||
     !draw.guest_shader_constants)return false;
  const auto& actual=*draw.guest_shader_constants;
  const auto& original=*draw.guest_vs_matrices;
  for(const auto span:{vertex.view_projection,vertex.world,
                       vertex.previous_world,vertex.sky_view_projection}) {
    if(!span.Present())continue;
    const auto source=xdk::ReadOriginalMatrix(original,span.index,span.count);
    if(!source)return false;
    for(uint32_t r=0;r<span.count;++r) {
      const auto full=xdk::ReadOriginalShaderFloat4(
          actual,xdk::OriginalShaderStage::Vertex,span.index+r);
      if(!full)return false;
      for(uint32_t k=0;k<4;++k)
        if(std::bit_cast<uint32_t>((*full)[k]) !=
           std::bit_cast<uint32_t>(source->values[r*4+k]))
          return false;
    }
  }
  return true;
}
void ReplayAotScene(const native::FrameGeometry& frame,
                    std::span<const BoundIndexedDraw> draws,
                    const ShaderBank& bank, const EffectCatalog& effects,
                    const ReflectionBank& reflection,
                    ShaderBindingBackend& backend,
                    const xdk::Frame* exact_original_timeline,
                    bool* scene_begin_attempted) {
  if(scene_begin_attempted)*scene_begin_attempted=false;
  if (effects.EffectCount() == 0 ||
      effects.ShaderCount() != bank.expected())
    throw std::logic_error("Original EoR effect catalog/shader bank mismatch");
  if (reflection.size() != bank.expected())
    throw std::logic_error("Original EoR VS/PS constant reflection is incomplete");
  // A valid original scene may use a subset of the 5194 shader sources.
  // Every actual Draw shader is checked below BEFORE any GPU BeginScene.
  if (!bank.size())
    throw std::logic_error("No compiled original EoR shader program available");
  if (draws.empty())
    throw std::logic_error("No original EoR material Draw submissions");
  // All program identities must resolve BEFORE beginning an irreversible GPU
  // render pass. No placeholder shaders or compatibility GPU fallback.
  std::vector<native::IndexedDraw> commands;
  commands.reserve(draws.size());
  // SPIR-V descriptor parsing is O(shader word count), so do it only once
  // per unique shader digest per stage for this frame, NOT for every Draw.
  std::map<Digest,std::vector<SpirvImageResource>> vsImageCache,psImageCache;
  uint64_t captured_frame=0;
  uint32_t expected_draw_ordinal=0;
  bool captured_source=false,anonymous_source=false;
  for (const auto& value : draws) {
    // A real PPC frame is an ordered sequence, not a bag of verified Draws.
    // Shuffling, truncating the leading Draws, or mixing live and offline
    // submissions can replace surfaces using the wrong per-instance state.
    if(value.draw.source_frame) {
      if(anonymous_source || value.draw.source_ordinal!=expected_draw_ordinal ||
         (captured_frame && value.draw.source_frame!=captured_frame))
        throw std::invalid_argument("Captured Xbox source Draw order/frame mismatch");
      if(expected_draw_ordinal==UINT32_MAX)
        throw std::invalid_argument("Original EoR Draw ordinal overflow");
      ++expected_draw_ordinal;
      captured_source=true;
      captured_frame=value.draw.source_frame;
    } else {
      if(captured_source || value.draw.source_ordinal!=UINT32_MAX)
        throw std::invalid_argument("Mixed live/anonymous original EoR Draw provenance");
      anonymous_source=true;
    }
    if (!value.original_model_hash ||
        value.original_surface_ordinal == UINT32_MAX)
      throw std::invalid_argument("Original EoR Draw has no model/surface owner");
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
      if (sampler.original_model_hash != value.original_model_hash ||
          sampler.original_surface_ordinal != value.original_surface_ordinal ||
          sampler.original_effect_hash != value.effect_hash ||
          sampler.original_pass_index != value.pass_index)
        throw std::invalid_argument("Original shader sampler belongs to a different Draw pass");
    }
    if (!value.effect_hash ||
        !effects.HasPair(value.effect_hash, value.pass_index,
                         value.vertex_source_sha256, value.pixel_source_sha256))
      throw std::logic_error("Shader programs do not belong to EoR material effect");
    const auto* vs_reflection=reflection.Find(value.vertex_source_sha256,
                                               Stage::Vertex);
    const auto* ps_reflection=reflection.Find(value.pixel_source_sha256,
                                               Stage::Pixel);
    if (!vs_reflection || !ps_reflection)
      throw std::logic_error("Original shader constant register layout unavailable");
    // A genuine captured PPC Draw has its own WORLD/ViewProj matrices.
    // Reject the whole batch BEFORE touching the GPU if any original
    // required register is missing or non-finite; never use the previous
    // model's constants or fabricate a 4th World row.
    if(value.draw.source_frame &&
       (!ExtractOriginalDrawVsMatrices(*vs_reflection,value.draw) ||
        !ValidateCapturedOriginalShaderRegisters(*vs_reflection,value.draw)))
      throw std::invalid_argument(
          "Captured Xbox USA Draw has mismatched/missing full VS/PS source constants");
    if (!bank.Find(value.vertex_source_sha256, Stage::Vertex) ||
        !bank.Find(value.pixel_source_sha256, Stage::Pixel))
      throw std::logic_error("Uncompiled Xenos shader pair in native EoR draw");
    commands.push_back(value.draw);
  }
  // The live EoR Vulkan presenter must replay the COMPLETE source
  // RT/ClearF/Resolve/Tiling/Present chronology. A backend which cannot
  // reproduce even one source command is rejected before BeginScene.
  if(exact_original_timeline) {
    const auto& source=*exact_original_timeline;
    if(!xdk::ValidateOriginalCommandTimeline(source) ||
       !captured_source || captured_frame!=source.sequence ||
       source.calls.size()!=draws.size())
      throw std::invalid_argument(
          "Incomplete original Xbox XDK command timeline for native GPU");
    // One-to-one SAME USA call provenance. An original DrawVertices
    // requires prior source-verified sequential host-index linearization;
    // DrawVerticesUP, wrong device or count cannot substitute a host Draw.
    // Validate all call pairs BEFORE GPU-side-effect eligibility checks.
    for(size_t i=0;i<draws.size();++i)
      if(!MatchesOriginalXdkDraw(source.calls[i],draws[i]))
        throw std::invalid_argument(
            "Native indexed EoR Draw disagrees with the original USA XDK call");
    if(!backend.CanReplayOriginalXdkTimeline(source))
      throw std::invalid_argument(
          "Incomplete original Xbox XDK command timeline for native GPU");
    for(const auto& event:source.commands)
      if(event.draw_ordinal==UINT32_MAX &&
         !backend.CanReplayOriginalXdkCommand(event))
        throw std::invalid_argument(
            "Native Vulkan has no source-proven RT/Clear/Resolve/Present handler");
  }
  // A captured live USA Draw must never bypass its original RT/depth/resolve
  // chronology. Anonymous offline scenes remain usable for unit tests.
  if(captured_source && !exact_original_timeline)
    throw std::invalid_argument(
        "Live original EoR Draw requires exact XDK frame timeline");
  // Shader/texture source validation above is complete. Vulkan prepares
  // actual PSOs and validates native geometry before opening any GPU scene;
  // a driver compile failure is no longer allowed halfway through a frame.
  if (!backend.PreflightOriginalScene(draws))
    throw std::runtime_error("Source-proven native Vulkan frame PSO/resource preflight failed");
  ForwardingBackend forward(draws,bank,reflection,backend,
                            exact_original_timeline,scene_begin_attempted);
  native::ReplayNativeScene(frame,commands,forward);
}
}  // namespace hulk::eor::aot
