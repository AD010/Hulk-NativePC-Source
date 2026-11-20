#include "../native_port/src/eor_aot_draw_bridge.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>
using namespace hulk::eor;
using namespace hulk::eor::aot;
struct Backend final:ShaderBindingBackend {
  unsigned begin=0,bind=0,effect=0,constants=0,draw=0,resolve=0,composite=0,abort=0,samplers=0,surfaces=0;
  std::vector<std::pair<uint32_t,uint32_t>> surface_history;
  bool refuse_samplers=false,refuse_preflight=false;
  bool accept_original_timeline=false;
  bool refuse_terminal_present=false;
  std::vector<xdk::CommandKind> original_gpu_commands;
  std::vector<char> presentation_order;
  bool CanReplayOriginalXdkTimeline(const xdk::Frame& source) const override {
    return accept_original_timeline &&
           xdk::ValidateOriginalCommandTimeline(source);
  }
  bool CanReplayOriginalXdkCommand(const xdk::RawCommand&) const override {
    return accept_original_timeline;
  }
  bool ReplayOriginalXdkCommand(const xdk::RawCommand& call) override {
    if(!accept_original_timeline ||
       (refuse_terminal_present && call.kind==xdk::CommandKind::Present))
      return false;
    original_gpu_commands.push_back(call.kind);
    if(call.kind==xdk::CommandKind::Present)
      presentation_order.push_back('P');
    return true;
  }
  unsigned preflights=0;
  bool PreflightOriginalScene(std::span<const BoundIndexedDraw> draws) override {
    ++preflights;
    return !refuse_preflight && !draws.empty();
  }
  Digest received_vs{},received_ps{};
  unsigned verified_program_pairs=0;
  bool BindVerifiedProgramPair(const Digest& vs_id,const Blob& vs,
                               const Digest& ps_id,const Blob& ps) override {
    assert(vs.stage==Stage::Vertex && ps.stage==Stage::Pixel);
    received_vs=vs_id;received_ps=ps_id;
    ++verified_program_pairs;
    return ShaderBindingBackend::BindVerifiedProgramPair(vs_id,vs,ps_id,ps);
  }
  bool BeginScene(native::Size size,uint32_t) override {
    assert(size.width==2560 && size.height==1600); ++begin;return true;}
  bool BindOriginalSurface(uint32_t model,uint32_t ordinal) override {
    ++surfaces;surface_history.emplace_back(model,ordinal);
    return model!=0 && ordinal!=UINT32_MAX;
  }
  bool BindOriginalEffect(uint32_t hash, uint32_t pass) override {
    assert(hash == 0x123u && pass == 0u); ++effect; return true;
  }
  bool BindOriginalConstants(const ReflectedShader& vs,
                             const ReflectedShader& ps) override {
    assert(vs.stage==Stage::Vertex && ps.stage==Stage::Pixel);
    assert(vs.view_projection.index==11 && vs.view_projection.count==4);
    assert(!ps.view_projection.Present());
    ++constants;return true;
  }
  bool BindOriginalSamplers(std::span<const gpu::VerifiedEorSampler> slots) override {
    ++samplers;
    for(const auto& slot:slots)
      assert(slot.original_effect_hash==0x123 && slot.original_pass_index==0);
    return !refuse_samplers || slots.empty();
  }
  bool BindOfflineShaders(std::span<const uint32_t> vs,
                          std::span<const uint32_t> ps) override {
    assert(vs[0]==0x07230203 && ps[0]==0x07230203);++bind;return true;}
  void AbortScene() noexcept override {++abort;}
  bool DrawIndexed(const native::IndexedDraw&) override {++draw;return true;}
  bool ResolveScene(native::Size size) override {assert(size.width==2560);++resolve;return true;}
  bool Composite(native::Rect r,native::Size) override {
    assert(r.height==1600);
    presentation_order.push_back('C');
    ++composite;return true;
  }
};
void I32(std::vector<uint8_t>& b,uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
void Add(std::vector<uint8_t>& b,uint8_t digest,uint32_t stage,bool sampled=false) {
  for(unsigned i=0;i<32;++i)b.push_back(digest);
  std::vector<uint32_t> words{
      0x07230203u,0x10500u,0u,16u,0u,
      0x00020011u,1u,
      0x0003000eu,0u,1u,
      0x0005000fu,stage==1?0u:4u,1u,0x6e69616du,0u
  };
  if(sampled && stage==2) {
    // Original shader fixture with a *named* UniformConstant image at
    // descriptor set 0, Vulkan binding 2. Unnamed/absent slots must fail.
    const uint32_t image_resource[] = {
      0x00050005u,4u,0x69445f67u,0x73756666u,0x00000065u,
      0x00040047u,4u,33u,2u,
      0x00040047u,4u,34u,0u,
      0x00090019u,2u,5u,1u,0u,0u,0u,1u,0u,
      0x00040020u,3u,0u,2u,
      0x0004003bu,3u,4u,0u
    };
    words.insert(words.end(),std::begin(image_resource),std::end(image_resource));
  }
  I32(b,stage);I32(b,static_cast<uint32_t>(words.size()));
  for(uint32_t v:words)I32(b,v);
}
int main(){
  auto file=std::filesystem::temp_directory_path()/"hulk_native_aot_pair_test.bin";
  std::vector<uint8_t> b{'H','E','O','R','S','P','V','1'};
  I32(b,2);I32(b,2);Add(b,0x11,1);Add(b,0x22,2);
  {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());}
  ShaderBank shaders;shaders.Load(file);std::filesystem::remove(file);
  auto effect_file=std::filesystem::temp_directory_path()/"hulk_aot_effect_pair.bin";
  std::vector<uint8_t> catalog{'H','E','O','R','M','A','T','2'};
  I32(catalog,1);I32(catalog,2);
  I32(catalog,0x123);I32(catalog,0); // Candidate first pass.
  for(unsigned i=0;i<32;++i)catalog.push_back(0x11);
  for(unsigned i=0;i<32;++i)catalog.push_back(0x22);
  {std::ofstream out(effect_file,std::ios::binary);
   out.write(reinterpret_cast<const char*>(catalog.data()),catalog.size());}
  EffectCatalog effects;effects.Load(effect_file);std::filesystem::remove(effect_file);
  auto reflection_file=std::filesystem::temp_directory_path()/"hulk_aot_reflection_test.bin";
  std::vector<uint8_t> reflected{'H','E','O','R','R','E','F','1'};
  I32(reflected,2);I32(reflected,1);
  for(uint8_t id:{uint8_t(0x11),uint8_t(0x22)}) {
    for(unsigned i=0;i<32;++i)reflected.push_back(id);
    I32(reflected,id==0x11?1:2);
    I32(reflected,id==0x11?11:UINT32_MAX);
    I32(reflected,id==0x11?4:0);
    for(unsigned j=0;j<3;++j){I32(reflected,UINT32_MAX);I32(reflected,0);}
  }
  {std::ofstream out(reflection_file,std::ios::binary);
   out.write(reinterpret_cast<const char*>(reflected.data()),reflected.size());}
  ReflectionBank reflection;reflection.Load(reflection_file);
  std::filesystem::remove(reflection_file);
  native::FrameGeometry geometry=native::PlanFrame({{2560,1600},{1280,720},true,true,true,4});
  BoundIndexedDraw entry{{1,2,3,4,12,0},{},{}};
  entry.vertex_source_sha256.fill(0x11);entry.pixel_source_sha256.fill(0x22);
  entry.effect_hash=0x123;
  entry.pass_index=0;
  entry.original_model_hash=0xCAFE;
  entry.original_surface_ordinal=0;
  // Verify real c11-c14 ViewProj is attached to the exact original Draw
  // before Vulkan BeginScene. The test uses synthetic BE-decoded numbers.
  native::IndexedDraw tagged=entry.draw;
  tagged.source_frame=17;tagged.source_ordinal=0;
  tagged.guest_vs_matrices=xdk::OriginalVsMatrices{};
  tagged.guest_vs_matrices->original_guest_device=0x40002880u;
  tagged.guest_vs_matrices->original_vs_guest_object=0x82001234u;
  tagged.guest_vs_matrices->finite_register_mask=(1u<<19)-1u;
  tagged.guest_vs_matrices->c7_through_c25[(11-7)*4]=2.5f;
  auto original_constants=std::make_shared<xdk::OriginalShaderRegisters>();
  for(uint32_t reg=11;reg<15;++reg)
    original_constants->finite_vertex[reg/64]|=uint64_t(1)<<(reg%64);
  original_constants->vertex[11*4]=2.5f;
  original_constants->pixel[0]=0.125f;
  original_constants->finite_pixel[0]|=1u;
  tagged.guest_shader_constants=original_constants;
  ReflectedShader view_only;
  view_only.stage=Stage::Vertex;
  view_only.view_projection={11,4};
  const auto real_matrices=ExtractOriginalDrawVsMatrices(view_only,tagged);
  assert(real_matrices && real_matrices->view_projection &&
         real_matrices->view_projection->values[0]==2.5f);
  assert(ValidateCapturedOriginalShaderRegisters(view_only,tagged));
  auto conflicting_full=std::make_shared<xdk::OriginalShaderRegisters>(
      *original_constants);
  conflicting_full->vertex[11*4]=3.5f;
  tagged.guest_shader_constants=conflicting_full;
  assert(!ValidateCapturedOriginalShaderRegisters(view_only,tagged));
  tagged.guest_shader_constants=original_constants;
  tagged.guest_vs_matrices->finite_register_mask &= ~(1u<<(11-7));
  assert(!ExtractOriginalDrawVsMatrices(view_only,tagged));
  tagged.guest_vs_matrices->finite_register_mask=(1u<<19)-1u;
  Backend target;
  std::vector<BoundIndexedDraw> draws{entry};
  bool native_scene_attempted=false;
  ReplayAotScene(geometry,draws,shaders,effects,reflection,target,
                 nullptr,&native_scene_attempted);
  assert(native_scene_attempted);
  // A captured source Draw with missing registers is rejected before any
  // live GPU render target is cleared or submitted.
  BoundIndexedDraw captured=entry;
  captured.draw=tagged;
  Backend bound;
  std::vector<BoundIndexedDraw> capturedDraws{captured};
  ReplayAotScene(geometry,capturedDraws,shaders,effects,reflection,bound);
  assert(bound.begin==1 && bound.draw==1);
  // Actual original RT0 -> ClearF -> indexed Draw -> Resolve -> Present
  // must be replayed in ORDER on the same backend. The previous bridge
  // silently discarded every non-Draw XDK GPU command.
  xdk::RawDraw real_call{xdk::Kind::DrawIndexedVertices,
       0x8288E318u,0x8263BF50u,{0x40002880u,4,0,0,12,0,0,0}};
  xdk::RawCommand rt{xdk::CommandKind::SetRenderTarget,
       0x8287F2C0u,0x8263A000u,{0x40002880u,0,0x82001234u,0,0,0,0,0}};
  xdk::RawCommand clr{xdk::CommandKind::ClearF,
       0x82888C40u,0x8263A004u,{0x40002880u,1,0,0x82002000u,0,0,0,0}};
  clr.captured_clear=xdk::OriginalXdkClearF{};
  clr.captured_clear->flags=1;
  clr.captured_clear->has_color=true;
  clr.captured_clear->color={0.25f,0.5f,0.75f,1.f};
  xdk::RawCommand original_draw{xdk::CommandKind::DrawIndexedVertices,
       real_call.function,real_call.lr,real_call.gpr,0u};
  xdk::RawCommand resolve{xdk::CommandKind::Resolve,
       0x828897F8u,0x8263A008u,{0x40002880u,1,0,0x82003000u,0,0,0,0}};
  xdk::RawCommand present{xdk::CommandKind::Present,
       0x82880778u,0x8263A00Cu,{0x40002880u,0x82004000u,0,0,0,0,0,0}};
  xdk::Frame source_frame{};
  source_frame.sequence=17;
  source_frame.calls={real_call};
  source_frame.commands={rt,clr,original_draw,resolve,present};
  assert(xdk::ValidateOriginalCommandTimeline(source_frame));
  Backend chronological;
  chronological.accept_original_timeline=true;
  ReplayAotScene(geometry,capturedDraws,shaders,effects,reflection,
                 chronological,&source_frame);
  assert(chronological.begin==1 && chronological.draw==1 &&
         chronological.resolve==1 && chronological.composite==1 &&
         chronological.presentation_order==std::vector<char>({'P','C'}) &&
         chronological.original_gpu_commands==
             std::vector<xdk::CommandKind>({
                xdk::CommandKind::SetRenderTarget,
                xdk::CommandKind::ClearF,
                xdk::CommandKind::Resolve,
                xdk::CommandKind::Present}));
  // A host indexed Draw MUST correspond to the same original USA indexed
  // XDK call, not merely a matching ordinal or shader pair. The host mesh
  // offsets can be rebased, but index COUNT and guest device cannot.
  auto RejectSourceMismatch=[&](const xdk::Frame& candidate,
                                const std::vector<BoundIndexedDraw>& host) {
    assert(xdk::ValidateOriginalCommandTimeline(candidate));
    Backend rejected;
    rejected.accept_original_timeline=true;
    bool refused=false;
    try {ReplayAotScene(geometry,host,shaders,effects,reflection,
                        rejected,&candidate);}
    catch(const std::invalid_argument&) {refused=true;}
    assert(refused && rejected.preflights==0 && rejected.begin==0 &&
           rejected.draw==0 && rejected.original_gpu_commands.empty());
  };
  auto wrong_count=source_frame;
  wrong_count.calls[0].gpr[4]=9;
  wrong_count.commands[2].gpr=wrong_count.calls[0].gpr;
  RejectSourceMismatch(wrong_count,capturedDraws);
  auto wrong_device=source_frame;
  wrong_device.calls[0].gpr[0]=0x40002884u;
  wrong_device.commands[2].gpr=wrong_device.calls[0].gpr;
  RejectSourceMismatch(wrong_device,capturedDraws);
  auto wrong_kind=source_frame;
  wrong_kind.calls[0].kind=xdk::Kind::DrawVertices;
  wrong_kind.calls[0].function=0x8288DF30u;
  wrong_kind.calls[0].gpr[2]=0; // DrawVertices first vertex
  wrong_kind.calls[0].gpr[3]=12; // DrawVertices vertex count
  wrong_kind.calls[0].gpr[4]=0; // no indexed count
  wrong_kind.commands[2].kind=xdk::CommandKind::DrawVertices;
  wrong_kind.commands[2].function=wrong_kind.calls[0].function;
  wrong_kind.commands[2].gpr=wrong_kind.calls[0].gpr;
  RejectSourceMismatch(wrong_kind,capturedDraws);
  // Upstream verified DrawVertices->sequential host index conversion is
  // the ONLY permitted non-indexed-to-indexed submission. Preserve the
  // existing mixed-primitive Vulkan path rather than dropping real objects.
  auto linearized=capturedDraws;
  linearized[0].source_nonindexed_linearization_verified=true;
  Backend converted;
  converted.accept_original_timeline=true;
  ReplayAotScene(geometry,linearized,shaders,effects,reflection,
                 converted,&wrong_kind);
  assert(converted.begin==1 && converted.draw==1 &&
         converted.composite==1);
  RejectSourceMismatch(source_frame,linearized); // wrong source method
  auto wrong_host_count=capturedDraws;
  wrong_host_count[0].draw.index_count=9;
  RejectSourceMismatch(source_frame,wrong_host_count);
  // A rejected source Present must abort the scene without presenting
  // the host swapchain, even after a successful Draw and Resolve.
  Backend terminal_rejected;
  terminal_rejected.accept_original_timeline=true;
  terminal_rejected.refuse_terminal_present=true;
  bool terminal_failed=false;
  try {ReplayAotScene(geometry,capturedDraws,shaders,effects,reflection,
                      terminal_rejected,&source_frame);}
  catch(const std::runtime_error&) {terminal_failed=true;}
  assert(terminal_failed && terminal_rejected.draw==1 &&
         terminal_rejected.resolve==1 && terminal_rejected.composite==0 &&
         terminal_rejected.abort==1);

  Backend cannot_present;
  bool rejected_original=false;
  try{ReplayAotScene(geometry,capturedDraws,shaders,effects,reflection,
                     cannot_present,&source_frame);}
  catch(const std::invalid_argument&){rejected_original=true;}
  assert(rejected_original && cannot_present.begin==0 &&
         cannot_present.preflights==0);
  auto missing_original_clear=source_frame;
  missing_original_clear.commands[1].captured_clear.reset();
  Backend invalid_original;
  invalid_original.accept_original_timeline=true;
  rejected_original=false;
  try{ReplayAotScene(geometry,capturedDraws,shaders,effects,reflection,
                     invalid_original,&missing_original_clear);}
  catch(const std::invalid_argument&){rejected_original=true;}
  assert(rejected_original && invalid_original.begin==0);
  auto wrong_full=captured;
  auto corrupted=std::make_shared<xdk::OriginalShaderRegisters>(
      *original_constants);
  corrupted->vertex[11*4]=1.f;
  wrong_full.draw.guest_shader_constants=corrupted;
  Backend conflicting;
  bool mismatch_refused=false;
  try{ReplayAotScene(geometry,std::span<const BoundIndexedDraw>(&wrong_full,1),
                     shaders,effects,reflection,conflicting);}
  catch(const std::invalid_argument&){mismatch_refused=true;}
  assert(mismatch_refused && conflicting.begin==0 && conflicting.draw==0);
  // The source frame is an ordered original PPC command stream: two
  // legitimate draws numbered 0,1 succeed, but reverse order, gaps,
  // missing leading draw and mixing offline/captured streams cannot begin.
  BoundIndexedDraw another=captured;
  another.draw.source_ordinal=1;
  std::vector<BoundIndexedDraw> ordered{captured,another};
  Backend orderedBackend;
  ReplayAotScene(geometry,ordered,shaders,effects,reflection,orderedBackend);
  assert(orderedBackend.begin==1 && orderedBackend.draw==2);
  auto RejectBeforeBegin=[&](std::vector<BoundIndexedDraw> broken) {
    Backend candidate;
    bool refused=false;
    try { ReplayAotScene(geometry,broken,shaders,effects,reflection,candidate); }
    catch(const std::invalid_argument&) {refused=true;}
    assert(refused && candidate.begin==0 && candidate.draw==0 &&
           candidate.preflights==0);
  };
  RejectBeforeBegin({another,captured});
  RejectBeforeBegin({another});
  auto wrongFrame=another;
  wrongFrame.draw.source_frame=18;
  RejectBeforeBegin({captured,wrongFrame});
  RejectBeforeBegin({captured,entry});
  RejectBeforeBegin({entry,captured});
  auto duplicate=captured;
  RejectBeforeBegin({captured,duplicate});
  captured.draw.guest_vs_matrices->finite_register_mask=0;
  capturedDraws={captured};
  Backend missing_constants;
  bool rejected_constants=false;
  try {ReplayAotScene(geometry,capturedDraws,shaders,effects,
                      reflection,missing_constants);}
  catch(const std::invalid_argument&){rejected_constants=true;}
  assert(rejected_constants && missing_constants.begin==0 &&
         missing_constants.draw==0);
  assert(target.samplers==1 && target.verified_program_pairs==1);
  assert(target.preflights==1);
  // Native backend refuses an unresolved actual PSO BEFORE BeginScene,
  // never presenting a partially rendered frame.
  Backend no_pso;
  no_pso.refuse_preflight=true;
  bool preflight_rejected=false;
  bool refused_before_gpu=true;
  try { ReplayAotScene(geometry,draws,shaders,effects,reflection,no_pso,
                       nullptr,&refused_before_gpu); }
  catch(const std::runtime_error&) { preflight_rejected=true; }
  assert(preflight_rejected && !refused_before_gpu && no_pso.preflights==1 &&
         no_pso.begin==0 && no_pso.draw==0);
  assert(target.surfaces==1 &&
         target.surface_history[0]==std::make_pair(0xCAFEu,0u));
  assert(target.received_vs==entry.vertex_source_sha256 &&
         target.received_ps==entry.pixel_source_sha256);
  assert(target.begin==1 && target.bind==1 && target.effect==1 &&
         target.constants==1 && target.draw==1 &&
         target.resolve==1 && target.composite==1);
  // Shared VS/PS and technique do not make two original models identical.
  BoundIndexedDraw other=entry;
  other.original_model_hash=0xBABE;
  other.original_surface_ordinal=7;
  Backend shared;
  draws={entry,other};
  ReplayAotScene(geometry,draws,shaders,effects,reflection,shared);
  assert(shared.surfaces==2 && shared.effect==2 && shared.draw==2);
  assert(shared.surface_history[0]==std::make_pair(0xCAFEu,0u));
  assert(shared.surface_history[1]==std::make_pair(0xBABEu,7u));
  other.original_model_hash=0;
  draws={entry,other};
  Backend rejected_owner;
  bool missing_owner=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,reflection,rejected_owner);}
  catch(const std::invalid_argument&){missing_owner=true;}
  assert(missing_owner && rejected_owner.begin==0);
  entry.effect_hash=0x456; // Shader pair exists but belongs to ANOTHER effect.
  draws={entry};
  bool effect_rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,reflection,target);}
  catch(const std::logic_error&){effect_rejected=true;}
  assert(effect_rejected && target.begin==1);
  entry.effect_hash=0x123;
  entry.pass_index=1; // Same shader bytes, wrong technique ordinal.
  draws={entry};
  bool pass_rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,reflection,target);}
  catch(const std::logic_error&){pass_rejected=true;}
  assert(pass_rejected && target.begin==1);
  entry.pass_index=0;
  entry.pixel_source_sha256.fill(0x33);
  draws={entry};
  bool rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,reflection,target);}
  catch(const std::logic_error&){rejected=true;}
  assert(rejected && target.begin==1); // Must reject before opening GPU RT.
  // Both shader records exist, but a malformed second draw must be rejected
  // before BeginScene, not after binding or submitting the first draw.
  entry.pixel_source_sha256.fill(0x22);
  auto later=entry;
  later.draw.index_buffer=0;
  Backend fresh;
  draws={entry,later};
  rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,reflection,fresh);}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected && fresh.begin==0 && fresh.bind==0 && fresh.draw==0);

  // A real sample/texture hash must reach the intended Draw only AFTER
  // validated VS/PS programs and before submission, not from guesses.
  gpu::VerifiedEorSampler texture;
  texture.original_model_hash=0xCAFE;
  texture.original_surface_ordinal=0;
  texture.original_effect_hash=0x123;
  texture.original_pass_index=0;
  texture.original_texture_hash=0xBEEF;
  texture.original_fetch_register=2;
  texture.stage=gpu::OriginalSamplerStage::Pixel;
  texture.exact_shader_variable_name="g_Diffuse";
  texture.original_material_ownership_verified=true;
  texture.original_shader_fetch_slot_verified=true;
  texture.original_texture_format_and_mips_verified=true;
  // The original dummy programs have no declared images. Replace only
  // the pixel module with an explicit named image variable for this test.
  std::vector<uint8_t> texturedBank{'H','E','O','R','S','P','V','1'};
  I32(texturedBank,2);I32(texturedBank,2);
  Add(texturedBank,0x11,1);
  Add(texturedBank,0x22,2,true);
  const auto texturedFile=std::filesystem::temp_directory_path()/
                          "hulk_original_textured_aot_test.bin";
  {std::ofstream out(texturedFile,std::ios::binary);
   out.write(reinterpret_cast<const char*>(texturedBank.data()),texturedBank.size());}
  ShaderBank texturedShaders;
  texturedShaders.Load(texturedFile);
  std::filesystem::remove(texturedFile);
  entry.original_samplers={texture};
  Backend textured;
  draws={entry};
  ReplayAotScene(geometry,draws,texturedShaders,effects,reflection,textured);
  assert(textured.begin==1 && textured.bind==1 && textured.samplers==1 &&
         textured.draw==1 && textured.abort==0);
  Backend blocked;
  auto invalid_texture=texture;
  invalid_texture.original_effect_hash=0xFAFE;
  entry.original_samplers={invalid_texture};
  draws={entry};
  bool wrong_owner=false;
  try {ReplayAotScene(geometry,draws,texturedShaders,effects,reflection,blocked);}
  catch(const std::invalid_argument&){wrong_owner=true;}
  assert(wrong_owner && blocked.begin==0 && blocked.samplers==0 &&
         blocked.draw==0);
  entry.original_samplers={texture};
  Backend missing_resource;
  missing_resource.refuse_samplers=true;
  draws={entry};
  bool refused=false;
  try {ReplayAotScene(geometry,draws,texturedShaders,effects,reflection,missing_resource);}
  catch(const std::runtime_error&){refused=true;}
  assert(refused && missing_resource.begin==1 && missing_resource.abort==1 &&
         missing_resource.draw==0 && missing_resource.resolve==0);
  // A partially compiled original shader bank must be usable for an exact
  // source-verified scene; the full effect and register-reflection ledgers
  // are still required. Absent shader variants can never be substituted.
  std::vector<uint8_t> subsetBytes{'H','E','O','R','S','P','V','1'};
  I32(subsetBytes,4);I32(subsetBytes,2);
  Add(subsetBytes,0x11,1);Add(subsetBytes,0x22,2);
  auto subsetPath=std::filesystem::temp_directory_path()/
                  "hulk_eor_subset_shader_test.bin";
  {std::ofstream out(subsetPath,std::ios::binary);
   out.write(reinterpret_cast<const char*>(subsetBytes.data()),subsetBytes.size());}
  ShaderBank subsetShaders;subsetShaders.Load(subsetPath);
  std::filesystem::remove(subsetPath);
  assert(subsetShaders.expected()==4 && subsetShaders.size()==2 &&
         !subsetShaders.HasFullArchiveCoverage());

  std::vector<uint8_t> fullEffects{'H','E','O','R','M','A','T','2'};
  I32(fullEffects,2);I32(fullEffects,2);
  for(uint32_t effect:{0x123u,0xABCDu}) {
    I32(fullEffects,effect);I32(fullEffects,0);
    for(int i=0;i<32;++i)fullEffects.push_back(effect==0x123u?0x11:0x33);
    for(int i=0;i<32;++i)fullEffects.push_back(effect==0x123u?0x22:0x44);
  }
  auto effectsPath=std::filesystem::temp_directory_path()/
                   "hulk_eor_subset_effects_test.bin";
  {std::ofstream out(effectsPath,std::ios::binary);
   out.write(reinterpret_cast<const char*>(fullEffects.data()),fullEffects.size());}
  EffectCatalog completeEffects;completeEffects.Load(effectsPath);
  std::filesystem::remove(effectsPath);
  assert(completeEffects.ShaderCount()==4);

  std::vector<uint8_t> fullReflection{'H','E','O','R','R','E','F','1'};
  I32(fullReflection,4);I32(fullReflection,1);
  for(uint8_t id:{uint8_t(0x11),uint8_t(0x22),uint8_t(0x33),uint8_t(0x44)}) {
    for(int i=0;i<32;++i)fullReflection.push_back(id);
    const bool vertex=id==0x11 || id==0x33;
    I32(fullReflection,vertex?1u:2u);
    I32(fullReflection,vertex?11u:UINT32_MAX);
    I32(fullReflection,vertex?4u:0u);
    for(int i=0;i<3;++i){I32(fullReflection,UINT32_MAX);I32(fullReflection,0);}
  }
  auto reflectionPath=std::filesystem::temp_directory_path()/
                      "hulk_eor_subset_reflection_test.bin";
  {std::ofstream out(reflectionPath,std::ios::binary);
   out.write(reinterpret_cast<const char*>(fullReflection.data()),fullReflection.size());}
  ReflectionBank completeReflection;completeReflection.Load(reflectionPath);
  std::filesystem::remove(reflectionPath);

  BoundIndexedDraw subsetDraw{{1,2,3,4,12,0},{},{}};
  subsetDraw.vertex_source_sha256.fill(0x11);
  subsetDraw.pixel_source_sha256.fill(0x22);
  subsetDraw.effect_hash=0x123;subsetDraw.pass_index=0;
  subsetDraw.original_model_hash=0xCAFE;subsetDraw.original_surface_ordinal=0;
  std::vector<BoundIndexedDraw> subsetDraws{subsetDraw};
  Backend subsetBackend;
  ReplayAotScene(geometry,subsetDraws,subsetShaders,completeEffects,
                 completeReflection,subsetBackend);
  assert(subsetBackend.begin==1 && subsetBackend.draw==1 &&
         subsetBackend.composite==1);
  auto missingVariant=subsetDraw;
  missingVariant.effect_hash=0xABCD;
  missingVariant.vertex_source_sha256.fill(0x33);
  missingVariant.pixel_source_sha256.fill(0x44);
  subsetDraws={missingVariant};
  Backend noFallback;
  bool missingWasRejected=false;
  try {ReplayAotScene(geometry,subsetDraws,subsetShaders,completeEffects,
                      completeReflection,noFallback);}
  catch(const std::logic_error&){missingWasRejected=true;}
  assert(missingWasRejected && noFallback.begin==0 && noFallback.draw==0);

}

