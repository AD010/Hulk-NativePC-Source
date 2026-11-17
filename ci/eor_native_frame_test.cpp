#include "../native_port/src/eor_native_frame.h"
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
  FrameSetup s{};
  s.backbuffer = {2560, 1600};
  s.samples = 4;
  const auto legacy = PlanFrame(s);
  assert(legacy.world.width == 2560 && legacy.world.height == 1440);
  assert(legacy.composite.left == 0 && legacy.composite.top == 80);
  assert(!legacy.direct_engine_path_ready);
  MustThrow([&] { FakeBackend b; ReplayNativeScene(legacy,
                 std::vector<IndexedDraw>{{1, 2, 3, 4, 6, 0}}, b); });
  // Missing USA camera ABI must NOT block a fully verified native Draw
  // path. Keep original 16:9 geometry, rasterized at full host density,
  // with letterbox bars instead of stretched HUD/3D or fabricated FOV.
  s.engine_draw_hook_verified = true;
  s.offline_shader_cache_verified = true;
  const auto letterboxed = PlanFrame(s);
  assert(letterboxed.direct_engine_path_ready && !letterboxed.widescreen_camera);
  assert(letterboxed.world.width == 2560 && letterboxed.world.height == 1440);
  assert(letterboxed.composite.left == 0 && letterboxed.composite.top == 80);
  FakeBackend letterbox_gpu;
  ReplayNativeScene(letterboxed,
                    std::vector<IndexedDraw>{{1, 2, 3, 4, 6, 0}},
                    letterbox_gpu);
  assert(letterbox_gpu.scene.height == 1440 &&
         letterbox_gpu.resolved.height == 1440 &&
         letterbox_gpu.rectangle.top == 80 &&
         letterbox_gpu.aborts == 0 && letterbox_gpu.draw_number == 1);
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
  const std::vector<IndexedDraw> real{{1, 2, 3, 4, 6, 0}, {5, 6, 7, 8, 24, 6}};
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
  assert(ultrawide.world.width == 2560 && ultrawide.composite.left == 440);
  assert(ultrawide.direct_engine_path_ready && !ultrawide.widescreen_camera);
  s.samples = 3;
  MustThrow([&] { (void)PlanFrame(s); });
  s.samples = 4;
  s.backbuffer = {0, 1600};
  MustThrow([&] { (void)PlanFrame(s); });
}
