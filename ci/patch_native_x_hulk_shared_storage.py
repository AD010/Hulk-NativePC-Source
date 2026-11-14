#!/usr/bin/env python3
"""Hulk: share native Vulkan storage for exactly equivalent Xbox360 sample-grid views.

Log 131 proves color base1024 pitch1280 1280x720 1x and base1024
pitch640 640x360 4x are distinct Diligent targets, both host 2560x1440.
The two Xbox EDRAM descriptors refer to the same sample grid. Existing
reverse-Resolve selection is insufficient: normal Draw/Clear sees distinct
native textures. Share one Diligent image for equivalent views but preserve
each guest view's width, height and host scale independently. Include depth
base0 with the exact same key rules. Never alias reduced/shadow targets.
Run after the existing MSAA-grid and reverse-alias patches on pinned NativeX.
This corrects a proven source-level split; visual correctness still needs GPU
replay/user testing, especially for shaders and unrelated depth surfaces.
"""
import subprocess
import sys
from pathlib import Path

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
HEADER_OLD = "  HostTarget* FindMsaaAlias(const GuestSurface& surface, uint32_t& samples_x, uint32_t& samples_y);"
HEADER_NEW = HEADER_OLD + """
  // Two exact Xbox sample-grid views can own the same native image.
  HostTarget* FindHulkSharedStorage(const GuestSurface& surface, Kind kind);"""
METHOD_ANCHOR = "HostTarget* RenderTargets::Get(const GuestSurface& s, Kind kind) {"
METHOD_NEW = """// On Xbox 360, a 640x360/4x and a 1280x720/1x view with equal
// EDRAM base/storage format represent the SAME physical pixel/sample grid.
HostTarget* RenderTargets::FindHulkSharedStorage(const GuestSurface& s, Kind kind) {
  if (!hulk_sample_grid_ || !s || kind != Kind::kRender ||
      !hulk_output_width_ || !hulk_output_height_) return nullptr;
  const uint32_t guest_format = std::get<4>(SurfaceKey(s));
  SurfaceTuple counterpart;
  if (s.msaa() == 0 && s.width == hulk_output_width_ &&
      s.height == hulk_output_height_ && s.pitch() == s.width &&
      (s.width % 2) == 0 && (s.height % 2) == 0) {
    counterpart = {s.depth, s.edram_base(), s.pitch() / 2, 2,
                   guest_format, s.width / 2, s.height / 2};
  } else if (s.msaa() == 2 && s.width * 2 == hulk_output_width_ &&
             s.height * 2 == hulk_output_height_ && s.pitch() == s.width) {
    counterpart = {s.depth, s.edram_base(), s.pitch() * 2, 0,
                   guest_format, s.width * 2, s.height * 2};
  } else {
    return nullptr;
  }
  auto it = targets_.find(Key{counterpart, Kind::kRender});
  if (it == targets_.end() || !it->second.texture) return nullptr;
  // NativeX uses a host image with one sample per pixel, including when
  // the guest describes 4x MSAA; the host *size* encodes the sample grid.
  const uint32_t host_width = scale_.HostWidth(s.msaa() == 2 ? s.width * 2 : s.width);
  const uint32_t host_height = scale_.HostHeight(s.msaa() == 2 ? s.height * 2 : s.height);
  if (it->second.width != host_width || it->second.height != host_height ||
      it->second.samples != 1 || it->second.depth != s.depth ||
      !it->second.view || !it->second.srv) return nullptr;
  return &it->second;
}

""" + METHOD_ANCHOR
BODY_OLD = """  if (it != targets_.end()) {
    it->second.last_used = frame_;
    return it->second.texture ? &it->second : nullptr;
  }
  HostTarget& target = targets_[key];
  target.last_used = frame_;

  TEXTURE_FORMAT format = s.depth ? HostDepthFormat(guest_format, depth_unorm24_) : HostColorFormat(guest_format);"""
BODY_NEW = """  if (it != targets_.end()) {
    it->second.last_used = frame_;
    // A sibling guest view may have drawn/cleared the SAME host texture
    // since this view was used. Synchronize newest write sequence lazily.
    if (HostTarget* sibling = FindHulkSharedStorage(s, kind)) {
      const uint64_t newest = std::max(it->second.written, sibling->written);
      it->second.written = sibling->written = newest;
      sibling->last_used = frame_;
    }
    return it->second.texture ? &it->second : nullptr;
  }
  HostTarget& target = targets_[key];
  target.last_used = frame_;

  TEXTURE_FORMAT format = s.depth ? HostDepthFormat(guest_format, depth_unorm24_) : HostColorFormat(guest_format);
  // Share the IMAGE for identical EDRAM samples. Do not share the guest
  // view's dimensions or scaling, which are different across 1x and 4x.
  if (HostTarget* sibling = FindHulkSharedStorage(s, kind)) {
    if (format == sibling->format) {
      RenderScale view_scale = scale_;
      if (s.msaa() == 2) { view_scale.x *= 2.0f; view_scale.y *= 2.0f; }
      target.texture = sibling->texture;
      target.view = sibling->view;
      target.srv = sibling->srv;
      target.stencil_srv = sibling->stencil_srv;
      target.width = sibling->width;
      target.height = sibling->height;
      target.guest_width = s.width;
      target.guest_height = s.height;
      target.samples = sibling->samples;
      target.format = sibling->format;
      target.depth = sibling->depth;
      target.depth_float24 = sibling->depth_float24;
      target.reduced = false;
      target.shadow = false;
      target.scale = view_scale;
      target.written = sibling->written;
      sibling->last_used = frame_;
      static uint32_t hulk_shared_logged = 0;
      if (hulk_shared_logged++ < 8)
        REXLOG_INFO("[HULK/SHARED-STORAGE] {} {}x{} {}x guest view shares ONE {}x{} native image base={} pitch={}",
                    s.depth ? "depth" : "color", s.width, s.height, s.samples(),
                    target.width, target.height, s.edram_base(), s.pitch());
      return &target;
    }
  }"""


# NativeX D139 introduced a four-slot recent-target memo *before* the
# targets_ lookup. A Hulk 1x/4x equivalent-view sibling may have changed
# the image since the cached view last drew. Revalidate its shared write
# epoch on BOTH D139 recent-cache and map-hit paths, never reuse stale
# written metadata. Keep D139's actual recent-target lookup optimization.
BODY_D140_OLD = """  for (const Recent& recent : recent_) {
    if (recent.target && recent.key == key) {
      recent.target->last_used = frame_;
      return recent.target;
    }
  }
  auto remember = [&](HostTarget* found) {
    recent_[recent_next_] = {key, found};
    recent_next_ = (recent_next_ + 1) % uint32_t(recent_.size());
    return found;
  };
  uint32_t guest_format = std::get<4>(std::get<0>(key));
  bool reduced = kind == Kind::kReduced, shadow = kind == Kind::kShadow, native = kind == Kind::kNative;
  auto it = targets_.find(key);
  if (it != targets_.end()) {
    it->second.last_used = frame_;
    return it->second.texture ? remember(&it->second) : nullptr;
  }
  HostTarget& target = targets_[key];
  target.last_used = frame_;

  TEXTURE_FORMAT format = s.depth ? HostDepthFormat(guest_format, depth_unorm24_) : HostColorFormat(guest_format);"""
BODY_D140_NEW = """  for (const Recent& recent : recent_) {
    if (recent.target && recent.key == key) {
      recent.target->last_used = frame_;
      // HULK/D139-RECENT-SHARED-EPOCH: one guest EDRAM view's write is
      // shared by its sibling even on a cached render-target lookup.
      if (HostTarget* sibling = FindHulkSharedStorage(s, kind)) {
        const uint64_t newest = std::max(recent.target->written, sibling->written);
        recent.target->written = sibling->written = newest;
        sibling->last_used = frame_;
      }
      return recent.target;
    }
  }
  auto remember = [&](HostTarget* found) {
    recent_[recent_next_] = {key, found};
    recent_next_ = (recent_next_ + 1) % uint32_t(recent_.size());
    return found;
  };
  uint32_t guest_format = std::get<4>(std::get<0>(key));
  bool reduced = kind == Kind::kReduced, shadow = kind == Kind::kShadow, native = kind == Kind::kNative;
  auto it = targets_.find(key);
  if (it != targets_.end()) {
    it->second.last_used = frame_;
    if (HostTarget* sibling = FindHulkSharedStorage(s, kind)) {
      const uint64_t newest = std::max(it->second.written, sibling->written);
      it->second.written = sibling->written = newest;
      sibling->last_used = frame_;
    }
    return it->second.texture ? remember(&it->second) : nullptr;
  }
  HostTarget& target = targets_[key];
  target.last_used = frame_;

  TEXTURE_FORMAT format = s.depth ? HostDepthFormat(guest_format, depth_unorm24_) : HostColorFormat(guest_format);
  // Build a sibling guest view over the very SAME underlying pixel grid.
  // Retain D139's recent-target lookup for its next native draw.
  if (HostTarget* sibling = FindHulkSharedStorage(s, kind)) {
    if (format == sibling->format) {
      RenderScale view_scale = scale_;
      if (s.msaa() == 2) { view_scale.x *= 2.0f; view_scale.y *= 2.0f; }
      target.texture = sibling->texture;
      target.view = sibling->view;
      target.srv = sibling->srv;
      target.stencil_srv = sibling->stencil_srv;
      target.width = sibling->width;
      target.height = sibling->height;
      target.guest_width = s.width;
      target.guest_height = s.height;
      target.samples = sibling->samples;
      target.format = sibling->format;
      target.depth = sibling->depth;
      target.depth_float24 = sibling->depth_float24;
      target.reduced = false;
      target.shadow = false;
      target.native = false;
      target.scale = view_scale;
      target.written = sibling->written;
      sibling->last_used = frame_;
      static uint32_t hulk_shared_logged = 0;
      if (hulk_shared_logged++ < 8)
        REXLOG_INFO("[HULK/SHARED-STORAGE] {} {}x{} {}x guest view shares ONE {}x{} native image base={} pitch={}",
                    s.depth ? "depth" : "color", s.width, s.height, s.samples(),
                    target.width, target.height, s.edram_base(), s.pitch());
      return remember(&target);
    }
  }"""

def once(src, old, new):
    if src.count(new) == 1:
        return src, False
    if src.count(old) != 1:
        raise ValueError("NativeX anchor missing/ambiguous: " + repr(old[:80]))
    return src.replace(old, new), True

def patch_header(src):
    return once(src, HEADER_OLD, HEADER_NEW)

def patch_cpp(src):
    src, a = once(src, METHOD_ANCHOR, METHOD_NEW)
    if BODY_D140_NEW in src:
        b = False
    elif BODY_D140_OLD in src:
        src, b = once(src, BODY_D140_OLD, BODY_D140_NEW)
    else:
        src, b = once(src, BODY_OLD, BODY_NEW)
    if a != b:
        raise ValueError("Partial shared-image patch; refusing write")
    return src, a

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_shared_storage.py <NativeX source>")
    root = Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if rev != PIN:
        raise ValueError("NativeX SHA changed: " + rev)
    edits = []
    for path, action in ((root / "src/targets.h", patch_header),
                         (root / "src/targets.cpp", patch_cpp)):
        updated, changed = action(path.read_text(encoding="utf-8"))
        edits.append((path, updated, changed))
    if edits[0][2] != edits[1][2]:
        raise ValueError("Partial installation; no writes")
    for path, src, changed in edits:
        if changed:
            path.write_text(src, encoding="utf-8", newline="\n")
    print("Hulk shared native color/depth sample grid:", "installed" if edits[0][2] else "already installed")

if __name__ == "__main__":
    main()
