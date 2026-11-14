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
  bool BeginScene(Size s, uint32_t n) override {
    order.push_back("BeginScene"); scene = s; samples = n; return true;
  }
  bool DrawIndexed(const IndexedDraw&) override {
    order.push_back("DrawIndexed"); return true;
  }
  bool ResolveScene(Size s) override {
    order.push_back("ResolveScene"); resolved = s; return true;
  }
  bool Composite(Rect r, Size) override {
    order.push_back("Composite"); rectangle = r; return true;
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
  s.samples = 1;
  FakeBackend c;
  ReplayNativeScene(PlanFrame(s), real, c);
  assert((c.order == std::vector<std::string>{"BeginScene","DrawIndexed","DrawIndexed","Composite"}));
  s.engine_draw_hook_verified = false;
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), real, d); });
  s.engine_draw_hook_verified = true;
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), {}, d); });
  std::vector<IndexedDraw> bad{{0, 2, 3, 4, 6, 0}};
  MustThrow([&] { FakeBackend d; ReplayNativeScene(PlanFrame(s), bad, d); });
  s.backbuffer = {3440, 1440};
  s.projection_hook_verified = false;
  const auto ultrawide = PlanFrame(s);
  assert(ultrawide.world.width == 2560 && ultrawide.composite.left == 440);
  s.samples = 3;
  MustThrow([&] { (void)PlanFrame(s); });
  s.samples = 4;
  s.backbuffer = {0, 1600};
  MustThrow([&] { (void)PlanFrame(s); });
}
