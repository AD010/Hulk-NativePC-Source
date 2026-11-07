// Hulk Recompiled PM4 diagnostic probes (2026-11-05).
// Developer-only; set -DHULK_XDK_PM4_PROBE=ON. Never redirects rendering.
// All 59 functions have guest/host mappings and PM4 constants; none is
// declared to be Draw*, Present, KickSegment etc. without runtime evidence.
// Does not read guest pointers: only register values, LR and call counts.
#include <atomic>
#include <cstdint>
#include <rex/hook.h>
#include <rex/logging.h>

#define HULK_PM4_PROBE(address)                                                 \
  REX_EXTERN(__imp__sub_##address);                                             \
  REX_HOOK_RAW(sub_##address) {                                                 \
    static std::atomic<uint64_t> calls{0};                                     \
    const uint64_t n = calls.fetch_add(1, std::memory_order_relaxed) + 1;      \
    if (n <= 8 || (n & (n - 1)) == 0) {                                        \
      REXLOG_INFO("[HULK_PM4] guest=0x" #address                               \
                  " n={} lr={:016X} r3={:08X} r4={:08X} r5={:08X}"            \
                  " r6={:08X} r7={:08X} r8={:08X}",                            \
                  n, ctx.lr, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,               \
                  ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);                          \
    }                                                                           \
    __imp__sub_##address(ctx, base);                                           \
  }

HULK_PM4_PROBE(8289BFA0)
HULK_PM4_PROBE(8288D458)
HULK_PM4_PROBE(8288D950)
HULK_PM4_PROBE(8288DF30)
HULK_PM4_PROBE(8288E770)
HULK_PM4_PROBE(8289A1F8)
HULK_PM4_PROBE(8288C618)
HULK_PM4_PROBE(8288E318)
HULK_PM4_PROBE(8288EBE8)
HULK_PM4_PROBE(828897F8)
HULK_PM4_PROBE(8289D730)
HULK_PM4_PROBE(8289D888)
HULK_PM4_PROBE(8289F5C8)
HULK_PM4_PROBE(8289F720)
HULK_PM4_PROBE(828737A8)
HULK_PM4_PROBE(828737E0)
HULK_PM4_PROBE(82887AD0)
HULK_PM4_PROBE(8289A190)
HULK_PM4_PROBE(828933D0)
HULK_PM4_PROBE(828A1548)
HULK_PM4_PROBE(828A2A18)
HULK_PM4_PROBE(8289F2A0)
HULK_PM4_PROBE(8287BCB0)
HULK_PM4_PROBE(8287EE18)
HULK_PM4_PROBE(828A04B0)
HULK_PM4_PROBE(82873460)
HULK_PM4_PROBE(82882390)
HULK_PM4_PROBE(82885958)
HULK_PM4_PROBE(828885B0)
HULK_PM4_PROBE(8289FDD0)
HULK_PM4_PROBE(828A01C0)
HULK_PM4_PROBE(828802D0)
HULK_PM4_PROBE(82880598)
HULK_PM4_PROBE(828873B0)
HULK_PM4_PROBE(82888020)
HULK_PM4_PROBE(8288C778)
HULK_PM4_PROBE(82899E20)
HULK_PM4_PROBE(82691080)
HULK_PM4_PROBE(8280F5C0)
HULK_PM4_PROBE(82874AF8)
HULK_PM4_PROBE(82881C88)
HULK_PM4_PROBE(82881E38)
HULK_PM4_PROBE(82885798)
HULK_PM4_PROBE(82885CC8)
HULK_PM4_PROBE(82886770)
HULK_PM4_PROBE(8288A900)
HULK_PM4_PROBE(8288BC48)
HULK_PM4_PROBE(8288BFE8)
HULK_PM4_PROBE(8288CAE0)
HULK_PM4_PROBE(8288CE50)
HULK_PM4_PROBE(8288F1C0)
HULK_PM4_PROBE(82891608)
HULK_PM4_PROBE(82899F10)
HULK_PM4_PROBE(8289DBE0)
HULK_PM4_PROBE(8289EE98)
HULK_PM4_PROBE(828A1348)
HULK_PM4_PROBE(828A26C8)
HULK_PM4_PROBE(828A2828)
HULK_PM4_PROBE(828A3A98)
