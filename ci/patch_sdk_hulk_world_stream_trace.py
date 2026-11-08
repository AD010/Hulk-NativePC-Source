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
#include <cctype>
#include <string>

REXCVAR_DEFINE_BOOL(hulk_world_stream_trace, false, "Hulk",
                    "Trace bounded original ARC/IND file reads to identify city high/low LOD");
"""
OLD_READ = """  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }"""
NEW_READ = """  const bool ok = file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read);
  // Diagnostic only: the original FileHandle::Read and result are unchanged.
  if (ok && REXCVAR_GET(hulk_world_stream_trace) && entry_) {
    std::string file_name = entry_->name();
    std::transform(file_name.begin(), file_name.end(), file_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (file_name.ends_with(".ARC") || file_name.ends_with(".IND")) {
      static std::atomic<uint32_t> recorded{0};
      const uint32_t seq = recorded.fetch_add(1, std::memory_order_relaxed);
      if (seq < 2000) {
        REXLOG_INFO("[HULK/STREAM] file={} offset={} requested={} read={} seq={}",
                    file_name, byte_offset, buffer.size(),
                    out_bytes_read ? *out_bytes_read : 0, seq);
      } else if (seq == 2000) {
        REXLOG_WARN("[HULK/STREAM] 2000 read events captured; tracing capped");
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
