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
  c.captured_clear_targets=State(target,width,height);
  c.captured_clear_fixed=Fixed(width);
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
  auto missing=frame;missing.commands[1].captured_clear_targets.reset();
  assert(!gpu::PlanOriginalNativeTargets(missing,1280,720));
  auto wrong=frame;wrong.commands[1].captured_clear_targets->original_rt0_guest_object=6;
  assert(!gpu::PlanOriginalNativeTargets(wrong,1280,720));
  auto other_device=frame;other_device.commands[1].captured_clear_fixed->original_guest_device=99;
  assert(!gpu::PlanOriginalNativeTargets(other_device,1280,720));
  auto uninitialized=frame;uninitialized.commands.erase(uninitialized.commands.begin()+1);
  assert(!gpu::PlanOriginalNativeTargets(uninitialized,1280,720));
  auto partial=frame;partial.commands[1].captured_clear->has_rect=true;
  partial.commands[1].captured_clear->rect={0,0,16,16};
  assert(!gpu::PlanOriginalNativeTargets(partial,1280,720));
}
