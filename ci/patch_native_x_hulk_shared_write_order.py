#!/usr/bin/env python3
"""Use physical Vulkan image write history when choosing MSAA/depth Resolve views.

Hulk's 640x360/4x and 1280x720/1x targets share one actual ITexture
(HULK/SHARED-STORAGE), but keep separate HostTarget::written counters.
FindMsaaAlias and FindColorOverDepth picked sources by stale per-view stamps.
Compare the newest timestamp among views of the SAME texture instead.
No frame clear, guessed XDK hook, shader substitution or new host resource.
"""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys

PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA",
                     "bf965e952c98e050753e91d08f32685a9f754b12")
SUPPORTED = {"bf965e952c98e050753e91d08f32685a9f754b12",
             "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"}
HEADER_ANCHOR = """  HostTarget* FindHulkSharedStorage(const GuestSurface& surface, Kind kind);"""
HEADER_NEW = HEADER_ANCHOR + """
  uint64_t HulkSharedLatestWrite(const HostTarget& candidate) const;"""
CPP_ANCHOR = """HostTarget* RenderTargets::FindMsaaAlias(const GuestSurface& s, uint32_t& samples_x, uint32_t& samples_y) {"""
CPP_HELPER = """// HULK/SHARED-RESOLVE-WRITE: HostTarget is a guest descriptor; identical
// ITexture backing across 1x/4x guest views must share its newest write
// epoch for resolve selection. Non-Hulk/standalone images are unchanged.
uint64_t RenderTargets::HulkSharedLatestWrite(const HostTarget& candidate) const {
  if (!hulk_sample_grid_ || !candidate.texture) return candidate.written;
  uint64_t latest = candidate.written;
  for (const auto& [key, other] : targets_) {
    if (std::get<1>(key) != Kind::kRender || !other.texture ||
        other.texture.RawPtr() != candidate.texture.RawPtr()) continue;
    latest = std::max(latest, other.written);
  }
  return latest;
}

""" + CPP_ANCHOR
MSAA_OLD = """  HostTarget* found = nullptr;
  for (auto& [target_key, target] : targets_) {
    const SurfaceTuple& t = std::get<0>(target_key);
    // A reduced target too (D121): the surface last drawn in an upscaled view is there.
    if (std::get<1>(target_key) == Kind::kShadow || !target.texture || std::get<0>(t) != s.depth) continue;
    if (std::get<1>(t) != std::get<1>(key) || std::get<3>(t) != 0 || std::get<4>(t) != std::get<4>(key)) continue;
    if (std::get<2>(t) != s.pitch() * samples_x) continue;
    if (!found || target.written > found->written) found = &target;
  }
  if (found) found->last_used = frame_;
  return found;
}"""
MSAA_NEW = """  HostTarget* found = nullptr;
  uint64_t found_write = 0;
  for (auto& [target_key, target] : targets_) {
    const SurfaceTuple& t = std::get<0>(target_key);
    // A reduced target too (D121): the surface last drawn in an upscaled view is there.
    if (std::get<1>(target_key) == Kind::kShadow || !target.texture || std::get<0>(t) != s.depth) continue;
    if (std::get<1>(t) != std::get<1>(key) || std::get<3>(t) != 0 || std::get<4>(t) != std::get<4>(key)) continue;
    if (std::get<2>(t) != s.pitch() * samples_x) continue;
    const uint64_t physical_write = HulkSharedLatestWrite(target);
    if (!found || physical_write > found_write) {
      found = &target;
      found_write = physical_write;
    }
  }
  if (found) {
    found->written = std::max(found->written, found_write);
    found->last_used = frame_;
  }
  return found;
}"""
DEPTH_OLD = """    HostTarget& target = it->second;
    if (std::get<1>(it->first) != kind || !target.texture || target.written <= written) continue;
    if (!found || target.written > found->written) found = &target;
  }
  return found;
}"""
DEPTH_NEW = """    HostTarget& target = it->second;
    if (std::get<1>(it->first) != kind || !target.texture) continue;
    const uint64_t physical_write = HulkSharedLatestWrite(target);
    if (physical_write <= written) continue;
    if (!found || physical_write > found_write) {
      found = &target;
      found_write = physical_write;
    }
  }
  if (found) found->written = std::max(found->written, found_write);
  return found;
}"""
DEPTH_DECL_OLD = """  HostTarget* found = nullptr;
  Key from{SurfaceTuple{false, s.edram_base(), s.pitch(), 0, k8888, 0, 0}, Kind::kRender};"""
DEPTH_DECL_NEW = """  HostTarget* found = nullptr;
  uint64_t found_write = 0;
  Key from{SurfaceTuple{false, s.edram_base(), s.pitch(), 0, k8888, 0, 0}, Kind::kRender};"""


# The Hulk reverse-Resolve bridge must not reinterpret a Vulkan image as
# another source when both guest views already share that EXACT image.
# Compare physical-write epochs only across truly distinct native textures.
# Otherwise 1x/4x coordinate conversion can be applied to the same pixels.
REVERSE_OLD = """    if (alias && alias->written > (source ? source->written : 0)) {"""
REVERSE_NEW = """    // HULK/REVERSE-ALIAS-PHYSICAL: shared 1x/4x storage means there is
    // no copy between native textures. Do not reinterpret its coordinates
    // or use stale guest-view write stamps to select a Resolve source.
    const uint64_t hulk_alias_write = alias
        ? g_targets.HulkSharedLatestWrite(*alias) : 0u;
    const uint64_t hulk_source_write = source
        ? g_targets.HulkSharedLatestWrite(*source) : 0u;
    if (alias && (!source || alias->texture.RawPtr() != source->texture.RawPtr()) &&
        hulk_alias_write > hulk_source_write) {"""


def patch_renderer(source: str) -> tuple[str, bool]:
    if source.count(REVERSE_NEW) == 1:
        if source.count(REVERSE_OLD):
            raise ValueError("Partial Hulk physical reverse-alias condition")
        return source, False
    if source.count(REVERSE_OLD) != 1 or "HULK/REVERSE-ALIAS" not in source:
        raise ValueError("Expected the existing pinned Hulk reverse-alias source")
    if "HULK/REVERSE-ALIAS-PHYSICAL" in source:
        raise ValueError("Partial reverse alias stabilization")
    return source.replace(REVERSE_OLD, REVERSE_NEW, 1), True


def replace_once(s: str, old: str, new: str) -> str:
    count = s.count(old)
    if count != 1:
        raise ValueError(f"NativeX shared-resolve source drift: {old[:68]!r}, count={count}")
    return s.replace(old, new, 1)


def patch(header: str, cpp: str) -> tuple[str, str, bool]:
    done = HEADER_NEW in header and CPP_HELPER in cpp and MSAA_NEW in cpp and DEPTH_NEW in cpp
    if done:
        return header, cpp, False
    if ("HULK/SHARED-RESOLVE-WRITE" in cpp or HEADER_NEW in header or
        MSAA_NEW in cpp or DEPTH_NEW in cpp):
        raise ValueError("Partial NativeX Hulk physical write epoch patch")
    if "FindHulkSharedStorage(const GuestSurface& s, Kind kind)" not in cpp:
        raise ValueError("Install Hulk exact shared-image storage patch first")
    header = replace_once(header, HEADER_ANCHOR, HEADER_NEW)
    cpp = replace_once(cpp, CPP_ANCHOR, CPP_HELPER)
    cpp = replace_once(cpp, MSAA_OLD, MSAA_NEW)
    cpp = replace_once(cpp, DEPTH_DECL_OLD, DEPTH_DECL_NEW)
    cpp = replace_once(cpp, DEPTH_OLD, DEPTH_NEW)
    return header, cpp, True


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_native_x_hulk_shared_write_order.py NativeXRoot")
    root = Path(sys.argv[1]).resolve()
    actual = subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],
                                     text=True).strip()
    if PIN not in SUPPORTED or actual != PIN:
        raise ValueError("Unsupported pinned NativeX revision "+actual)
    header, cpp = root / "src/targets.h", root / "src/targets.cpp"
    renderer = root / "src/renderer.cpp"
    h, c, changed = patch(header.read_text(encoding="utf-8"),
                          cpp.read_text(encoding="utf-8"))
    render, alias_changed = patch_renderer(renderer.read_text(encoding="utf-8"))
    if changed != alias_changed:
        raise ValueError("Mixed Hulk physical-image write/resolve stabilization")
    if changed:
        header.write_text(h, encoding="utf-8", newline="\n")
        cpp.write_text(c, encoding="utf-8", newline="\n")
        renderer.write_text(render, encoding="utf-8", newline="\n")
    print("Hulk physical Vulkan RT/depth/alias resolve write epochs:",
          "patched" if changed else "already patched")


if __name__ == "__main__":
    main()
