#include "../native_port/src/eor_resource_generation.h"
#include <cassert>
using namespace hulk::eor::gpu;
int main() {
  using U=NativeResourceUpdate;
  for(unsigned mask=0;mask<8;++mask) {
    const bool exists=mask&1u, device=mask&2u, pack=mask&4u;
    const auto expected=!exists || !device?U::Recreate:pack?U::Reuse:U::ReplaceShaderPack;
    assert(SelectNativeResourceUpdate(exists,device,pack)==expected);
  }
}
