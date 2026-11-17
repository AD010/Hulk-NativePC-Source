#include "../native_port/src/eor_aot_draw_bridge.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace hulk::eor;
using namespace hulk::eor::aot;
struct Backend final:ShaderBindingBackend {
  unsigned begin=0,bind=0,effect=0,constants=0,draw=0,resolve=0,composite=0,abort=0,samplers=0,surfaces=0;
  std::vector<std::pair<uint32_t,uint32_t>> surface_history;
  bool refuse_samplers=false;
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
  bool Composite(native::Rect r,native::Size) override {assert(r.height==1600);++composite;return true;}
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
  Backend target;
  std::vector<BoundIndexedDraw> draws{entry};
  ReplayAotScene(geometry,draws,shaders,effects,reflection,target);
  assert(target.samplers==1 && target.verified_program_pairs==1);
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
  auto other=texture;
  other.original_effect_hash=0xFAFE;
  entry.original_samplers={other};
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
}

