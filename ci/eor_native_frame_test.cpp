#include "../native_port/src/eor_native_frame.h"
#include "../native_port/src/eor_diligent_scene_targets.h"
#include "../native_port/src/native_camera_projection.h"
#include "../native_port/src/native_resolution.h"
#include <array>
#include <cmath>
#include <cassert>
#include <stdexcept>
#include <vector>
#include <string>
using namespace hulk::eor::native;
struct FakeBackend final : Backend {
  std::vector<std::string> order;
  Size scene{}, resolved{};
  uint32_t samples = 0;
  Rect rectangle{};
  unsigned aborts = 0, draw_number = 0, fail_draw_at = 0;
  bool fail_begin = false, fail_resolve = false, fail_composite = false;
  bool BeginScene(Size s, uint32_t n) override {
    order.push_back("BeginScene"); scene = s; samples = n; return !fail_begin;
  }
  void AbortScene() noexcept override { ++aborts; }
  bool DrawIndexed(const IndexedDraw&) override {
    order.push_back("DrawIndexed"); return ++draw_number != fail_draw_at;
  }
  bool ResolveScene(Size s) override {
    order.push_back("ResolveScene"); resolved = s; return !fail_resolve;
  }
  bool Composite(Rect r, Size) override {
    order.push_back("Composite"); rectangle = r; return !fail_composite;
  }
};
template<class F> void MustThrow(F f) {
  bool found = false;
  try { f(); } catch (const std::exception&) { found = true; }
  assert(found);
}
int main() {
  // Real-source target ownership is checked per Draw, not only at the
  // preceding original SetRenderTarget command. Test IDs are synthetic.
  hulk::eor::gpu::VerifiedOriginalXdkScreen target{};
  target.guest_device=0x1000u; target.guest_rt0=0x2000u;
  target.guest_depth=0x3000u;
  target.exact_guest_to_native_targets_verified=true;
  hulk::eor::xdk::OriginalVsMatrices actual{};
  actual.original_guest_device=0x1000u;
  actual.original_rt0_guest_object=0x2000u;
  actual.original_depth_guest_object=0x3000u;
  actual.original_vertex_declaration_guest_object=0x4000u;
  actual.original_vs_guest_object=0x5000u;
  actual.original_ps_guest_object=0x6000u;
  using hulk::eor::gpu::MatchesVerifiedOriginalXdkMainDrawTargets;
  assert(MatchesVerifiedOriginalXdkMainDrawTargets(actual,target));
  target.exact_guest_to_native_targets_verified=false;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(actual,target));
  target.exact_guest_to_native_targets_verified=true;
  auto alien=actual;
  alien.original_rt0_guest_object=0x2001u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  alien=actual;alien.original_depth_guest_object=0x3001u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  alien=actual;alien.original_guest_device=0x1001u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  alien=actual;alien.original_vs_guest_object=0u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  alien=actual;alien.original_ps_guest_object=0u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  alien=actual;alien.original_vertex_declaration_guest_object=0u;
  assert(!MatchesVerifiedOriginalXdkMainDrawTargets(alien,target));
  // Actual 16:10 camera rewrite is enabled only when source validation
  // proves the original 3D view and its matrix multiplication ABI.
  // Preserve original horizontal FOV: 16:9 -> 16:10 scales clip-Y by 0.9,
  // without stretching HUD, changing guest EDRAM or cropping scene sides.
  std::array<float,16> proj{1,0,0,0, 0,1,0,0,
                            0,0,1,0, 0,0,0,1};
  hulk::camera::SourceCameraProof camera_proof{
      true,true,true,hulk::camera::ClipMatrixConvention::ColumnVector};
  const auto corrected=hulk::camera::ExpandVerifiedVerticalView(
      proj,16.0/9.0,2560,1600,camera_proof);
  assert(corrected && std::abs((*corrected)[5]-0.9f)<0.00001f &&
         (*corrected)[0]==1.0f && (*corrected)[15]==1.0f);
  camera_proof.convention=hulk::camera::ClipMatrixConvention::RowVector;
  const auto row_corrected=hulk::camera::ExpandVerifiedVerticalView(
      proj,16.0/9.0,2560,1600,camera_proof);
  assert(row_corrected && std::abs((*row_corrected)[5]-0.9f)<0.00001f);
  // The SAME matrix ABI must work at arbitrary monitor aspects:
  // Hor+ for ultrawide, Vert+ for 4:3, square and portrait.
  const auto ultra=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,3440,1440,camera_proof);
  const auto doublewide=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,5120,1440,camera_proof);
  const auto oldstyle=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,1024,768,camera_proof);
  const auto square=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,1920,1920,camera_proof);
  const auto portrait=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,1080,1920,camera_proof);
  const auto baseline=hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,1920,1080,camera_proof);
  assert(ultra && std::abs((*ultra)[0]-float((16.0/9.0)/(3440.0/1440.0)))<1e-5f
         && (*ultra)[5]==1.f);
  assert(doublewide && std::abs((*doublewide)[0]-0.5f)<1e-5f &&
         (*doublewide)[5]==1.f);
  assert(oldstyle && (*oldstyle)[0]==1.f &&
         std::abs((*oldstyle)[5]-0.75f)<1e-5f);
  assert(square && (*square)[0]==1.f &&
         std::abs((*square)[5]-0.5625f)<1e-5f);
  assert(portrait && (*portrait)[0]==1.f &&
         std::abs((*portrait)[5]-0.31640625f)<1e-5f);
  assert(baseline && *baseline==proj);
  camera_proof.original_main_perspective_view=false;
  assert(!hulk::camera::AdaptVerifiedMainViewProjection(
      proj,16.0/9.0,2560,1600,camera_proof));
  FrameSetup s{};
  s.backbuffer = {2560, 1600};
  s.samples = 4;
  const auto legacy = PlanFrame(s);
  assert(legacy.world.width == 2560 && legacy.world.height == 1600);
  assert(legacy.composite.left == 0 && legacy.composite.top == 0);
  assert(!legacy.direct_engine_path_ready);
  MustThrow([&] { FakeBackend b; ReplayNativeScene(legacy,
                 std::vector<IndexedDraw>{{1, 2, 3, 4, 6, 0}}, b); });
  // Without the original USA camera ABI a requested 16:10 host cannot
  // be accepted as a native, full-screen gameplay frame. Do NOT present
  // an Xbox-aspect 2560x1440 rectangle with artificial black bars.
  s.engine_draw_hook_verified = true;
  s.offline_shader_cache_verified = true;
  const auto no_camera = PlanFrame(s);
  assert(!no_camera.direct_engine_path_ready &&
         !no_camera.widescreen_camera);
  assert(no_camera.world.width == 2560 &&
         no_camera.world.height == 1600 &&
         no_camera.composite.top == 0);
  assert(HasFullNativeCompositePixels(
      no_camera.world,no_camera.composite,no_camera.backbuffer));
  assert(!HasFullNativeCompositePixels(
      {1280,720},no_camera.composite,no_camera.backbuffer));
  assert(!HasFullNativeCompositePixels(
      {2560,1440},{0,80,2560,1440},no_camera.backbuffer));
  MustThrow([&] { FakeBackend b; ReplayNativeScene(no_camera,
                 std::vector<IndexedDraw>{{1,2,3,4,6,0}}, b); });
  // 16:9 needs no aspect correction: it is still rendered 1:1 at the
  // selected host size, rather than inheriting a 720p EDRAM backbuffer.
  s.backbuffer = {1920,1080};
  const auto native_original_aspect = PlanFrame(s);
  assert(native_original_aspect.direct_engine_path_ready);
  assert(HasFullNativeCompositePixels(
      native_original_aspect.world,native_original_aspect.composite,
      native_original_aspect.backbuffer));
  FakeBackend original_aspect_gpu;
  ReplayNativeScene(native_original_aspect,
                    std::vector<IndexedDraw>{{1,2,3,4,6,0}},
                    original_aspect_gpu);
  assert(original_aspect_gpu.scene.width==1920 &&
         original_aspect_gpu.scene.height==1080 &&
         original_aspect_gpu.rectangle.left==0 &&
         original_aspect_gpu.rectangle.top==0 &&
         original_aspect_gpu.aborts==0);
  s.backbuffer = {2560,1600};
  s.offline_shader_cache_verified = false;
  assert(!PlanFrame(s).direct_engine_path_ready);
  s.offline_shader_cache_verified = true;
  s.engine_draw_hook_verified = false;
  assert(!PlanFrame(s).direct_engine_path_ready);
  s.engine_draw_hook_verified = true;
  s.projection_hook_verified = true;
  s.engine_draw_hook_verified = true;
  s.offline_shader_cache_verified = true;
  const auto native = PlanFrame(s);
  assert(native.world.width == 2560 && native.world.height == 1600);
  assert(native.composite.top == 0 && native.direct_engine_path_ready);
  assert(HasFullNativeCompositePixels(
      native.world,native.composite,native.backbuffer));
  const std::vector<IndexedDraw> real{{1, 2, 3, 4, 6, 0}, {5, 6, 7, 8, 24, 6}};
  auto barred = native;
  barred.world = {2560,1440};
  barred.composite = {0,80,2560,1440};
  FakeBackend barred_backend;
  MustThrow([&] { ReplayNativeScene(barred,real,barred_backend); });
  assert(barred_backend.order.empty());
  auto unverified_camera = native;
  unverified_camera.widescreen_camera = false;
  FakeBackend unverified_backend;
  MustThrow([&] { ReplayNativeScene(unverified_camera,real,unverified_backend); });
  assert(unverified_backend.order.empty());
  FakeBackend b;
  ReplayNativeScene(native, real, b);
  assert((b.order == std::vector<std::string>{"BeginScene","DrawIndexed","DrawIndexed","ResolveScene","Composite"}));
  assert(b.scene.width == 2560 && b.scene.height == 1600);
  assert(b.resolved.width == 2560 && b.resolved.height == 1600);
  assert(b.samples == 4 && b.rectangle.width == 2560);
  assert(b.aborts == 0);
  FakeBackend failed_draw; failed_draw.fail_draw_at = 2;
  MustThrow([&] { ReplayNativeScene(native, real, failed_draw); });
  assert(failed_draw.aborts == 1 && failed_draw.draw_number == 2);
  FakeBackend failed_begin; failed_begin.fail_begin = true;
  MustThrow([&] { ReplayNativeScene(native, real, failed_begin); });
  assert(failed_begin.aborts == 1 && failed_begin.draw_number == 0);
  FakeBackend failed_resolve; failed_resolve.fail_resolve = true;
  MustThrow([&] { ReplayNativeScene(native, real, failed_resolve); });
  assert(failed_resolve.aborts == 1);
  FakeBackend failed_composite; failed_composite.fail_composite = true;
  MustThrow([&] { ReplayNativeScene(native, real, failed_composite); });
  assert(failed_composite.aborts == 1);
  // 2560x1600 stays PHYSICAL 2560x1600, while 4x->2x MSAA
  // reduces color samples from 16,384,000 to 8,192,000.
  s.max_scene_color_samples = 8u << 20;
  const auto balanced = PlanFrame(s);
  assert(balanced.world.width == 2560 && balanced.world.height == 1600);
  assert(balanced.samples == 2 && balanced.direct_engine_path_ready);
  FakeBackend budget_backend;
  ReplayNativeScene(balanced, real, budget_backend);
  assert(budget_backend.samples == 2 && budget_backend.resolved.height == 1600);
  s.max_scene_color_samples = 1;
  assert(PlanFrame(s).samples == 1); // never upscale or shrink pixels
  s.max_scene_color_samples = 0;
  s.samples = 1;
  FakeBackend c;
  ReplayNativeScene(PlanFrame(s), real, c);
  assert((c.order == std::vector<std::string>{"BeginScene","DrawIndexed","DrawIndexed","ResolveScene","Composite"}));
  assert(c.resolved.width==2560 && c.resolved.height==1600);
  FakeBackend invalid_1x;invalid_1x.fail_resolve=true;
  MustThrow([&] { ReplayNativeScene(PlanFrame(s),real,invalid_1x); });
  assert(invalid_1x.aborts==1 && invalid_1x.order.back()=="ResolveScene");
  s.engine_draw_hook_verified = false;
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), real, d); });
  s.engine_draw_hook_verified = true;
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), {}, d); });
  std::vector<IndexedDraw> bad{{0, 2, 3, 4, 6, 0}};
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), bad, d); });
  // An invalid *later* draw must not leave half a GPU frame submitted.
  std::vector<IndexedDraw> late_bad = real;
  late_bad[1].index_buffer = 0;
  FakeBackend untouched;
  MustThrow([&] { ReplayNativeScene(PlanFrame(s), late_bad, untouched); });
  assert(untouched.order.empty());
  late_bad[1] = {5, 6, 7, 8, 2, UINT32_MAX};
  MustThrow([&] { ReplayNativeScene(PlanFrame(s), late_bad, untouched); });
  assert(untouched.order.empty());
  // Public callers may supply a FrameGeometry instead of PlanFrame: fail closed.
  auto bad_samples = PlanFrame(s);
  bad_samples.samples = 3;
  MustThrow([&] { ReplayNativeScene(bad_samples, real, untouched); });
  assert(untouched.order.empty());
  s.backbuffer = {3440, 1440};
  s.projection_hook_verified = false;
  const auto ultrawide = PlanFrame(s);
  assert(ultrawide.world.width == 3440 && ultrawide.composite.left == 0);
  assert(!ultrawide.direct_engine_path_ready && !ultrawide.widescreen_camera);
  s.projection_hook_verified=true;
  const auto ultra_fov=PlanFrame(s);
  assert(ultra_fov.world.width==3440 && ultra_fov.world.height==1440 &&
         ultra_fov.original_guest.width==1280 &&
         ultra_fov.original_guest.height==720);
  const auto hud_ultra=hulk::display::FitOriginalPicture(
      ultra_fov.world.width,ultra_fov.world.height,
      ultra_fov.original_guest.width,ultra_fov.original_guest.height);
  assert(hud_ultra.valid && hud_ultra.x==440 && hud_ultra.y==0 &&
         hud_ultra.width==2560 && hud_ultra.height==1440);
  s.backbuffer={1080,1920};
  const auto vertical=PlanFrame(s);
  const auto hud_vertical=hulk::display::FitOriginalPicture(
      vertical.world.width,vertical.world.height,
      vertical.original_guest.width,vertical.original_guest.height);
  assert(vertical.world.width==1080 && vertical.world.height==1920 &&
         hud_vertical.valid && hud_vertical.width==1080 &&
         hud_vertical.height==607 && hud_vertical.y==656);
  s.backbuffer={1024,768};
  s.projection_hook_verified=false;
  const auto unverified_4_3=PlanFrame(s);
  assert(!unverified_4_3.direct_engine_path_ready &&
         unverified_4_3.composite.left==0);
  s.projection_hook_verified=true;
  const auto original_4_3=PlanFrame(s);
  assert(original_4_3.direct_engine_path_ready &&
         original_4_3.world.width==1024 && original_4_3.world.height==768);
  s.backbuffer={16384,16384};
  MustThrow([&] { (void)PlanFrame(s); });
  s.backbuffer={1920,1080};  // isolate invalid MSAA from invalid 16K^2
  s.samples = 3;
  MustThrow([&] { (void)PlanFrame(s); });
  s.samples = 4;
  s.backbuffer = {0, 1600};
  MustThrow([&] { (void)PlanFrame(s); });
}
