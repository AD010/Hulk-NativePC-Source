#include "../native_port/src/eor_diligent_scene_targets.h"
#undef NDEBUG
#include <cassert>
#include <memory>
using namespace hulk::eor;
using namespace hulk::eor::xdk;
namespace {
OriginalVsMatrices State(uint32_t target,uint32_t width,uint32_t height) {
  OriginalVsMatrices s{};
  s.original_guest_device=1;s.original_rt0_guest_object=target;
  s.original_color_extent=OriginalSurfaceExtent{width,height};
  s.raster=OriginalRasterWindow{0,0,float(width),float(height),0,1,false,{}};
  return s;
}
OriginalXdkFixedGraphicsState Fixed(uint32_t width) {
  OriginalXdkFixedGraphicsState s{};
  s.original_guest_device=1;
  s.original_register_values[0]=width; // 0x2000: pitch, single sample
  s.original_register_values[16+4]=15; // 0x2104: RT0 color mask
  return s;
}
RawCommand Clear(uint32_t target,uint32_t width,uint32_t height) {
  RawCommand c{CommandKind::ClearF,0x82888C40u,0x8263A008u,{1,1,0,0x1000}};
  c.captured_clear=OriginalXdkClearF{};
  c.captured_clear->flags=1;c.captured_clear->has_color=true;
  c.captured_clear_targets=std::make_shared<const OriginalVsMatrices>(State(target,width,height));
  c.captured_clear_fixed=std::make_shared<const OriginalXdkFixedGraphicsState>(Fixed(width));
  return c;
}
Frame Source() {
  FrameCapture f;
  f.RecordCommand(RawCommand{CommandKind::SetRenderTarget,0x8287F2C0u,0x8263A000u,{1,0,2}});
  f.RecordCommand(Clear(2,320,180));
  f.RecordCommand(RawCommand{CommandKind::Resolve,0x828897F8u,0x8263A014u,{1,0,0,3}});
  f.RecordCommand(RawCommand{CommandKind::SetRenderTarget,0x8287F2C0u,0x8263A000u,{1,0,4}});
  f.RecordCommand(Clear(4,1280,720));
  RawDraw draw{Kind::DrawIndexedVertices,0x8288E318u,0x826676A8u,{1,4,0,0,3}};
  draw.observed_vs_matrices=State(4,1280,720);
  auto constants=std::make_shared<OriginalShaderRegisters>();
  constants->fixed_graphics_state=Fixed(1280);
  draw.observed_shader_constants=constants;
  f.Record(draw);
  f.RecordCommand(RawCommand{CommandKind::Resolve,0x828897F8u,0x8263A014u,{1,0,0,5}});
  f.RecordCommand(RawCommand{CommandKind::Present,0x82880778u,0x8263A018u,{1,5}});
  return f.Finish();
}
}
int main() {
  auto frame=Source();
  assert(ValidateOriginalCommandTimeline(frame));
  auto plan=gpu::PlanOriginalNativeTargets(frame,1280,720);
  assert(plan);
  const auto& graph=plan->format.original_graph;
  assert(graph.first_color==2 && graph.final_color==4 && graph.front_buffer==5);
  assert(graph.HasColor(2) && graph.HasColor(4) && graph.HasResolve(3));
  assert(graph.extents.at(3).width==320 && graph.extents.at(3).height==180);
  native::FrameSetup setup{};setup.backbuffer={2560,1600};setup.original_guest={1280,720};
  setup.engine_draw_hook_verified=true;setup.offline_shader_cache_verified=true;
  setup.projection_hook_verified=true;setup.samples=plan->samples;
  assert(gpu::OriginalTargetGraphFitsBudget(graph,native::PlanFrame(setup)));
  auto rotated=frame;
  rotated.commands[rotated.commands.size()-2].gpr[3]=7;
  rotated.commands.back().gpr[1]=7;
  const auto next=gpu::PlanOriginalNativeTargets(rotated,1280,720);
  assert(next && next->format.original_graph.front_buffer==7);
  assert(gpu::SameOriginalTargetStorage(graph,next->format.original_graph));
  assert(graph!=next->format.original_graph);
  auto incompatible=next->format.original_graph;
  incompatible.extents.at(2).width=640;
  assert(!gpu::SameOriginalTargetStorage(graph,incompatible));
  incompatible=next->format.original_graph;
  incompatible.front_buffer=2;
  assert(!gpu::SameOriginalTargetStorage(graph,incompatible));
  incompatible=next->format.original_graph;
  incompatible.sample_counts.at(7)=4;
  assert(!gpu::SameOriginalTargetStorage(graph,incompatible));
  // A color-only intermediate Resolve may occur while a depth target
  // belonging to the NEXT larger pass is already selected.
  auto mixed=frame;
  auto state=*mixed.commands[4].captured_clear_targets;
  state.original_depth_guest_object=8;
  state.original_depth_extent=OriginalSurfaceExtent{1280,720};
  mixed.commands[4].captured_clear_targets=std::make_shared<const OriginalVsMatrices>(state);
  mixed.calls[0].observed_vs_matrices=state;
  mixed.commands.insert(mixed.commands.begin()+2,
      RawCommand{CommandKind::SetDepthStencilSurface,0x8287F628u,0x8263A010u,{1,8}});
  auto mixed_plan=gpu::PlanOriginalNativeTargets(mixed,1280,720);
  assert(mixed_plan && mixed_plan->format.original_graph.HasDepth(8));
  // An independently rebound depth target is unused by a color-only
  // clear. The later matching main pass proves its true size/sample count.
  auto color_only=mixed;
  auto color_clear=Clear(2,320,180);
  auto color_state=*color_clear.captured_clear_targets;
  color_state.original_depth_guest_object=8;
  color_state.original_depth_extent=OriginalSurfaceExtent{1280,720};
  color_clear.captured_clear_targets=std::make_shared<const OriginalVsMatrices>(color_state);
  color_only.commands.insert(color_only.commands.begin()+3,color_clear);
  const auto color_plan=gpu::PlanOriginalNativeTargets(color_only,1280,720);
  assert(color_plan && color_plan->format.original_graph.HasDepth(8));
  assert(color_plan->format.original_graph.extents.at(8).width==1280);
  // The very first source pass can already retain the larger depth
  // attachment. BeginScene must defer the incompatible Vulkan pair,
  // rather than binding it before replay reaches the color-only clear.
  auto initial_mismatch=mixed;
  auto initial=*initial_mismatch.commands[1].captured_clear_targets;
  initial.original_depth_guest_object=8;
  initial.original_depth_extent=OriginalSurfaceExtent{1280,720};
  initial_mismatch.commands[1].captured_clear_targets=
      std::make_shared<const OriginalVsMatrices>(initial);
  // Move the genuine depth selection ahead of the first clear.
  std::swap(initial_mismatch.commands[1],initial_mismatch.commands[2]);
  const auto initial_plan=gpu::PlanOriginalNativeTargets(initial_mismatch,1280,720);
  assert(initial_plan);
  const auto& initial_graph=initial_plan->format.original_graph;
  assert(initial_graph.first_color==2 && initial_graph.first_depth==8);
  assert(initial_graph.extents.at(2).width==320 &&
         initial_graph.extents.at(8).width==1280);
  // The same mismatched pair is still NOT legal for a combined clear.
  auto bad_clear=mixed;
  auto bad=Clear(2,320,180);
  bad.captured_clear->flags=0x11;bad.gpr[1]=0x11;
  auto bad_state=*bad.captured_clear_targets;
  bad_state.original_depth_guest_object=8;
  bad_state.original_depth_extent=OriginalSurfaceExtent{1280,720};
  bad.captured_clear_targets=std::make_shared<const OriginalVsMatrices>(bad_state);
  bad_clear.commands.insert(bad_clear.commands.begin()+3,bad);
  assert(!gpu::PlanOriginalNativeTargets(bad_clear,1280,720));
  auto missing=frame;missing.commands[1].captured_clear_targets.reset();
  assert(!gpu::PlanOriginalNativeTargets(missing,1280,720));
  auto wrong=frame;auto wrong_state=*wrong.commands[1].captured_clear_targets;
  wrong_state.original_rt0_guest_object=6;
  wrong.commands[1].captured_clear_targets=std::make_shared<const OriginalVsMatrices>(wrong_state);
  assert(!gpu::PlanOriginalNativeTargets(wrong,1280,720));
  auto other_device=frame;auto wrong_fixed=*other_device.commands[1].captured_clear_fixed;
  wrong_fixed.original_guest_device=99;
  other_device.commands[1].captured_clear_fixed=std::make_shared<const OriginalXdkFixedGraphicsState>(wrong_fixed);
  assert(!gpu::PlanOriginalNativeTargets(other_device,1280,720));
  auto uninitialized=frame;uninitialized.commands.erase(uninitialized.commands.begin()+1);
  assert(!gpu::PlanOriginalNativeTargets(uninitialized,1280,720));
  auto partial=frame;partial.commands[1].captured_clear->has_rect=true;
  partial.commands[1].captured_clear->rect={0,0,16,16};
  assert(!gpu::PlanOriginalNativeTargets(partial,1280,720));
  auto again=partial.commands[1];
  again.gpr[2]=0x1000;
  auto updated=frame;
  updated.commands.insert(updated.commands.begin()+2,again);
  assert(gpu::PlanOriginalNativeTargets(updated,1280,720));
  updated.commands[2].captured_clear->rect[2]=321;
  assert(!gpu::PlanOriginalNativeTargets(updated,1280,720));
  // Initializing D24S8 depth does not initialize stencil, or vice versa.
  for(const uint32_t plane:{0x10u,0x20u}) {
    auto aspects=mixed;
    auto full=aspects.commands[5];
    full.captured_clear->flags=full.gpr[1]=1u|plane;
    aspects.commands[5]=full;
    auto bounded=full;
    bounded.captured_clear->flags=bounded.gpr[1]=plane;
    bounded.gpr[2]=0x1000;
    bounded.captured_clear->has_rect=true;
    bounded.captured_clear->rect={12,5,36,27};
    aspects.commands.insert(aspects.commands.begin()+6,bounded);
    assert(gpu::PlanOriginalNativeTargets(aspects,1280,720));
    aspects.commands[6].captured_clear->flags=aspects.commands[6].gpr[1]=plane^0x30u;
    assert(!gpu::PlanOriginalNativeTargets(aspects,1280,720));
  }
}
