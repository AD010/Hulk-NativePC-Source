// Instrument the three verified Xbox 360 input reader callsites and their
// one verified direct parent. NOT yet identified as gameplay camera hooks.
// Developer-only: -DHULK_GAME_INPUT_PROBE=ON.
#include <atomic>
#include <cstdint>
#include <rex/hook.h>
#include <rex/logging.h>
#define HULK_INPUT_PROBE(address)                                               \
  REX_EXTERN(__imp__sub_##address);                                             \
  REX_HOOK_RAW(sub_##address) {                                                 \
    static std::atomic<uint64_t> calls{0};                                     \
    const uint64_t n = calls.fetch_add(1, std::memory_order_relaxed) + 1;      \
    if (n <= 8 || (n & (n - 1)) == 0) {                                        \
      REXLOG_INFO("[HULK_INPUT] guest=0x" #address                             \
                  " n={} lr={:016X} r3={:08X} r4={:08X} r5={:08X}",           \
                  n, ctx.lr, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);              \
    }                                                                           \
    __imp__sub_##address(ctx, base);                                           \
  }
HULK_INPUT_PROBE(8265E370)
HULK_INPUT_PROBE(82678E30)
HULK_INPUT_PROBE(82679278)
HULK_INPUT_PROBE(8265E7A0)
