#include "../src/eor_original_pipeline_key.h"
#undef NDEBUG
#include <cassert>
#include <string>
#include <vector>
using namespace Diligent;
using hulk::eor::gpu::OriginalGraphicsPipelineKey;
int main() {
  GraphicsPipelineDesc state{};
  state.NumRenderTargets=1;
  state.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
  state.DSVFormat=TEX_FORMAT_D24_UNORM_S8_UINT;
  state.SmplDesc.Count=4;
  state.RasterizerDesc.ScissorEnable=true;
  std::string semantic="ATTRIB";
  std::vector<LayoutElement> layout{{0,0,4,VT_FLOAT32,False,0,16},
                                    {1,1,4,VT_UINT32,False,0,16}};
  layout[0].HLSLSemantic=semantic.c_str();
  state.InputLayout={layout.data(),static_cast<Uint32>(layout.size())};
  OriginalGraphicsPipelineKey key(state);
  auto independent=layout;
  independent[0].HLSLSemantic="ATTRIB";
  auto same=state;
  same.InputLayout.LayoutElements=independent.data();
  // The key is unaffected by destroying the source layout/semantic.
  layout.clear();layout.shrink_to_fit();semantic.assign(128,'x');
  assert(key.Matches(same));
  auto changed=same;changed.SmplDesc.Count=1;assert(!key.Matches(changed));
  changed=same;changed.SampleMask=7;assert(!key.Matches(changed));
  changed=same;changed.RTVFormats[0]=TEX_FORMAT_RGBA16_FLOAT;assert(!key.Matches(changed));
  changed=same;changed.DepthStencilDesc.DepthWriteEnable=false;assert(!key.Matches(changed));
  changed=same;changed.RasterizerDesc.CullMode=CULL_MODE_NONE;assert(!key.Matches(changed));
  changed=same;changed.BlendDesc.RenderTargets[0].BlendEnable=true;assert(!key.Matches(changed));
  independent[1].ValueType=VT_FLOAT32;assert(!key.Matches(same));
  independent[1].ValueType=VT_UINT32;independent[1].Stride=32;assert(!key.Matches(same));
  independent[1].Stride=16;independent[0].HLSLSemantic="POSITION";assert(!key.Matches(same));
  independent[0].HLSLSemantic="ATTRIB";assert(key.Matches(same));
  changed=same;changed.InputLayout.NumElements=1;assert(!key.Matches(changed));
  changed=same;changed.InputLayout.LayoutElements=nullptr;assert(!key.Matches(changed));
}
