#include "../native_port/src/eor_native_present_contract.h"
#include <cassert>
using namespace hulk::eor::gpu;
int main() {
  const NativeColorSurface native{2560,1600,87,87,1,1,1,true,false};
  const NativeColorSurface back{2560,1600,87,87,1,1,1,false,true};
  assert(SelectNativeColorPresentPath(native,back,false)==NativeColorPresentPath::ExactCopy);
  auto direct=native;
  direct.render_target=true;
  assert(SelectNativeColorPresentPath(direct,back,true)==NativeColorPresentPath::AlreadyBackbuffer);
  assert(SelectNativeColorPresentPath(native,back,true)==NativeColorPresentPath::Reject);
  auto wrong=native;
  wrong.height=1440;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.width=1280;wrong.height=720;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.sample_count=4;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.format=88;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.view_format=88;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.shader_resource=false;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.mip_levels=2;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  wrong=native;wrong.array_size=2;
  assert(SelectNativeColorPresentPath(wrong,back,false)==NativeColorPresentPath::Reject);
  auto dst=back;dst.render_target=false;
  assert(SelectNativeColorPresentPath(native,dst,false)==NativeColorPresentPath::Reject);
}
