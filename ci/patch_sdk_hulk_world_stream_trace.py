#!/usr/bin/env python3
"""Read-only guest ARC/IND read trace for Hulk world streaming reverse engineering.

Exact-anchor SDK source patch; no changes to returns, buffers, images,
gameplay, rendering or title-update assets. Default OFF.
"""
from pathlib import Path
import sys

INCLUDES = '#include <rex/filesystem/devices/host_path_file.h>\n'
NEW_INCLUDES = INCLUDES + """
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>

REXCVAR_DEFINE_BOOL(hulk_world_stream_trace, false, "Hulk",
                    "Trace bounded original ARC/IND file reads to identify city high/low LOD");
"""
OLD_READ = """  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }"""
NEW_READ = """  // HULK/STREAM-LIFETIME: do not stop observing after the first 2000
  // requests. Sampling is bounded per ten-second window and opt-in only.
  const bool tracing = REXCVAR_GET(hulk_world_stream_trace) && entry_;
  using HulkClock = std::chrono::steady_clock;
  const auto read_start = tracing ? HulkClock::now() : HulkClock::time_point{};
  const bool ok = file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read);
  if (tracing) {
    std::string file_name = entry_->name();
    std::transform(file_name.begin(), file_name.end(), file_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (file_name.ends_with(".ARC") || file_name.ends_with(".IND")) {
      const auto read_end = HulkClock::now();
      static const auto trace_start = read_start;
      static std::atomic<uint64_t> total_requests{0};
      static std::atomic<uint64_t> window_state{0};
      const uint64_t seq = total_requests.fetch_add(1, std::memory_order_relaxed);
      const uint64_t t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          read_end - trace_start).count();
      const uint64_t read_us = std::chrono::duration_cast<std::chrono::microseconds>(
          read_end - read_start).count();
      const uint64_t window = t_ms / 10000ull;
      // Atomic packed window/counter: avoids a per-read mutex on concurrent
      // streaming threads. Up to 256 exact reads + every 64th later request
      // (through request 16384) are logged per 10-second window.
      uint64_t state = window_state.load(std::memory_order_relaxed);
      uint32_t ordinal = 0;
      for (;;) {
        const uint32_t prev_window = uint32_t(state >> 32);
        const uint32_t prev_count = uint32_t(state);
        ordinal = prev_window == uint32_t(window)
            ? (prev_count == UINT32_MAX ? prev_count : prev_count + 1) : 1;
        const uint64_t next = (uint64_t(uint32_t(window)) << 32) | ordinal;
        if (window_state.compare_exchange_weak(
                state, next, std::memory_order_relaxed)) break;
      }
      if (ordinal <= 256 || (ordinal <= 16384 && (ordinal - 256) % 64 == 0)) {
        REXLOG_INFO("[HULK/STREAM] file={} offset={} requested={} read={} seq={} t_ms={} read_us={} ok={}",
                    file_name, byte_offset, buffer.size(),
                    out_bytes_read ? *out_bytes_read : 0, seq, t_ms, read_us, ok ? 1 : 0);
      } else if (ordinal == 257) {
        REXLOG_INFO("[HULK/STREAM] window={} sampling 1 in 64 reads after first 256", window);
      }
    }
  }
  return ok ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;"""


def apply_patch(source: str) -> tuple[str, bool]:
    if source.count(NEW_INCLUDES) == 1 and source.count(NEW_READ) == 1:
        return source, False
    if source.count(INCLUDES) != 1 or source.count(OLD_READ) != 1:
        raise ValueError("Unexpected ReXGlue HostPathFile ReadSync source; refusing patch")
    if NEW_INCLUDES in source or NEW_READ in source:
        raise ValueError("Partial Hulk stream trace patch; refusing mixed code")
    return source.replace(INCLUDES, NEW_INCLUDES).replace(OLD_READ, NEW_READ), True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_sdk_hulk_world_stream_trace.py <rexglue-sdk root>")
    path = Path(sys.argv[1]) / "src/filesystem/devices/host_path_file.cpp"
    patched, changed = apply_patch(path.read_text(encoding="utf-8"))
    if changed:
        path.write_text(patched, encoding="utf-8", newline="\n")
    print("Hulk ARC/IND trace:", "patched" if changed else "already installed")


if __name__ == "__main__":
    main()
