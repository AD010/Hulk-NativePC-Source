// Offscreen Vulkan integration. Supply the original USA INDEX.IND and TEXTURES.ARC.
#include "eor_diligent_texture_residency.h"
#include "eor_diligent_original_pipeline.h"
#include "eor_diligent_aot_backend.h"
#include "eor_diligent_material_textures.h"
#include "eor_effect_catalog.h"
#include "eor_original_font_source.h"
#include "eor_native_font_geometry.h"
#include "eor_live_material_producer.h"
#include "eor_spirv_clip_position.h"
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
  const size_t bytes_per_pixel=desc.Format==TEX_FORMAT_RGBA16_FLOAT?8u:4u;
  if(desc.SampleCount!=1 || (desc.Format!=TEX_FORMAT_RGBA16_FLOAT && desc.Format!=TEX_FORMAT_RGBA8_UNORM && desc.Format!=TEX_FORMAT_BGRA8_UNORM))
    throw std::runtime_error("Unsupported integration-test readback format");
  const size_t pitch=size_t(desc.Width)*bytes_per_pixel;
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

static eor::gpu::VerifiedOriginalGraphicsPass FillRectPass() {
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
  return pass;
}

// USA FillRect pass 2 forwards clip-space position and outputs PS c21.
// Draw twice on one command list to check addressing and descriptor lifetime.
static void VerifyFillRectRegisters(IRenderDevice* device,IDeviceContext* context,
    eor::gpu::DiligentAotShaderModules& modules,const eor::aot::ShaderBank& bank,
    const eor::aot::EffectCatalog& effects) {
  using eor::aot::Stage;
  using eor::gpu::FloatRegisterAbi;
  auto pass=FillRectPass();
  const auto& gpu=pass.exact_render_state;
  if(modules.GetFloatRegisterAbi(bank,pass.original_vertex_shader_sha256,Stage::Vertex)!=FloatRegisterAbi::Unused ||
     modules.GetFloatRegisterAbi(bank,pass.original_pixel_shader_sha256,Stage::Pixel)!=FloatRegisterAbi::Direct256)
    throw std::runtime_error("Original FillRect register ABI mismatch");
  auto pso=eor::gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,pass);
  auto repeated=eor::gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,pass);
  if(pso.pipeline.RawPtr()!=repeated.pipeline.RawPtr() ||
     pso.bindings.RawPtr()==repeated.bindings.RawPtr())
    throw std::runtime_error("Immutable PSO reuse lost independent mutable SRBs");
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
  for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL}) {
    pso.bindings->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
    repeated.bindings->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
  }
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
  paint(repeated,1);
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
  if(color_only.pipeline.RawPtr()==pso.pipeline.RawPtr())
    throw std::runtime_error("Distinct depth attachment state incorrectly reused a PSO");
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

// Real GPU readback of the production target graph. Separate owner IDs
// must retain distinct contents across intermediate Resolve and final output.
static void VerifySequentialTargets(IRenderDevice* device,IDeviceContext* context) {
  {
    eor::gpu::DiligentMeshUploadBackend meshes(device);
    const std::array<uint32_t,3> indices{0,1,2},changed{0,2,1};
    const auto first=meshes.UploadIndices(indices);
    const auto second=meshes.UploadIndices(indices);
    const auto different=meshes.UploadIndices(changed);
    if(!first || !second || !different || first==second ||
       meshes.FindBuffer(first)!=meshes.FindBuffer(second) ||
       meshes.FindBuffer(first)==meshes.FindBuffer(different) ||
       !meshes.IndexWindowInRange(second,0,3,0,3))
      throw std::runtime_error("Immutable Vulkan mesh cache identity mismatch");
    meshes.ReleaseBuffer(first);
    if(meshes.FindBuffer(first) || !meshes.FindBuffer(second))
      throw std::runtime_error("Releasing one Draw lost another GPU owner");
    meshes.ReleaseBuffer(second);
    const auto next=meshes.UploadIndices(indices);
    if(!next || next==second || !meshes.IndexWindowInRange(next,0,3,0,3))
      throw std::runtime_error("Reacquired GPU index buffer lost source provenance");
  }
  namespace gpu=eor::gpu;
  constexpr uint32_t w=64,h=32;
  const eor::native::Size size{w,h};
  eor::native::FrameGeometry frame{size,size,{0,0,w,h},1,false,true,size};
  gpu::VerifiedSceneFormat format{gpu::NativeColorFormat::Rgba8,
      gpu::NativeDepthFormat::None,true,true,{1,3,0,5,true,true,false}};
  auto& graph=format.original_graph;
  graph.colors={2,3};graph.resolves={4};
  graph.extents={{2,{w/2,h/2}},{3,{w,h}},{4,{w/2,h/2}}};
  graph.color_formats={{2,gpu::NativeColorFormat::Rgba16Float},{3,gpu::NativeColorFormat::Rgba8},{4,gpu::NativeColorFormat::Rgba16Float}};
  graph.sample_counts={{2,4},{3,1},{4,1}};
  graph.partial_msaa_sources.insert(2);
  graph.first_color=2;graph.final_color=3;graph.front_buffer=5;
  gpu::DiligentSceneTargets scene(device,context);
  eor::xdk::OriginalXdkClearF clear{};
  clear.flags=1;clear.has_color=true;clear.color={1,0,0,1};
  if(!scene.Prepare(frame,format) || !scene.BeginScene() ||
     scene.IntermediateColorSRV(4) ||
     !scene.ClearVerifiedOriginalXdk(clear,{w/2,h/2}) ||
     scene.ResolveOriginalIntermediate(99) ||
     !scene.ResolveOriginalIntermediate(4) ||
     !scene.IntermediateColorSRV(4))
    throw std::runtime_error("Sequential native target initialization failed");
  // Actual partial FP16 4x MSAA Resolve. Preserve red outside the
  // destination region and update only its 16x8 bottom-right corner.
  clear.color={0,0,1,1};
  const eor::xdk::OriginalResolveRegion region{{0,0,16,8},{16,8}};
  if(!scene.ClearVerifiedOriginalXdk(clear,{w/2,h/2}) ||
     !scene.ResolveOriginalIntermediate(4,region) ||
     !scene.SetOriginalColorTarget(3))
    throw std::runtime_error("Partial FP16 MSAA Resolve failed");
  clear.color={0,0,1,1};
  if(!scene.ClearVerifiedOriginalXdk(clear,size) || !scene.ResolveScene())
    throw std::runtime_error("Sequential native target final resolve failed");
  const auto first=ReadPixels(device,context,scene.IntermediateColorSRV(4)->GetTexture());
  const auto final=ReadPixels(device,context,scene.ResolvedColorSRV()->GetTexture());
  if(first.size()!=(w/2)*(h/2)*8 || final.size()!=w*h*4)
    throw std::runtime_error("Sequential target readback size mismatch");
  // Native IEEE half floats {1,0,0,1} in little-endian host storage.
  const std::array red{std::byte{0},std::byte{0x3c},std::byte{0},std::byte{0},
                       std::byte{0},std::byte{0},std::byte{0},std::byte{0x3c}};
  const std::array blue{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
  const std::array blue16{std::byte{0},std::byte{0},std::byte{0},std::byte{0},
                          std::byte{0},std::byte{0x3c},std::byte{0},std::byte{0x3c}};
  for(uint32_t y=0;y<h/2;++y)for(uint32_t x=0;x<w/2;++x) {
    const auto& expected=x>=16 && y>=8 ? blue16:red;
    if(!std::equal(expected.begin(),expected.end(),first.begin()+(size_t(y)*(w/2)+x)*8))
      throw std::runtime_error("Partial MSAA Resolve changed pixels outside exact region");
  }
  for(size_t i=0;i<final.size();i+=4)
    if(!std::equal(blue.begin(),blue.end(),final.begin()+i))
      throw std::runtime_error("Final target contents changed");
  if(!scene.Prepare(frame,format) || !scene.BeginScene() || scene.IntermediateColorSRV(4))
    throw std::runtime_error("Stale intermediate target survived into the next frame");
  scene.AbortScene();
  std::cout<<"Native mixed RGBA16F 4x MSAA and RGBA8 1x sequential targets readback passed\n";
}

// Controlled XDK state for the original shader integration fixture only.
// Production Draws read these words from the real USA device shadow.
static eor::xdk::OriginalXdkFixedGraphicsState FixtureFixedState(
    uint32_t device,uint32_t color_mask=15,uint32_t samples=0) {
  using State=eor::xdk::OriginalXdkFixedGraphicsState;
  State result{};
  result.original_guest_device=device;
  const auto set=[&](uint32_t reg,uint32_t value) {
    size_t base=0;
    for(size_t g=0;g<State::kCounts.size();++g) {
      const auto first=State::kRegisterBases[g];
      if(reg>=first && reg-first<State::kCounts[g]) {
        result.original_register_values[base+reg-first]=value;
        return;
      }
      base+=State::kCounts[g];
    }
    throw std::runtime_error("Fixture XDK register outside captured groups");
  };
  set(0x2000,samples<<16); // RGBA8/D24S8, exact sample count
  set(0x2104,color_mask);
  set(0x2201,1u|(1u<<16)); // opaque: ONE/ZERO, ADD for color and alpha
  return result;
}

// Original FillRect programs and reflection, controlled XDK commands and mesh.
// Drive the production material lookup, scene preflight, DrawIndexed and Resolve.
static void VerifyMaterialReplay(IRenderDevice* device,IDeviceContext* context,
    const char* bank_path,const char* effects_path,const char* reflection_path) {
  namespace gpu=eor::gpu;
  namespace xdk=eor::xdk;
  eor::aot::ShaderBank bank; bank.Load(bank_path);
  eor::aot::EffectCatalog effects; effects.Load(effects_path);
  eor::aot::ReflectionBank reflection; reflection.Load(reflection_path);
  gpu::DiligentAotShaderModules shaders(device);
  gpu::DiligentSceneTargets scene(device,context);
  gpu::DiligentMeshUploadBackend meshes(device);
  constexpr uint32_t width=320,height=200;
  const eor::native::Size size{width,height};
  eor::native::FrameGeometry frame{size,size,{0,0,width,height},1,false,true,size};
  gpu::VerifiedSceneFormat format{gpu::NativeColorFormat::Rgba8,gpu::NativeDepthFormat::D24S8,
      true,true,{1,2,3,4,true,true,false}};
  const auto& screen=format.original_screen;
  const std::array<eor::model::NativePosition,3> triangle{{{-1,-1,0},{3,-1,0},{-1,3,0}}};
  const auto pos=meshes.UploadPositions(triangle);
  const auto ib=meshes.UploadIndices(std::array<uint32_t,3>{0,1,2});
  constexpr uint32_t model=1; // Fixture owner, never an archive model identity.
  gpu::NativeEorDrawBuffers buffers{pos,ib,3,3,{pos},model,0};
  auto pass=FillRectPass();
  pass.exact_host_input_layout={{0,0,3,VT_FLOAT32,False}};
  pass.exact_render_state.DSVFormat=TEX_FORMAT_D24_UNORM_S8_UINT;
  const auto base_state=gpu::DeriveOriginalXdkGraphicsPipelineState(
      FixtureFixedState(screen.guest_device),pass.exact_render_state);
  if(!base_state)throw std::runtime_error("Fixture base XDK state was rejected");
  pass.exact_render_state=*base_state;
  auto overlay=pass;
  overlay.original_depth_unbound=true;
  overlay.exact_render_state.DSVFormat=TEX_FORMAT_UNKNOWN;
  overlay.exact_render_state.DepthStencilDesc.DepthWriteEnable=False;
  auto shared=BufferOf(device,BIND_UNIFORM_BUFFER,std::array<uint32_t,8>{});
  auto constants=[&](IShaderResourceBinding* binding,const auto&...) {
    for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
      binding->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
    return true;
  };
  unsigned composites=0;
  std::vector<std::byte> pixels;
  auto output=[&](ITextureView* image,eor::native::Rect,eor::native::Size) {
    pixels=ReadPixels(device,context,image->GetTexture()); ++composites; return true;
  };
  gpu::DiligentEorDirectBackend backend(device,context,scene,meshes,shaders,bank,effects,frame,format,
      {{model,0,pass,{}},{model,1,overlay,{}},{model,0,overlay,{}}},{buffers},constants,output);

  xdk::FrameCapture capture;
  using Kind=xdk::CommandKind;
  xdk::RawCommand clear{Kind::ClearF,0x82888c40,0,{screen.guest_device,1}};
  clear.captured_clear=xdk::OriginalXdkClearF{};
  clear.captured_clear->flags=1;
  clear.captured_clear->has_color=true;
  clear.captured_clear->color={0,0,1,1};
  capture.RecordCommand(clear);
  std::array<eor::aot::BoundIndexedDraw,2> draws{};
  for(uint32_t i=0;i<draws.size();++i) {
    if(i)capture.RecordCommand({Kind::SetDepthStencilSurface,0x8287f628,0,{screen.guest_device,0}});
    xdk::RawDraw call;
    call.kind=xdk::Kind::DrawIndexedVertices;
    call.function=0x8288e318; call.lr=0x8265b53c; call.gpr={screen.guest_device,4,0,0,3};
    xdk::OriginalVsMatrices live;
    live.original_guest_device=screen.guest_device;
    live.original_rt0_guest_object=screen.guest_rt0;
    live.original_depth_guest_object=i ? 0 : screen.guest_depth;
    live.original_vertex_declaration_guest_object=5;
    live.original_vs_guest_object=6; live.original_ps_guest_object=7;
    const int left=int(i)*int(width/2);
    live.raster=xdk::OriginalRasterWindow{0,0,width,height,0,1,true,
        {left,0,left+int(width/2),int(height)}};
    auto regs=std::make_shared<xdk::OriginalShaderRegisters>();
    regs->complete_float_banks=true;
    regs->fixed_graphics_state=FixtureFixedState(screen.guest_device);
    regs->pixel[21*4+i]=regs->pixel[21*4+3]=1;
    call.observed_vs_matrices=live; call.observed_shader_constants=regs;
    auto& item=draws[i];
    item.original_model_hash=model; item.original_mesh_header_offset=0; item.original_surface_ordinal=i;
    item.effect_hash=pass.original_effect_hash; item.pass_index=pass.original_pass_index;
    item.vertex_source_sha256=pass.original_vertex_shader_sha256;
    item.pixel_source_sha256=pass.original_pixel_shader_sha256;
    item.draw={pos,ib,6,7,3,0,0,capture.NextFrameSequence(),capture.NextDrawOrdinal(),live,regs};
    capture.Record(call);
  }
  capture.RecordCommand({Kind::Resolve,0x828897f8,0,{screen.guest_device,0,0,screen.guest_front_buffer}});
  capture.RecordCommand({Kind::Present,0x82880778,0,{screen.guest_device,screen.guest_front_buffer}});
  const auto timeline=capture.Finish();
  auto wrong=draws;
  wrong.back().original_surface_ordinal=2;
  if(backend.PreflightOriginalScene(wrong))
    throw std::runtime_error("Material replay accepted an unregistered surface");
  eor::aot::ReplayAotScene(frame,draws,bank,effects,reflection,backend,&timeline);
  if(composites!=1 || pixels.size()!=width*height*4)
    throw std::runtime_error("Expected one complete offscreen composite");
  for(size_t i=0;i<pixels.size();i+=4) {
    const bool left=(i/4)%width<width/2;
    const std::array expected{std::byte(left?255:0),std::byte(left?0:255),std::byte{0},std::byte{255}};
    if(!std::equal(expected.begin(),expected.end(),pixels.begin()+i))
      throw std::runtime_error("Two-surface replay changed the expected pixel colors");
  }
  const auto separate_surfaces=pixels;
  draws[1].original_surface_ordinal=0;
  auto wrong_depth=draws;
  wrong_depth[1].draw.guest_vs_matrices->original_depth_guest_object=screen.guest_depth;
  bool rejected=false, scene_started=false;
  try {
    eor::aot::ReplayAotScene(frame,wrong_depth,bank,effects,reflection,backend,&timeline,&scene_started);
  } catch(const std::invalid_argument&) { rejected=true; }
  if(!rejected || scene_started || composites!=1)
    throw std::runtime_error("Accepted a registered depth variant from a different XDK draw");
  eor::aot::ReplayAotScene(frame,draws,bank,effects,reflection,backend,&timeline);
  if(composites!=2 || pixels!=separate_surfaces)
    throw std::runtime_error("Depth switch changed output when both draws shared one surface and effect");
  // The same model/surface/effect and attachment now uses a different
  // live color-write mask. Only green is writable on the second Draw,
  // leaving the blue clear channel intact; a stale PSO would write green.
  auto changed=draws;
  auto changed_regs=std::make_shared<xdk::OriginalShaderRegisters>(
      *changed[1].draw.guest_shader_constants);
  changed_regs->fixed_graphics_state=FixtureFixedState(screen.guest_device,2);
  changed[1].draw.guest_shader_constants=changed_regs;
  auto changed_timeline=timeline;
  changed_timeline.calls[1].observed_shader_constants=changed_regs;
  auto invalid=changed;
  auto invalid_regs=std::make_shared<xdk::OriginalShaderRegisters>(*changed_regs);
  invalid_regs->fixed_graphics_state=FixtureFixedState(screen.guest_device,2,1);
  invalid[1].draw.guest_shader_constants=invalid_regs;
  if(backend.PreflightOriginalScene(invalid))
    throw std::runtime_error("PSO variant changed the scene MSAA contract");
  eor::aot::ReplayAotScene(frame,changed,bank,effects,reflection,backend,&changed_timeline);
  if(composites!=3 || pixels.size()!=width*height*4)
    throw std::runtime_error("Live PSO variant failed to composite");
  for(size_t i=0;i<pixels.size();i+=4) {
    const bool left=(i/4)%width<width/2;
    const std::array expected{std::byte(left?255:0),std::byte(left?0:255),
                              std::byte(left?0:255),std::byte{255}};
    if(!std::equal(expected.begin(),expected.end(),pixels.begin()+i))
      throw std::runtime_error("Live PSO variant failed to preserve masked blue channel");
  }
  // Switching back must not reuse the last variant's pipeline/SRB.
  eor::aot::ReplayAotScene(frame,draws,bank,effects,reflection,backend,&timeline);
  if(composites!=4 || pixels!=separate_surfaces)
    throw std::runtime_error("Base PSO was contaminated by the live state variant");
  std::cout<<"Depth-bound and color-only draws shared the same material, all 64000 pixels matched\n";
  std::cout<<"Direct EoR backend replayed two fixture surfaces with original FillRect shaders, "
             "64000 pixels matched in one offscreen composite\n";
}

// Shader objects and the command timeline below belong to the test. The glyph
// stream, shader containers, atlas and texture fetch are supplied privately.
static void VerifyStagedGlyphReplay(IRenderDevice* device,IDeviceContext* context,
    eor::gpu::DiligentAotShaderModules& modules,const eor::aot::ShaderBank& bank,
    const eor::aot::EffectCatalog& effects,eor::gpu::VerifiedOriginalGraphicsPass pass,
    eor::gpu::DiligentOriginalTextureResidency& residency,IBuffer* shared,
    const std::vector<std::byte>& glyphs,const std::array<uint32_t,6>& fetch,
    eor::native::Size size,const std::vector<std::byte>& expected_bgra,
    const char* reflection_path,const char* virtual_path,const char* source_dir) {
  namespace xdk=eor::xdk;
  namespace gpu=eor::gpu;
  namespace aot=eor::aot;
  aot::ReflectionBank reflection;
  reflection.Load(reflection_path);
  aot::OriginalVirtualShaderIdentity identities;
  identities.Load(virtual_path);
  const xdk::FrameCapture first_frame;
  const auto sequence=first_frame.NextFrameSequence();
  const auto ordinal=first_frame.NextDrawOrdinal();
  std::optional<aot::OriginalPixelSamplerControls> pixel_sampling;
  auto shader_object=[&](const aot::Digest& digest,aot::Stage stage,uint32_t object) {
    std::string name;
    constexpr char hex[]="0123456789abcdef";
    for(auto b:digest) { name+=hex[b>>4];name+=hex[b&15]; }
    std::ifstream file(std::filesystem::path(source_dir)/(name+".xenos.bin"),std::ios::binary);
    std::vector<uint8_t> container{std::istreambuf_iterator<char>(file),{}};
    if(aot::SourceSha256(container)!=digest)throw std::runtime_error("Font shader container SHA mismatch");
    if(stage==aot::Stage::Pixel)
      pixel_sampling=aot::ReadOriginalPixelSamplerControls(container,digest);
    const size_t header=object+(stage==aot::Stage::Vertex ? 872u : 40u);
    std::vector<uint8_t> memory(header+container.size());
    std::copy(container.begin(),container.end(),memory.begin()+header);
    auto read=[](void* state,uint32_t address,std::byte* bytes,size_t count) {
      const auto& memory=*static_cast<const std::vector<uint8_t>*>(state);
      if(address>memory.size() || count>memory.size()-address)return false;
      std::memcpy(bytes,memory.data()+address,count);
      return true;
    };
    return aot::ObserveOriginalBoundXdkShader(identities,object,stage,read,&memory,sequence,ordinal);
  };
  gpu::VerifiedSceneFormat format{gpu::NativeColorFormat::Rgba8,gpu::NativeDepthFormat::D24S8,
      true,true,{1,2,3,4,true,true,false}};
  const auto& screen=format.original_screen;
  eor::native::FrameGeometry frame{size,size,{0,0,size.width,size.height},1,false,true,{1280,720}};
  pass.original_view_space=gpu::VerifiedOriginalViewSpace::OriginalAspectHud;
  pass.original_view_space_verified=pass.original_hud_clip_coordinates_verified=true;
  pass.exact_render_state.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
  pass.exact_render_state.DSVFormat=TEX_FORMAT_D24_UNORM_S8_UINT;
  const auto fixed=FixtureFixedState(screen.guest_device);
  const auto state=gpu::DeriveOriginalXdkGraphicsPipelineState(fixed,pass.exact_render_state);
  if(!state)throw std::runtime_error("Font fixture XDK state is unsupported");
  pass.exact_render_state=*state;
  constexpr uint32_t fixture_owner=1,atlas_hash=0xdad1dd22;
  gpu::VerifiedEorSampler sampler{fixture_owner,0,pass.original_effect_hash,pass.original_pass_index,
      atlas_hash,0,gpu::OriginalSamplerStage::Pixel,"HulkTexture2D_s0",true,true,true};
  gpu::VerifiedNativeMaterialPass material{fixture_owner,0,pass,
      {{sampler,gpu::OriginalTextureColorSpace::Linear,gpu::OriginalTextureViewKind::Native2D}}};
  gpu::DiligentMeshUploadBackend meshes(device);
  gpu::DiligentSceneTargets scene(device,context);
  unsigned composites=0;
  auto output=[&](ITextureView* image,eor::native::Rect,eor::native::Size) {
    auto rgba=ReadPixels(device,context,image->GetTexture());
    for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
    if(rgba!=expected_bgra)throw std::runtime_error("Staged Font_Main readback differs from reference");
    ++composites;return true;
  };
  auto constants=[&](IShaderResourceBinding* binding,const auto&...) {
    for(auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
      binding->GetVariableByName(stage,"HulkSharedConstants")->Set(shared);
    return true;
  };
  gpu::DiligentEorDirectBackend backend(device,context,scene,meshes,modules,bank,effects,frame,format,
      gpu::AcquireVerifiedNativeMaterialPasses({&material,1},residency),{},constants,output);
  xdk::RawDraw call;
  call.kind=xdk::Kind::EndVertices;call.function=0x8288d8f8;call.lr=0x82676344;call.gpr[0]=screen.guest_device;
  xdk::OriginalVsMatrices live;
  live.original_guest_device=screen.guest_device;live.original_rt0_guest_object=screen.guest_rt0;
  live.original_depth_guest_object=screen.guest_depth;live.original_vertex_declaration_guest_object=8;
  live.original_vs_guest_object=0x100;live.original_ps_guest_object=0x200;
  live.original_texture_guest_objects[0]=0x300;
  live.raster=xdk::OriginalRasterWindow{0,0,1280,720,0,1};
  call.observed_vs_matrices=live;
  call.original_font_fetch=fetch;call.original_pixel_texture_keys[0]=atlas_hash;
  call.observed_live_vertex_shader_header=shader_object(pass.original_vertex_shader_sha256,aot::Stage::Vertex,live.original_vs_guest_object);
  call.observed_live_pixel_shader_header=shader_object(pass.original_pixel_shader_sha256,aot::Stage::Pixel,live.original_ps_guest_object);
  if(!call.observed_live_vertex_shader_header || !call.observed_live_pixel_shader_header)
    throw std::runtime_error("Could not identify Font_Main shader headers");
  call.observed_live_eor_effect_pass=aot::ResolveOriginalLiveEorEffectPass(identities,effects,
      *call.observed_live_vertex_shader_header,*call.observed_live_pixel_shader_header,
      live.original_vs_guest_object,live.original_ps_guest_object,sequence,ordinal);
  xdk::OriginalEorShaderMethodAtDraw parent;
  parent.original_shader_method_guest=0x8266a0c0;parent.original_shader_vtable_guest=0x8206ed6c;
  parent.original_shader_instance_guest=0x400;parent.nesting_depth=1;
  parent.original_pass_material_context_guest=0x600;parent.original_pass_state_one_based=1;
  parent.original_pass_vertex_handle_entry_guest=0x640;parent.original_pass_pixel_handle_entry_guest=0x644;
  parent.original_shader_method_vtable_verified=parent.original_pass_vs_ps_xdk_binding_verified=true;
  parent.original_pass_sampler_slots_verified=parent.original_pass_live_texture_objects_present=true;
  parent.original_pass_sampler_slot_mask=1;parent.original_pass_vs_xdk_guest_object=live.original_vs_guest_object;
  parent.original_pass_ps_xdk_guest_object=live.original_ps_guest_object;
  call.original_engine_shader_method=parent;
  auto regs=std::make_shared<xdk::OriginalShaderRegisters>();
  regs->complete_float_banks=true;regs->fixed_graphics_state=fixed;call.observed_shader_constants=regs;
  auto vb=std::make_shared<xdk::OriginalGuestBufferBytes>();
  vb->guest_address=vb->guest_object=0x500;vb->guest_size=static_cast<uint32_t>(glyphs.size());vb->bytes=glyphs;
  call.inline_glyphs=xdk::OriginalInlineGlyphBatch{live.original_vertex_declaration_guest_object,vb->guest_size/24,vb};
  const auto layout=eor::ui::ResolveUsaInlineFontLayout(call);
  if(!layout)throw std::runtime_error("Original Font_Main declaration unresolved");
  const auto staged=backend.StageVerifiedOriginalFontDraw(call,*layout,sequence,ordinal,fixture_owner,0,identities);
  using Kind=xdk::CommandKind;
  xdk::RawCommand clear{Kind::ClearF,0x82888c40,0,{screen.guest_device,1}};
  clear.captured_clear=xdk::OriginalXdkClearF{};
  clear.captured_clear->flags=1;clear.captured_clear->has_color=true;clear.captured_clear->color={1,0,1,1};
  auto replay=[&](const xdk::RawDraw& source,const aot::BoundIndexedDraw& draw,gpu::DiligentEorDirectBackend& gpu_backend) {
    xdk::FrameCapture commands;
    commands.RecordCommand(clear);commands.Record(source);
    commands.RecordCommand({Kind::Resolve,0x828897f8,0,{screen.guest_device,0,0,screen.guest_front_buffer}});
    commands.RecordCommand({Kind::Present,0x82880778,0,{screen.guest_device,screen.guest_front_buffer}});
    const auto timeline=commands.Finish();
    aot::ReplayAotScene(frame,{&draw,1},bank,effects,reflection,gpu_backend,&timeline);
  };
  replay(call,staged,backend);
  if(composites!=1)throw std::runtime_error("Staged glyph replay did not complete exactly once");
  backend.RetireCompletedOriginalFontDraws(sequence); // ReadPixels waited for completion.
  // Reuse the captured attributes as a controlled indexed mesh. The world
  // uploader packs positions as float3, UV and color as separate float4 arrays.
  // The original shader fetches only two components from the UV stream.
  const auto decoded=eor::ui::DecodeUsaInlineGlyphPayload(glyphs);
  std::vector<eor::model::NativePosition> xyz;
  for(const auto& v:decoded.positions)xyz.push_back({v[0],v[1],v[2]});
  const auto pos=meshes.UploadPositions(xyz);
  const auto uv=meshes.UploadDecodedAttributes(decoded.texcoords);
  const auto color=meshes.UploadDecodedAttributes(decoded.colors);
  const auto ib=meshes.UploadIndices(decoded.triangle_indices);
  gpu::NativeEorDrawBuffers buffers{pos,ib,static_cast<uint32_t>(xyz.size()),
      static_cast<uint32_t>(decoded.triangle_indices.size()),{pos,uv,color},fixture_owner,0};
  material.original_pipeline.original_dynamic_font_vertex_layout_verified=false;
  gpu::DiligentEorDirectBackend world(device,context,scene,meshes,modules,bank,effects,frame,format,
      gpu::AcquireVerifiedNativeMaterialPasses({&material,1},residency),{buffers},constants,output);
  auto indexed=call;
  indexed.kind=xdk::Kind::DrawIndexedVertices;indexed.function=0x8288e318;indexed.lr=0x8265b53c;
  indexed.gpr={screen.guest_device,4,0,0,buffers.index_count};
  indexed.inline_glyphs.reset();indexed.original_engine_shader_method.reset();
  auto world_regs=std::make_shared<xdk::OriginalShaderRegisters>(*regs);
  world_regs->pixel_fetch[0]=fetch;world_regs->valid_pixel_fetch=1;
  indexed.observed_shader_constants=world_regs;
  auto draw=staged;
  draw.draw.vertex_buffer=pos;draw.draw.index_buffer=ib;draw.draw.guest_shader_constants=world_regs;
  draw.original_mesh_header_offset=0;draw.original_dynamic_font_draw=false;
  draw.source_nonindexed_linearization_verified=false;draw.original_pixel_sampling=pixel_sampling;
  replay(indexed,draw,world);
  if(composites!=2)throw std::runtime_error("Indexed mesh did not reproduce the captured glyph image");
  std::cout<<"Native indexed XYZ/UV/color mesh matched the captured glyph reference\n";
  std::cout<<"Production Font_Main replay matched "<<size.width*size.height<<" pixels in one offscreen composite\n";
}

// This fixture exercises the original font shaders on a controlled quad.
// Game-generated glyph positions, UI events and presentation are separate.
static void VerifyFontDraw(IRenderDevice* device, IDeviceContext* context,
                           const eor::textures::OriginalXboxTextureAssets& assets,
                           eor::gpu::DiligentOriginalTextureResidency& residency,
                           const char* bank_path,const char* effects_path,const char* captured_path,
                           const char* fetch_path,char** replay_inputs) {
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
  std::vector<std::byte> raw;
  if(captured_path) {
    std::ifstream f(captured_path,std::ios::binary|std::ios::ate);
    const auto size=f.tellg();
    if(size<=0 || size>65535*24) throw std::runtime_error("Invalid source glyph payload size");
    raw.resize(static_cast<size_t>(size));
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
      if(replay_inputs)
        VerifyStagedGlyphReplay(device,context,shaders,bank,effects,pass,residency,shared,raw,words,
            {desc.Width,desc.Height},observed,replay_inputs[0],replay_inputs[1],replay_inputs[2]);
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

// Actual private original shader bank, not synthetic shader replacements.
// This tests driver PSO admission only; draw semantics and pixel readbacks
// are exercised separately. Never represent PSO creation as a game frame.
static void VerifyFullOriginalShaderBank(IRenderDevice* device,const char* bank_path,const char* effects_path) {
  namespace aot=eor::aot;
  namespace gpu=eor::gpu;
  aot::ShaderBank bank;bank.Load(bank_path);
  aot::EffectCatalog effects;effects.Load(effects_path);
  if(!bank.HasFullArchiveCoverage() || bank.size()!=5194 || effects.PairCount()!=2597)
    throw std::runtime_error("Original USA shader bank coverage incomplete");
  size_t pipelines=0,cube_variants=0,clip_programs=0;
  effects.ForEachPair([&](uint32_t effect,uint32_t index,const aot::Digest& vs,const aot::Digest& ps) {
    const auto* vertex=bank.Find(vs,aot::Stage::Vertex);
    const auto* pixel=bank.Find(ps,aot::Stage::Pixel);
    if(!vertex || !pixel)throw std::runtime_error("Missing original pair");
    const auto interface=gpu::live::InspectScalarInterface(*vertex);
    if(!interface.supported || interface.inputs.empty())
      throw std::runtime_error("Unsupported original VS vertex ABI in test");
    gpu::VerifiedOriginalGraphicsPass pass{};
    pass.original_effect_hash=effect;pass.original_pass_index=index;
    pass.original_vertex_shader_sha256=vs;pass.original_pixel_shader_sha256=ps;
    pass.material_pass_verified=pass.original_render_state_verified=pass.original_host_vertex_layout_verified=true;
    pass.exact_render_state.NumRenderTargets=1;
    pass.exact_render_state.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
    pass.exact_render_state.DSVFormat=TEX_FORMAT_D24_UNORM_S8_UINT;
    pass.exact_render_state.DepthStencilDesc.DepthEnable=False;
    pass.exact_render_state.DepthStencilDesc.DepthWriteEnable=False;
    pass.exact_render_state.RasterizerDesc.CullMode=CULL_MODE_NONE;
    pass.exact_render_state.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    for(const auto& input:interface.inputs) {
      LayoutElement e{input.location,static_cast<uint32_t>(pass.exact_host_input_layout.size()),
          input.components,input.uint_bits?VT_UINT32:VT_FLOAT32,False};
      e.RelativeOffset=0;e.Stride=input.components*4u;
      pass.exact_host_input_layout.push_back(e);
    }
    uint16_t cube_mask=0;
    for(const auto& image:aot::InspectSpirvImageResources(pixel->spirv,aot::Stage::Pixel))
      if(const auto slot=aot::DecodeFixedAotImageSlot(image.exact_variable_name);slot && slot->dimension==3u)
        cube_mask|=static_cast<uint16_t>(1u<<slot->slot);
    // Keep memory bounded: destroy this fixture's native PSOs and modules
    // after each pair. The runtime cache remains untouched.
    gpu::DiligentAotShaderModules modules(device);
    auto pipeline=gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,pass);
    if(!pipeline.pipeline)throw std::runtime_error("Original Vulkan pair rejected");
    ++pipelines;
    // Test every nonempty subset, including a mixture of linear/gamma slots.
    for(uint16_t mask=cube_mask;mask;mask=static_cast<uint16_t>((mask-1)&cube_mask)) {
      pass.original_xbox_cube_gamma_mask=mask;
      auto variant=gpu::CreateOriginalVulkanPipeline(device,modules,bank,effects,pass);
      if(!variant.pipeline)throw std::runtime_error("Original cube gamma variant rejected");
      ++cube_variants;
    }
    clip_programs+=aot::ScalarPreservesClipXYW(vertex->spirv)?1:0;
  });
  if(pipelines!=2597 || cube_variants<248 || clip_programs!=37)
    throw std::runtime_error("Original shader bank regression coverage mismatch");
  std::cout<<"Original Vulkan pairs: "<<pipelines<<"; cube gamma variants: "<<cube_variants
           <<"; proven clip-space programs: "<<clip_programs<<"\n";
}

int main(int argc,char** argv) {
  try {
    if (argc!=2 && argc!=3 && argc!=4 && argc!=5 && argc!=6 && argc!=7 && argc!=10) throw std::invalid_argument("Expected USA INDEX.IND and TEXTURES.ARC paths");
    auto* factory=LoadAndGetEngineFactoryVk();
    if (!factory) throw std::runtime_error("Diligent Vulkan factory unavailable");
    EngineVkCreateInfo ci{};
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    factory->CreateDeviceAndContextsVk(ci,&device,&context);
    if (!device || !context) throw std::runtime_error("Vulkan device unavailable");
    std::cout<<device->GetAdapterInfo().Description<<'\n';
    if(argc==2 && std::strcmp(argv[1],"--mixed-targets")==0) {
      VerifySequentialTargets(device,context);
      return 0;
    }
    if(argc==4 && std::strcmp(argv[1],"--shader-bank")==0) {
      VerifyFullOriginalShaderBank(device,argv[2],argv[3]);
      return 0;
    }
    if(argc==4)throw std::invalid_argument("Unrecognized original shader-bank test mode");
    if(argc==5 && std::strcmp(argv[1],"--material-replay")==0) {
      VerifySequentialTargets(device,context);
      VerifyMaterialReplay(device,context,argv[2],argv[3],argv[4]);
      return 0;
    }
    const auto assets=eor::textures::OriginalXboxTextureAssets::OpenPinnedUsa(argv[1],argv[2]);
    eor::gpu::DiligentOriginalTextureResidency residency(device,assets);
    VerifyAtlas(device,context,assets,residency,0x995ad5dc);
    VerifyAtlas(device,context,assets,residency,0xdad1dd22);
    if(argc>=5) VerifyFontDraw(device,context,assets,residency,argv[3],argv[4],argc>=6 ? argv[5] : nullptr,argc>=7 ? argv[6] : nullptr,argc==10 ? argv+7 : nullptr);
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
