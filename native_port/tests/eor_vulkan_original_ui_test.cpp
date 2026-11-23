// Offscreen Vulkan integration. Supply the original USA INDEX.IND and TEXTURES.ARC.
#include "eor_diligent_texture_residency.h"
#include "eor_diligent_original_pipeline.h"
#include "eor_effect_catalog.h"
#include "eor_original_font_source.h"
#include "eor_native_font_geometry.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <array>
#include "Buffer.h"
#include "PipelineState.h"
#include "Sampler.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace Diligent;
namespace eor = hulk::eor;

static void CheckPixels(IRenderDevice* device, IDeviceContext* context, ITexture* image,
                        const auto& mip) {
  auto desc=image->GetDesc();
  desc.Name="Original USA atlas readback";
  desc.Usage=USAGE_STAGING;
  desc.BindFlags=BIND_NONE;
  desc.CPUAccessFlags=CPU_ACCESS_READ;
  RefCntAutoPtr<ITexture> staging;
  device->CreateTexture(desc,nullptr,&staging);
  if (!staging) throw std::runtime_error("Vulkan staging texture unavailable");
  CopyTextureAttribs copy{image,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                          staging,RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
  context->CopyTexture(copy);
  context->WaitForIdle();
  MappedTextureSubresource mapped{};
  context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
  if (!mapped.pData) throw std::runtime_error("Vulkan atlas readback failed");
  bool matches=mip.row_pitch_bytes==size_t(desc.Width)*4 && mapped.Stride>=mip.row_pitch_bytes &&
               mip.blocks.size()==size_t(desc.Height)*mip.row_pitch_bytes;
  size_t differing=0;
  if(matches) for(Uint32 y=0;y<desc.Height;++y) {
    const auto* actual=static_cast<const std::byte*>(mapped.pData)+y*mapped.Stride;
    const auto* expected=mip.blocks.data()+size_t(y)*mip.row_pitch_bytes;
    if(std::memcmp(actual,expected,mip.row_pitch_bytes)==0)continue;
    for(Uint32 i=0;i<mip.row_pitch_bytes;++i) if(actual[i]!=expected[i]) {
      if(differing<8) std::cerr<<"readback ("<<i/4<<","<<y<<") channel "<<i%4
          <<" got "<<std::to_integer<unsigned>(actual[i])<<" expected "
          <<std::to_integer<unsigned>(expected[i])<<"\n";
      ++differing;
    }
  }
  matches = matches && differing==0;
  context->UnmapTextureSubresource(staging,0,0);
  if (!matches) throw std::runtime_error("Vulkan readback differs from decoded atlas");
}

static void VerifyAtlas(IRenderDevice* device, IDeviceContext* context,
                        const eor::textures::OriginalXboxTextureAssets& assets,
                        eor::gpu::DiligentOriginalTextureResidency& residency, uint32_t hash) {
  const auto original=assets.LoadHash(hash);
  if (!original || !original->verified_gpu_mips || original->original_stream)
    throw std::runtime_error("Expected an original resident UI texture");
  const auto& gpu=*original->verified_gpu_mips;
  const auto source=eor::textures::PrepareExplicitOriginalMips(
      gpu.format,gpu.layouts,original->original_bytes,{});
  const auto uploaded=residency.Acquire(
      hash,eor::gpu::OriginalTextureColorSpace::Linear,
      eor::gpu::OriginalTextureViewKind::Native2D);
  auto desc=uploaded->host_texture->GetDesc();
  if (!uploaded->SRV() || desc.Format!=TEX_FORMAT_BGRA8_UNORM || desc.Width!=512 ||
      desc.Height!=256 || desc.MipLevels!=1 || source.mips.size()!=1)
    throw std::runtime_error("Unexpected native UI view or mip layout");

  CheckPixels(device,context,uploaded->host_texture,source.mips.front());
  const auto& mip=source.mips.front();
  std::cout<<std::hex<<hash<<std::dec<<" matched "<<mip.blocks.size()<<" bytes\n";
}

static RefCntAutoPtr<IBuffer> BufferOf(IRenderDevice* device, BIND_FLAGS flags, const auto& values) {
  BufferDesc desc{};
  desc.Name="Font shader test data";
  const auto bytes=std::as_bytes(std::span{values});
  desc.Size=bytes.size();
  desc.Usage=USAGE_IMMUTABLE;
  desc.BindFlags=flags;
  BufferData data{bytes.data(),bytes.size()};
  RefCntAutoPtr<IBuffer> buffer;
  device->CreateBuffer(desc,&data,&buffer);
  if (!buffer) throw std::runtime_error("Test buffer creation failed");
  return buffer;
}

// This fixture exercises the original font shaders on a controlled quad.
// Game-generated glyph positions, UI events and presentation are separate.
static void VerifyFontDraw(IRenderDevice* device, IDeviceContext* context,
                           const eor::textures::OriginalXboxTextureAssets& assets,
                           eor::gpu::DiligentOriginalTextureResidency& residency,
                           const char* bank_path,const char* effects_path,const char* captured_path) {
  eor::aot::EffectCatalog effects;
  effects.Load(effects_path);
  eor::aot::ShaderBank bank;
  bank.Load(bank_path);
  const auto& vs=eor::ui::kOriginalFontMainVsSha;
  const auto& ps=eor::ui::kOriginalFontMainPsSha;
  if (!effects.HasPair(eor::ui::kOriginalFontMainEffectHash,
                       eor::ui::kOriginalFontMainPassIndex,vs,ps))
    throw std::runtime_error("USA Font_Main source pair missing");
  eor::gpu::DiligentAotShaderModules shaders(device);
  eor::gpu::VerifiedOriginalGraphicsPass pass;
  pass.original_effect_hash=eor::ui::kOriginalFontMainEffectHash;
  pass.original_pass_index=eor::ui::kOriginalFontMainPassIndex;
  pass.original_vertex_shader_sha256=vs;
  pass.original_pixel_shader_sha256=ps;
  // This test supplies its own framebuffer and fixed render state. The
  // shader pair, glyph bytes and atlas come from the USA assets.
  pass.material_pass_verified=pass.original_render_state_verified=true;
  pass.original_host_vertex_layout_verified=pass.original_dynamic_font_vertex_layout_verified=true;
  auto& state=pass.exact_render_state;
  state.NumRenderTargets=1;
  state.RTVFormats[0]=TEX_FORMAT_BGRA8_UNORM;
  state.DSVFormat=TEX_FORMAT_D32_FLOAT;
  state.DepthStencilDesc.DepthEnable=False;
  state.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  state.RasterizerDesc.CullMode=CULL_MODE_NONE;
  pass.exact_host_input_layout={{0,0,3,VT_FLOAT32,False},{4,1,2,VT_FLOAT32,False},
                                {8,2,4,VT_FLOAT32,False}};
  auto pso=eor::gpu::CreateOriginalVulkanPipeline(device,shaders,bank,effects,pass);
  auto& pipeline=pso.pipeline;
  auto& binding=pso.bindings;

  auto atlas=residency.Acquire(0xdad1dd22,eor::gpu::OriginalTextureColorSpace::Linear,
                              eor::gpu::OriginalTextureViewKind::Native2D);
  auto desc=atlas->host_texture->GetDesc();
  const auto atlas_width=desc.Width, atlas_height=desc.Height;
  if(captured_path) {
    // Replay normalized game positions on the original USA 720p framebuffer.
    desc.Width=1280;
    desc.Height=720;
  }
  desc.Name="Font raster output";
  desc.BindFlags=BIND_RENDER_TARGET;
  RefCntAutoPtr<ITexture> target;
  device->CreateTexture(desc,nullptr,&target);
  if (!target) throw std::runtime_error("Font target missing");
  SamplerDesc sampling{};
  sampling.MinFilter=sampling.MagFilter=sampling.MipFilter=FILTER_TYPE_POINT;
  sampling.AddressU=sampling.AddressV=sampling.AddressW=TEXTURE_ADDRESS_CLAMP;
  RefCntAutoPtr<ISampler> sampler;
  device->CreateSampler(sampling,&sampler);
  if (!sampler) throw std::runtime_error("Font sampler missing");
  std::array<uint32_t,8> shared_values{}; // Identity UVs, zero half-pixel offset, alpha test off.
  auto shared=BufferOf(device,BIND_UNIFORM_BUFFER,shared_values);
  auto set=[&](SHADER_TYPE stage,const char* name,IDeviceObject* value) {
    auto* var=binding->GetVariableByName(stage,name);
    if (!var) throw std::runtime_error(std::string("Missing font shader variable ")+name);
    var->Set(value);
  };
  set(SHADER_TYPE_PIXEL,"HulkTexture2D_s0",atlas->SRV());
  set(SHADER_TYPE_PIXEL,"HulkSampler_s0",sampler);
  set(SHADER_TYPE_PIXEL,"HulkSharedConstants",shared);
  set(SHADER_TYPE_VERTEX,"HulkSharedConstants",shared);
  std::vector<std::array<float,10>> vertices={
      {0,0,0,1,1,1,1,1,0,0},{1,0,0,1,1,1,1,1,1,0},
      {0,1,0,1,1,1,1,1,0,1},{1,1,0,1,1,1,1,1,1,1}};
  std::vector<uint32_t> indices={0,1,2,2,1,3};
  if(captured_path) {
    std::ifstream f(captured_path,std::ios::binary|std::ios::ate);
    const auto size=f.tellg();
    if(size<=0 || size>65535*24) throw std::runtime_error("Invalid source glyph payload size");
    std::vector<std::byte> raw(static_cast<size_t>(size));
    f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(raw.data()),size)) throw std::runtime_error("Unreadable glyph payload");
    const auto batch=eor::ui::DecodeUsaInlineGlyphPayload(raw);
    indices=batch.triangle_indices;
    vertices.clear();
    for(size_t i=0;i<batch.positions.size();++i) {
      const auto& pos=batch.positions[i];const auto& uv=batch.texcoords[i];const auto& col=batch.colors[i];
      vertices.push_back({pos[0],pos[1],pos[2],1.f,col[0],col[1],col[2],col[3],uv[0],uv[1]});
    }
  }
  std::vector<std::array<float,4>> positions,uvs,colors;
  for(const auto& v:vertices) {
    positions.push_back({v[0],v[1],v[2],v[3]});
    uvs.push_back({v[8],v[9],0,0});
    colors.push_back({v[4],v[5],v[6],v[7]});
  }
  auto xyz=BufferOf(device,BIND_VERTEX_BUFFER,positions);
  auto uv=BufferOf(device,BIND_VERTEX_BUFFER,uvs);
  auto rgba=BufferOf(device,BIND_VERTEX_BUFFER,colors);
  auto ib=BufferOf(device,BIND_INDEX_BUFFER,indices);
  auto* rtv=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  auto depth_desc=desc;
  depth_desc.Name="Captured glyph test depth";
  depth_desc.BindFlags=BIND_DEPTH_STENCIL;
  depth_desc.Format=state.DSVFormat;
  RefCntAutoPtr<ITexture> depth;
  device->CreateTexture(depth_desc,nullptr,&depth);
  if(!depth) throw std::runtime_error("Font depth attachment unavailable");
  context->SetRenderTargets(1,&rtv,depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Rect clip{0,0,static_cast<Int32>(desc.Width),static_cast<Int32>(desc.Height)};
  context->SetScissorRects(1,&clip,desc.Width,desc.Height);
  const float clear[]={1,0,1,1};
  context->ClearRenderTarget(rtv,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetPipelineState(pipeline);
  Uint64 offsets[3]{};
  IBuffer* streams[]={xyz,uv,rgba};
  context->SetVertexBuffers(0,3,streams,offsets,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetIndexBuffer(ib,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->DrawIndexed({static_cast<Uint32>(indices.size()),VT_UINT32,DRAW_FLAG_VERIFY_ALL});
  context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
  auto source=assets.LoadHash(0xdad1dd22);
  const auto& gpu=*source->verified_gpu_mips;
  const auto texels=eor::textures::PrepareExplicitOriginalMips(
      gpu.format,gpu.layouts,source->original_bytes,{});
  if(captured_path) {
    // Independent reference for this capture: axis-aligned glyph quads,
    // point/clamp sampling, vertex color modulation and source order.
    // Compare the entire attachment, including untouched background.
    const auto& mip=texels.mips.front();
    auto reference=mip;
    reference.row_pitch_bytes=desc.Width*4;
    reference.blocks.resize(size_t(reference.row_pitch_bytes)*desc.Height);
    constexpr std::array<std::byte,4> magenta{
        std::byte{255},std::byte{0},std::byte{255},std::byte{255}};
    for(size_t at=0;at<reference.blocks.size();at+=4)
      std::copy(magenta.begin(),magenta.end(),reference.blocks.begin()+at);
    size_t shaded=0,visible=0;
    for(size_t first=0;first<vertices.size();first+=4) {
      const auto& tl=vertices[first];const auto& tr=vertices[first+1];
      const auto& br=vertices[first+2];const auto& bl=vertices[first+3];
      if(tl[0]!=bl[0] || tl[1]!=tr[1] || br[0]!=tr[0] || br[1]!=bl[1] ||
         br[0]<=tl[0] || br[1]<=tl[1] || tl[8]!=bl[8] || tl[9]!=tr[9] ||
         tr[8]!=br[8] || bl[9]!=br[9])
        throw std::runtime_error("Fixture needs screen-aligned textured glyphs");
      for(size_t j=1;j<4;++j)
        if(!std::equal(tl.begin()+4,tl.begin()+8,vertices[first+j].begin()+4))
          throw std::runtime_error("Fixture needs uniform glyph color");
      for(Uint32 y=0;y<desc.Height;++y) for(Uint32 x=0;x<desc.Width;++x) {
        const double fx=((x+0.5)/desc.Width-tl[0])/(br[0]-tl[0]);
        const double fy=((y+0.5)/desc.Height-tl[1])/(br[1]-tl[1]);
        if(fx<0 || fx>=1 || fy<0 || fy>=1)continue;
        auto texel=[](double uv,Uint32 size) {
          return static_cast<Uint32>(std::clamp(std::floor(uv*size),0.,double(size-1)));
        };
        const auto sx=texel(tl[8]+fx*(br[8]-tl[8]),atlas_width);
        const auto sy=texel(tl[9]+fy*(br[9]-tl[9]),atlas_height);
        const size_t src=sy*mip.row_pitch_bytes+sx*4,dst=y*reference.row_pitch_bytes+x*4;
        const unsigned bgra[]={6,5,4,7};
        for(unsigned ch=0;ch<4;++ch) {
          auto value=std::to_integer<uint8_t>(mip.blocks[src+ch])*tl[bgra[ch]];
          reference.blocks[dst+ch]=std::byte(std::lround(value));
        }
        ++shaded;
        if(reference.blocks[dst+3]!=std::byte{0})++visible;
      }
    }
    if(!shaded || !visible || visible==shaded)
      throw std::runtime_error("Glyph capture did not exercise the atlas alpha mask");
    CheckPixels(device,context,target,reference);
    std::cout<<vertices.size()/4<<" captured glyphs, "<<shaded<<" fragments, "
             <<visible<<" with nonzero alpha; full target matched\n";
    return;
  }
  CheckPixels(device,context,target,texels.mips.front());
  std::cout<<"USA font shader DrawIndexed matched 131072 rendered pixels\n";
}

int main(int argc,char** argv) {
  try {
    if (argc!=3 && argc!=5 && argc!=6) throw std::invalid_argument("Expected USA INDEX.IND and TEXTURES.ARC paths");
    const auto assets=eor::textures::OriginalXboxTextureAssets::OpenPinnedUsa(argv[1],argv[2]);
    auto* factory=LoadAndGetEngineFactoryVk();
    if (!factory) throw std::runtime_error("Diligent Vulkan factory unavailable");
    EngineVkCreateInfo ci{};
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    factory->CreateDeviceAndContextsVk(ci,&device,&context);
    if (!device || !context) throw std::runtime_error("Vulkan device unavailable");
    std::cout<<device->GetAdapterInfo().Description<<'\n';
    eor::gpu::DiligentOriginalTextureResidency residency(device,assets);
    VerifyAtlas(device,context,assets,residency,0x995ad5dc);
    VerifyAtlas(device,context,assets,residency,0xdad1dd22);
    if(argc>=5) VerifyFontDraw(device,context,assets,residency,argv[3],argv[4],argc==6 ? argv[5] : nullptr);
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
