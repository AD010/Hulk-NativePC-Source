#pragma once
#include <cstdint>
#include <rex/ppc/context.h>

namespace hulk::gpu_probe {
// Read-only PPC entry snapshot; no guest memory is dereferenced or modified.
void Hit(uint32_t index, uint32_t address, const PPCContext& ctx);
}
