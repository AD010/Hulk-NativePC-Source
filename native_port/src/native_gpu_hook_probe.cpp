// Read-only live PPC candidate-call profiler for Hulk USA.
// Does NOT assume that PM4 packet writers are D3D Draw/Resolve calls.
// All original recompiled function bodies execute unmodified.
#include "native_gpu_hook_probe.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
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
std::array<std::atomic<uint32_t>, kMaxSites> r3s{}, r4s{}, r5s{};
std::atomic<int64_t> next_report_ms{1000};
Clock::time_point StartingTime() {
  static const Clock::time_point start = Clock::now();
  return start;
}
struct Entry { uint32_t address, r3, r4, r5; uint64_t hits; };
}
void Hit(uint32_t index, uint32_t address, uint32_t r3, uint32_t r4, uint32_t r5) {
  if (index >= kMaxSites || !REXCVAR_GET(hulk_gpu_hook_probe)) return;
  addresses[index].store(address, std::memory_order_relaxed);
  const auto n = counts[index].fetch_add(1, std::memory_order_relaxed);
  if (!n) {
    r3s[index].store(r3, std::memory_order_relaxed);
    r4s[index].store(r4, std::memory_order_relaxed);
    r5s[index].store(r5, std::memory_order_relaxed);
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
    active.push_back({addresses[i].load(std::memory_order_relaxed),
                      r3s[i].load(std::memory_order_relaxed),
                      r4s[i].load(std::memory_order_relaxed),
                      r5s[i].load(std::memory_order_relaxed), count});
  }
  std::sort(active.begin(), active.end(),
            [](const Entry& a, const Entry& b) { return a.hits > b.hits; });
  std::ostringstream summary;
  const auto displayed = std::min<size_t>(active.size(), 16);
  for (size_t i = 0; i < displayed; ++i) {
    const auto& e = active[i];
    summary << " 0x" << std::hex << std::setw(8) << std::setfill('0')
            << e.address << std::dec << '=' << e.hits
            << "(r3=0x" << std::hex << e.r3 << ",r4=0x" << e.r4
            << ",r5=0x" << e.r5 << std::dec << ')';
  }
  REXLOG_INFO("[HULK/GPU-PROBE] active_sites={} top:{}",
              active.size(), summary.str());
}
} // namespace hulk::gpu_probe
