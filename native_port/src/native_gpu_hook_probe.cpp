// Read-only live PPC candidate-call profiler for Hulk USA.
// Does NOT assume that PM4 packet writers are D3D Draw/Resolve calls.
// All original recompiled function bodies execute unmodified.
#include "native_gpu_hook_probe.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

REXCVAR_DEFINE_BOOL(hulk_gpu_hook_probe, false, "Hulk/Diagnostics",
                    "Log counts and sample arguments of candidate XDK functions");

namespace hulk::gpu_probe {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kMaxSites = 256;
std::array<std::atomic<uint64_t>, kMaxSites> counts{};
std::array<std::atomic<uint32_t>, kMaxSites> addresses{};
// PPC integer arguments occupy r3..r10; f1/f2 contain raw FP registers.
// Capture their bit patterns and LR without interpreting unknown APIs.
std::array<std::array<std::atomic<uint32_t>, 8>, kMaxSites> gpr_samples{};
std::array<std::atomic<uint64_t>, kMaxSites> f1_bits{}, f2_bits{};
std::array<std::atomic<uint32_t>, kMaxSites> lr_samples{};
std::atomic<int64_t> next_report_ms{1000};
Clock::time_point StartingTime() {
  static const Clock::time_point start = Clock::now();
  return start;
}
struct Entry {
  uint32_t address = 0;
  std::array<uint32_t, 8> gpr{};
  uint64_t f1_raw = 0, f2_raw = 0;
  uint32_t lr = 0;
  uint64_t hits = 0;
};
}
void Hit(uint32_t index, uint32_t address, const PPCContext& ctx) {
  if (index >= kMaxSites || !REXCVAR_GET(hulk_gpu_hook_probe)) return;
  addresses[index].store(address, std::memory_order_relaxed);
  const auto n = counts[index].fetch_add(1, std::memory_order_relaxed);
  if (!n) {
    const std::array<uint32_t, 8> r = {
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
        ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
    for (size_t j = 0; j < r.size(); ++j)
      gpr_samples[index][j].store(r[j], std::memory_order_relaxed);
    f1_bits[index].store(std::bit_cast<uint64_t>(ctx.f1.f64),
                         std::memory_order_relaxed);
    f2_bits[index].store(std::bit_cast<uint64_t>(ctx.f2.f64),
                         std::memory_order_relaxed);
    lr_samples[index].store(static_cast<uint32_t>(ctx.lr),
                            std::memory_order_relaxed);
  }
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now() - StartingTime()).count();
  auto expected = next_report_ms.load(std::memory_order_relaxed);
  if (elapsed_ms < expected) return;
  if (!next_report_ms.compare_exchange_strong(expected, elapsed_ms + 1000,
                                               std::memory_order_relaxed)) return;
  std::vector<Entry> active;
  for (uint32_t i = 0; i < kMaxSites; ++i) {
    const auto count = counts[i].exchange(0, std::memory_order_relaxed);
    if (!count) continue;
    Entry e{};
    e.address = addresses[i].load(std::memory_order_relaxed);
    for (size_t j = 0; j < e.gpr.size(); ++j)
      e.gpr[j] = gpr_samples[i][j].load(std::memory_order_relaxed);
    e.f1_raw = f1_bits[i].load(std::memory_order_relaxed);
    e.f2_raw = f2_bits[i].load(std::memory_order_relaxed);
    e.lr = lr_samples[i].load(std::memory_order_relaxed);
    e.hits = count;
    active.push_back(e);
  }
  std::sort(active.begin(), active.end(),
            [](const Entry& a, const Entry& b) { return a.hits > b.hits; });
  std::ostringstream summary;
  const auto displayed = std::min<size_t>(active.size(), 16);
  for (size_t i = 0; i < displayed; ++i) {
    const auto& e = active[i];
    summary << " 0x" << std::hex << std::setw(8) << std::setfill('0')
            << e.address << std::dec << '=' << e.hits
            << "(lr=0x" << std::hex << e.lr << " r3-r10=[";
    for (size_t j = 0; j < e.gpr.size(); ++j) {
      if (j) summary << ',';
      summary << "0x" << e.gpr[j];
    }
    summary << "] f1bits=0x" << e.f1_raw
            << " f2bits=0x" << e.f2_raw << std::dec << ')';
  }
  REXLOG_INFO("[HULK/GPU-PROBE/ABI] active_sites={} top:{}",
              active.size(), summary.str());
}
} // namespace hulk::gpu_probe
