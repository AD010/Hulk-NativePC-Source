// Offscreen Vulkan integration. Supply the original USA INDEX.IND and TEXTURES.ARC.
#include "eor_diligent_texture_residency.h"
#include "eor_diligent_spirv_shaders.h"
#include "eor_effect_catalog.h"
#include "eor_original_font_source.h"
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
  bool matches=mip.row_pitch_bytes==2048 && mapped.Stride>=mip.row_pitch_bytes &&
               mip.blocks.size()==size_t(desc.Height)*mip.row_pitch_bytes;
  for (Uint32 y=0;matches && y<desc.Height;++y)
    matches=std::memcmp(static_cast<const std::byte*>(mapped.pData)+y*mapped.Stride,
                        mip.blocks.data()+size_t(y)*mip.row_pitch_bytes,mip.row_pitch_bytes)==0;
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
  desc.Size=sizeof(values);
  desc.Usage=USAGE_IMMUTABLE;
  desc.BindFlags=flags;
  BufferData data{&values,sizeof(values)};
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
                           const char* bank_path,const char* effects_path) {
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
  GraphicsPipelineStateCreateInfo ci{};
  ci.PSODesc.Name="Original USA font shader readback";
  ci.PSODesc.ResourceLayout.DefaultVariableType=SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
  ci.pVS=shaders.GetOrCreate(bank,vs,eor::aot::Stage::Vertex);
  ci.pPS=shaders.GetOrCreate(bank,ps,eor::aot::Stage::Pixel);
  auto& state=ci.GraphicsPipeline;
  state.NumRenderTargets=1;
  state.RTVFormats[0]=TEX_FORMAT_BGRA8_UNORM;
  state.DepthStencilDesc.DepthEnable=False;
  state.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  state.RasterizerDesc.CullMode=CULL_MODE_NONE;
  const LayoutElement layout[]={{0,0,4,VT_FLOAT32,False},{8,0,4,VT_FLOAT32,False},
                                {4,0,2,VT_FLOAT32,False}};
  state.InputLayout={layout,3};
  RefCntAutoPtr<IPipelineState> pipeline;
  device->CreateGraphicsPipelineState(ci,&pipeline);
  if (!pipeline) throw std::runtime_error("Original font pipeline failed");
  RefCntAutoPtr<IShaderResourceBinding> binding;
  pipeline->CreateShaderResourceBinding(&binding,true);
  if (!binding) throw std::runtime_error("Font descriptor creation failed");

  auto atlas=residency.Acquire(0xdad1dd22,eor::gpu::OriginalTextureColorSpace::Linear,
                              eor::gpu::OriginalTextureViewKind::Native2D);
  auto desc=atlas->host_texture->GetDesc();
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
  const float vertices[][10]={
      {0,0,0,1,1,1,1,1,0,0},{1,0,0,1,1,1,1,1,1,0},
      {0,1,0,1,1,1,1,1,0,1},{1,1,0,1,1,1,1,1,1,1}};
  const uint32_t indices[]={0,1,2,2,1,3};
  auto vb=BufferOf(device,BIND_VERTEX_BUFFER,vertices);
  auto ib=BufferOf(device,BIND_INDEX_BUFFER,indices);
  auto* rtv=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  context->SetRenderTargets(1,&rtv,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  const float clear[]={1,0,1,1};
  context->ClearRenderTarget(rtv,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetPipelineState(pipeline);
  Uint64 offset=0;
  auto* vertex_buffer=vb.RawPtr();
  context->SetVertexBuffers(0,1,&vertex_buffer,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetIndexBuffer(ib,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->DrawIndexed({6,VT_UINT32,DRAW_FLAG_VERIFY_ALL});
  context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
  auto source=assets.LoadHash(0xdad1dd22);
  const auto& gpu=*source->verified_gpu_mips;
  const auto texels=eor::textures::PrepareExplicitOriginalMips(
      gpu.format,gpu.layouts,source->original_bytes,{});
  CheckPixels(device,context,target,texels.mips.front());
  std::cout<<"USA font shader DrawIndexed matched 131072 rendered pixels\n";
}

int main(int argc,char** argv) {
  try {
    if (argc!=3 && argc!=5) throw std::invalid_argument("Expected USA INDEX.IND and TEXTURES.ARC paths");
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
    if(argc==5) VerifyFontDraw(device,context,assets,residency,argv[3],argv[4]);
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
