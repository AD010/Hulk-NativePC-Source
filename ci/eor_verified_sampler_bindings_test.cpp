#include "../native_port/src/eor_verified_sampler_bindings.h"
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::gpu;
template <typename F> void Reject(F&& f) {
  bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}
  assert(failed);
}
int main() {
  VerifiedEorSampler s;
  s.original_model_hash=0xCAFE;
  s.original_surface_ordinal=3;
  s.original_effect_hash=0x1234;
  s.original_pass_index=1;
  s.original_texture_hash=0xABCDE;
  s.original_fetch_register=5;
  s.stage=OriginalSamplerStage::Pixel;
  s.exact_shader_variable_name="g_Texture0";
  s.original_material_ownership_verified=true;
  s.original_shader_fetch_slot_verified=true;
  s.original_texture_format_and_mips_verified=true;
  ValidateOriginalEorSamplers(std::span(&s,1));
  auto two=s;two.original_texture_hash=0xDECAF;
  two.original_fetch_register=6;two.exact_shader_variable_name="g_Texture1";
  ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,two});
  const auto test=[&](auto mutate) {
    auto bad=s;mutate(bad);
    Reject([&]{ValidateOriginalEorSamplers(std::span(&bad,1));});
  };
  test([](auto& v){v.original_model_hash=0;});
  test([](auto& v){v.original_surface_ordinal=UINT32_MAX;});
  test([](auto& v){v.original_effect_hash=0;});
  test([](auto& v){v.original_pass_index=UINT32_MAX;});
  test([](auto& v){v.original_texture_hash=0;});
  test([](auto& v){v.original_fetch_register=UINT32_MAX;});
  test([](auto& v){v.original_fetch_register=128;});
  test([](auto& v){v.stage=OriginalSamplerStage::Unknown;});
  test([](auto& v){v.exact_shader_variable_name="g_\\unsafe";});
  test([](auto& v){v.exact_shader_variable_name.clear();});
  test([](auto& v){v.original_material_ownership_verified=false;});
  test([](auto& v){v.original_shader_fetch_slot_verified=false;});
  test([](auto& v){v.original_texture_format_and_mips_verified=false;});
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,s});});
  auto bad=s;bad.original_fetch_register=6;
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,bad});});
  bad=s;bad.original_texture_hash=0x1234567;bad.exact_shader_variable_name="g_Alias";
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,bad});});
  bad=two;bad.original_surface_ordinal=4;
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,bad});});
  bad=two;bad.original_effect_hash=0xA;
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,bad});});
  bad=two;bad.original_pass_index=4;
  Reject([&]{ValidateOriginalEorSamplers(std::vector<VerifiedEorSampler>{s,bad});});
  ValidateOriginalEorSamplers(std::span<const VerifiedEorSampler>{});
}
