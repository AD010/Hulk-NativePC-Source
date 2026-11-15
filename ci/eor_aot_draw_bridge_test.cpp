#include "../native_port/src/eor_aot_draw_bridge.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace hulk::eor;
using namespace hulk::eor::aot;
struct Backend final:ShaderBindingBackend {
  unsigned begin=0,bind=0,effect=0,draw=0,resolve=0,composite=0,abort=0;
  bool BeginScene(native::Size size,uint32_t) override {
    assert(size.width==2560 && size.height==1600); ++begin;return true;}
  bool BindOriginalEffect(uint32_t hash) override {
    assert(hash == 0x123u); ++effect; return true;
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
void Add(std::vector<uint8_t>& b,uint8_t digest,uint32_t stage) {
  for(unsigned i=0;i<32;++i)b.push_back(digest);
  const std::vector<uint32_t> words{
      0x07230203u,0x10500u,0u,16u,0u,
      0x00020011u,1u,
      0x0003000eu,0u,1u,
      0x0005000fu,stage==1?0u:4u,1u,0x6e69616du,0u
  };
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
  std::vector<uint8_t> catalog{'H','E','O','R','M','A','T','1'};
  I32(catalog,2);I32(catalog,1);
  I32(catalog,0x123);I32(catalog,1);
  for(unsigned i=0;i<32;++i)catalog.push_back(0x11);
  I32(catalog,0x123);I32(catalog,2);
  for(unsigned i=0;i<32;++i)catalog.push_back(0x22);
  {std::ofstream out(effect_file,std::ios::binary);
   out.write(reinterpret_cast<const char*>(catalog.data()),catalog.size());}
  EffectCatalog effects;effects.Load(effect_file);std::filesystem::remove(effect_file);
  native::FrameGeometry geometry=native::PlanFrame({{2560,1600},{1280,720},true,true,true,4});
  BoundIndexedDraw entry{{1,2,3,4,12,0},{},{}};
  entry.vertex_source_sha256.fill(0x11);entry.pixel_source_sha256.fill(0x22);
  entry.effect_hash=0x123;
  Backend target;
  std::vector<BoundIndexedDraw> draws{entry};
  ReplayAotScene(geometry,draws,shaders,effects,target);
  assert(target.begin==1 && target.bind==1 && target.effect==1 && target.draw==1 &&
         target.resolve==1 && target.composite==1);
  entry.effect_hash=0x456; // Shader pair exists but belongs to ANOTHER effect.
  draws={entry};
  bool effect_rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,target);}
  catch(const std::logic_error&){effect_rejected=true;}
  assert(effect_rejected && target.begin==1);
  entry.effect_hash=0x123;
  entry.pixel_source_sha256.fill(0x33);
  draws={entry};
  bool rejected=false;
  try {ReplayAotScene(geometry,draws,shaders,effects,target);}
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
  try {ReplayAotScene(geometry,draws,shaders,effects,fresh);}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected && fresh.begin==0 && fresh.bind==0 && fresh.draw==0);
}

