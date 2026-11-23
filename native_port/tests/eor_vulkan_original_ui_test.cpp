// Offscreen Vulkan integration. Supply the original USA INDEX.IND and TEXTURES.ARC.
#include "eor_diligent_texture_residency.h"
#include "eor_diligent_original_pipeline.h"
#include "eor_diligent_scene_targets.h"
#include "eor_diligent_material_textures.h"
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

static std::vector<std::byte> ReadPixels(IRenderDevice* device, IDeviceContext* context, ITexture* image) {
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
  const size_t pitch=size_t(desc.Width)*4;
  std::vector<std::byte> pixels(pitch*desc.Height);
  MappedTextureSubresource mapped{};
  context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
  if (!mapped.pData) throw std::runtime_error("Vulkan atlas readback failed");
  if(mapped.Stride>=pitch) {
    for(Uint32 y=0;y<desc.Height;++y)
      std::memcpy(pixels.data()+size_t(y)*pitch,
                  static_cast<const std::byte*>(mapped.pData)+y*mapped.Stride,pitch);
  }
  context->UnmapTextureSubresource(staging,0,0);
  if(mapped.Stride<pitch)throw std::runtime_error("Invalid Vulkan readback row stride");
  return pixels;
}

static void CheckPixels(IRenderDevice* device, IDeviceContext* context, ITexture* image,
                        const auto& mip) {
  if(mip.row_pitch_bytes!=size_t(image->GetDesc().Width)*4 ||
     ReadPixels(device,context,image)!=mip.blocks)
    throw std::runtime_error("Vulkan readback differs from expected pixels");
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

// USA FillRect pass 2 forwards clip-space position and outputs PS c21.
// Draw twice on one command list to check addressing and descriptor lifetime.
static void VerifyFillRectRegisters(IRenderDevice* device,IDeviceContext* context,
    eor::gpu::DiligentAotShaderModules& modules,const eor::aot::ShaderBank& bank,
    const eor::aot::EffectCatalog& effects) {
  using eor::aot::Stage;
  using eor::gpu::FloatRegisterAbi;
  eor::gpu::VerifiedOriginalGraphicsPass pass;
  pass.original_effect_hash=0x1edd5adf;
  pass.original_pass_index=2;
  pass.original_vertex_shader_sha256={
      0xf8,0x84,0x51,0x83,0x86,0xe3,0x64,0xce,0x02,0xf3,0x02,0x86,0x76,0xcc,0xde,0xd8,
      0xce,0x4d,0xa0,0x8b,0x49,0xba,0x31,0x74,0x60,0x3c,0x1c,0x57,0xaa,0x81,0x88,0x4e};
  pass.original_pixel_shader_sha256={
      0xfb,0xdf,0x6c,0x11,0x54,0x9d,0x17,0x82,0xb1,0x2b,0x72,0xef,0x03,0x64,0xdd,0x74,
      0xc0,0x2c,0x42,0x43,0x6b,0xd6,0x52,0x55,0x08,0x7f,0x6e,0xb3,0x51,0xf0,0x9e,0x81};
  pass.material_pass_verified=pass.original_render_state_verified=pass.original_host_vertex_layout_verified=true;
  pass.exact_host_input_layout={{0,0,4,VT_FLOAT32,False}};
  auto& gpu=pass.exact_render_state;
  gpu.NumRenderTargets=1;
  gpu.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
  gpu.DSVFormat=TEX_FORMAT_D32_FLOAT;
  gpu.DepthStencilDesc.DepthEnable=False;
  gpu.RasterizerDesc.ScissorEnable=True;
  gpu.RasterizerDesc.CullMode=CULL_MODE_NONE;
  gpu.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if(modules.GetFloatRegisterAbi(bank,pass.original_vertex_shader_sha256,Stage::Vertex)!=FloatRegisterAbi::Unused ||
     modules.GetFloatRegisterAbi(bank,pass.original_pixel_shader_sha256,Stage::Pixel)!=FloatRegisterAbi::Direct256)
    throw std::runtime_error("Original FillRect register ABI mismatch");
  auto pso=eor::gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,pass);
  TextureDesc desc{};
  desc.Type=RESOURCE_DIM_TEX_2D;
  desc.Width=64;desc.Height=32;
  desc.Format=gpu.RTVFormats[0];
  desc.BindFlags=BIND_RENDER_TARGET;
  RefCntAutoPtr<ITexture> color,depth;
  device->CreateTexture(desc,nullptr,&color);
  desc.Format=gpu.DSVFormat;desc.BindFlags=BIND_DEPTH_STENCIL;
  device->CreateTexture(desc,nullptr,&depth);
  if(!color || !depth)throw std::runtime_error("Register test attachments unavailable");
  ITextureView* rtv=color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  context->SetRenderTargets(1,&rtv,depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  const float bg[]={0,0,1,1};
  context->ClearRenderTarget(rtv,bg,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetPipelineState(pso.pipeline);
  const std::array<float,12> triangle{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
  auto vertex=BufferOf(device,BIND_VERTEX_BUFFER,triangle);
  auto index=BufferOf(device,BIND_INDEX_BUFFER,std::array<uint32_t,3>{0,1,2});
  IBuffer* stream=vertex;
  Uint64 offset=0;
  context->SetVertexBuffers(0,1,&stream,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context->SetIndexBuffer(index,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  auto shared=BufferOf(device,BIND_UNIFORM_BUFFER,std::array<uint32_t,8>{});
  for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
    pso.bindings->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
  auto paint=[&](const eor::gpu::OriginalVulkanPipeline& active,int half) {
    context->SetPipelineState(active.pipeline);
    Rect scissor{half*32,0,(half+1)*32,32};
    context->SetScissorRects(1,&scissor,64,32);
    std::array<float,1024> registers{};
    registers[21*4+half]=1;
    registers[21*4+3]=1;
    modules.BindDirectFloatRegisters(context,active.bindings,Stage::Pixel,registers);
    context->CommitShaderResources(active.bindings,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DrawIndexed({3,VT_UINT32,DRAW_FLAG_VERIFY_ALL});
  };
  paint(pso,0);
  paint(pso,1);
  context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
  const auto pixels=ReadPixels(device,context,color);
  for(size_t i=0;i<pixels.size();i+=4) {
    const bool left=(i/4)%64<32;
    const std::array<std::byte,4> expected{std::byte(left?255:0),std::byte(left?0:255),
                                           std::byte{0},std::byte{255}};
    if(!std::equal(expected.begin(),expected.end(),pixels.begin()+i))
      throw std::runtime_error("Original FillRect PS c21 lost its draw-specific value");
  }
  auto without_depth=pass;
  without_depth.exact_render_state.DSVFormat=TEX_FORMAT_UNKNOWN;
  without_depth.exact_render_state.DepthStencilDesc.DepthWriteEnable=False;
  auto expect_rejected=[&](const eor::gpu::VerifiedOriginalGraphicsPass& invalid) {
    try {
      eor::gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,invalid);
    } catch(const std::invalid_argument&) {return;}
    throw std::runtime_error("Accepted an unverified color-only PSO");
  };
  expect_rejected(without_depth);
  without_depth.original_depth_unbound=true;
  auto depth_writing=without_depth;
  depth_writing.exact_render_state.DepthStencilDesc.DepthWriteEnable=True;
  expect_rejected(depth_writing);
  auto color_only=eor::gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,without_depth);
  for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
    color_only.bindings->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);

  eor::gpu::DiligentSceneTargets scene(device,context);
  eor::native::FrameGeometry frame{};
  frame.direct_engine_path_ready=true;
  frame.world={64,32}; frame.backbuffer=frame.world; frame.composite={0,0,64,32}; frame.samples=1;
  eor::gpu::VerifiedSceneFormat formats{eor::gpu::NativeColorFormat::Rgba8,
      eor::gpu::NativeDepthFormat::D32Float,true,true,{}};
  formats.original_screen={1,2,3,0,true,false,false}; // Controlled attachment identities.
  eor::xdk::OriginalXdkClearF clear{};
  clear.flags=1; clear.has_color=true; clear.color={0,0,1,1};
  if(!scene.Prepare(frame,formats) || !scene.BeginScene() || !scene.ClearVerifiedOriginalXdk(clear,frame.world))
    throw std::runtime_error("Failed to initialize the depth-bound test scene");
  paint(pso,0);
  if(!scene.SetDepthBound(false))throw std::runtime_error("Failed to detach DSV");
  clear.flags=0x11;
  if(scene.ClearVerifiedOriginalXdk(clear,frame.world))
    throw std::runtime_error("Accepted a combined clear without a DSV");
  paint(color_only,1);
  clear.flags=0x10;
  if(!scene.SetDepthBound(true) || !scene.ClearVerifiedOriginalXdk(clear,frame.world) ||
     !scene.ResolveScene() || ReadPixels(device,context,scene.ResolvedColorSRV()->GetTexture())!=pixels)
    throw std::runtime_error("Detaching or restoring the DSV changed color pixels");

  formats.depth=eor::gpu::NativeDepthFormat::None;
  if(scene.Prepare(frame,formats))throw std::runtime_error("Accepted contradictory depth owner");
  formats.original_screen.guest_depth=0;
  clear.flags=1;
  if(!scene.Prepare(frame,formats) || !scene.BeginScene() || scene.SetDepthBound(true) ||
     !scene.ClearVerifiedOriginalXdk(clear,frame.world))
    throw std::runtime_error("Failed to initialize the color-only scene");
  paint(color_only,0);
  paint(color_only,1);
  if(!scene.ResolveScene() || ReadPixels(device,context,scene.ResolvedColorSRV()->GetTexture())!=pixels)
    throw std::runtime_error("Color-only rendering differs from the original two draws");
  std::cout<<"Original FillRect preserved 2048 pixels through DSV detach/reattach and color-only rendering\n";
  std::cout<<"Original FillRect PS c21 matched both draws and all 2048 pixels\n";
}

// This fixture exercises the original font shaders on a controlled quad.
// Game-generated glyph positions, UI events and presentation are separate.
static void VerifyFontDraw(IRenderDevice* device, IDeviceContext* context,
                           const eor::textures::OriginalXboxTextureAssets& assets,
                           eor::gpu::DiligentOriginalTextureResidency& residency,
                           const char* bank_path,const char* effects_path,const char* captured_path,
                           const char* fetch_path) {
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
  if(!captured_path && bank.HasFullArchiveCoverage())VerifyFillRectRegisters(device,context,shaders,bank,effects);
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
    const Uint32 scale=fetch_path ? 2u : 1u; // At 2x, sampling between atlas texels is observable.
    desc.Width=1280*scale;
    desc.Height=720*scale;
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
  auto render=[&](IShaderResourceBinding* descriptors) {
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
    context->CommitShaderResources(descriptors,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DrawIndexed({static_cast<Uint32>(indices.size()),VT_UINT32,DRAW_FLAG_VERIFY_ALL});
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
  };
  render(binding);
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
    if(fetch_path) {
      std::ifstream stream(fetch_path,std::ios::binary|std::ios::ate);
      if(stream.tellg()!=24)throw std::runtime_error("Expected a single original XDK fetch constant");
      std::array<uint32_t,6> words{};
      stream.seekg(0);
      for(auto& word:words)for(unsigned i=0;i<4;++i) {
        char byte;
        if(!stream.get(byte))throw std::runtime_error("Truncated XDK fetch constant");
        word=(word<<8)|static_cast<unsigned char>(byte);
      }
      // Independent oracle for this saved slot 0. Keep the exact CPU point
      // check above and compare the second full render byte-for-byte.
      auto glyph_binding=[&]() {
        RefCntAutoPtr<IShaderResourceBinding> out;
        pipeline->CreateShaderResourceBinding(&out,true);
        if(!out)throw std::runtime_error("Glyph SRB unavailable");
        for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
          out->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
        return out;
      };
      auto captured=glyph_binding();
      eor::gpu::BindUsaFontMainAtlas(device,captured,*atlas,words);
      render(captured);
      const auto sampled=ReadPixels(device,context,target);
      auto oracle=glyph_binding();
      SamplerDesc linear{};
      linear.MinFilter=linear.MagFilter=FILTER_TYPE_LINEAR;
      linear.MipFilter=FILTER_TYPE_POINT;
      linear.AddressU=linear.AddressV=TEXTURE_ADDRESS_WRAP;
      linear.AddressW=TEXTURE_ADDRESS_CLAMP;
      linear.MinLOD=linear.MaxLOD=0;
      RefCntAutoPtr<ISampler> expected;
      device->CreateSampler(linear,&expected);
      if(!expected)throw std::runtime_error("Reference sampler unavailable");
      oracle->GetVariableByName(SHADER_TYPE_PIXEL,"HulkTexture2D_s0")->Set(atlas->SRV());
      oracle->GetVariableByName(SHADER_TYPE_PIXEL,"HulkSampler_s0")->Set(expected);
      render(oracle);
      auto observed=ReadPixels(device,context,target);
      if(observed!=sampled)throw std::runtime_error("Captured sampler differs from linear reference");
      if(observed==reference.blocks)throw std::runtime_error("Sampling fixture did not distinguish linear from point");
      // Exercise the world material path's instruction override against the
      // same real descriptor. The actual Font_Main program above inherits it.
      auto overridden=glyph_binding();
      eor::gpu::BindOriginalPixelImage2D(device,overridden,*atlas,0,words,true);
      render(overridden);
      CheckPixels(device,context,target,reference);
      std::cout<<"Instruction point override matched the full point reference\n";
      std::cout<<"Captured XDK linear font sampler matched all "<<desc.Width*desc.Height<<" pixels\n";
    }
    return;
  }
  CheckPixels(device,context,target,texels.mips.front());
  std::cout<<"USA font shader DrawIndexed matched 131072 rendered pixels\n";
}

int main(int argc,char** argv) {
  try {
    if (argc!=3 && argc!=5 && argc!=6 && argc!=7) throw std::invalid_argument("Expected USA INDEX.IND and TEXTURES.ARC paths");
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
    if(argc>=5) VerifyFontDraw(device,context,assets,residency,argv[3],argv[4],argc>=6 ? argv[5] : nullptr,argc==7 ? argv[6] : nullptr);
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
