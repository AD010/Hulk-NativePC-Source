#!/usr/bin/env python3
"""Hulk NativeX: native Vulkan aliasing of 640x360 4x and 1280x720 1x EDRAM views.

Log 124:
 color 1280x720 base=1024 pitch=1280 1x (native host 2560x1440)
 color  640x360 base=1024 pitch= 640 4x (native host 2560x1440)

The two guest descriptors map to the SAME physical pixel/sample extent, but
NativeXRenderer currently stores them in separate Diligent Vulkan images.
If a recent 4x scene draw is followed by a 1x Resolve, the old 1x host image
is read, leaving stale solid-clear colors and a quarter-frame image.

This patch uses the most recently written matching 4x native host texture
when a 1280x720 1x surface is resolved. It maps the 1280-coordinate rectangle
to 640 guest sample-grid coordinates; both end up 2560 host pixels wide.
It neither simulates EDRAM nor uses Xenos rendering, and it does not change
guest surface metadata. The precise descriptor key prevents unrelated aliases.
"""
from pathlib import Path
import subprocess
import sys
import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
 "src/targets.h": [
  (
   "  HostTarget* FindMsaaAlias(const GuestSurface& surface, uint32_t& samples_x, uint32_t& samples_y);",
   "  HostTarget* FindMsaaAlias(const GuestSurface& surface, uint32_t& samples_x, uint32_t& samples_y);\n"
   "  // Hulk: a 1280x720/1x alias of the most recently written 640x360/4x\n"
   "  // native image with the same EDRAM base and doubled pitch.\n"
   "  HostTarget* FindHulkNativeSampleGridAlias(const GuestSurface& surface);"
  )
 ],
 "src/targets.cpp": [
  (
   "HostTarget* RenderTargets::FindMsaaAlias(const GuestSurface& s, uint32_t& samples_x, uint32_t& samples_y) {",
   "HostTarget* RenderTargets::FindHulkNativeSampleGridAlias(const GuestSurface& s) {\n"
   "  if (!hulk_sample_grid_ || !s || s.depth || s.msaa() != 0 ||\n"
   "      s.width != hulk_output_width_ || s.height != hulk_output_height_) return nullptr;\n"
   "  const SurfaceTuple desired = SurfaceKey(s);\n"
   "  HostTarget* found = nullptr;\n"
   "  for (auto& [key, host] : targets_) {\n"
   "    if (std::get<1>(key) != Kind::kRender || !host.texture || host.depth) continue;\n"
   "    const SurfaceTuple& candidate = std::get<0>(key);\n"
   "    if (std::get<1>(candidate) != s.edram_base() ||\n"
   "        std::get<2>(candidate) * 2 != s.pitch() ||\n"
   "        std::get<3>(candidate) != 2 ||\n"
   "        std::get<4>(candidate) != std::get<4>(desired) ||\n"
   "        std::get<5>(candidate) * 2 != s.width ||\n"
   "        std::get<6>(candidate) * 2 != s.height) continue;\n"
   "    if (!found || host.written > found->written) found = &host;\n"
   "  }\n"
   "  if (found) found->last_used = frame_;\n"
   "  return found;\n"
   "}\n\n"
   "HostTarget* RenderTargets::FindMsaaAlias(const GuestSurface& s, uint32_t& samples_x, uint32_t& samples_y) {"
  )
 ],
 "src/renderer.cpp": [
  (
   "  HostTarget* source = TargetFor(surface);",
   "  HostTarget* source = TargetFor(surface);\n"
   "  // The game's 1280/1x and 640/4x surfaces share the same physical\n"
   "  // sample-grid storage but have separate native Vulkan targets. When the\n"
   "  // 4x view was written more recently, resolve from that native image.\n"
   "  bool hulk_reverse_alias = false;\n"
   "  if (REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid) &&\n"
   "      surface && !surface.depth && surface.msaa() == 0 &&\n"
   "      surface.width == g_profile->output_width &&\n"
   "      surface.height == g_profile->output_height) {\n"
   "    HostTarget* alias = g_targets.FindHulkNativeSampleGridAlias(surface);\n"
   "    if (alias && alias->written > (source ? source->written : 0)) {\n"
   "      source = alias;\n"
   "      hulk_reverse_alias = true;\n"
   "      static std::atomic<uint64_t> hulk_aliased{0};\n"
   "      const uint64_t count = ++hulk_aliased;\n"
   "      if (count <= 16 || count % 1000 == 0)\n"
   "        REXLOG_INFO(\"[HULK/REVERSE-ALIAS] #{} 1280x720/1x pitch={} base={}\"\n"
   "                    \" resolved from latest 640x360/4x native image {}x{}\",\n"
   "                    count, surface.pitch(), surface.edram_base(),\n"
   "                    source->width, source->height);\n"
   "    }\n"
   "  }"
  ),
  (
   "  if (c.has_dest_point) {\n    to.x = c.dest_point[0];\n    to.y = c.dest_point[1];\n  }",
   "  if (c.has_dest_point) {\n    to.x = c.dest_point[0];\n    to.y = c.dest_point[1];\n  }\n"
   "  const Blitter::Rect hulk_reverse_original_from = from;\n"
   "  const Blitter::Rect hulk_reverse_original_to = to;\n"
   "  if (hulk_reverse_alias) {\n"
   "    // The source now uses the 640/4x guest view, not the caller's 1280/1x\n"
   "    // coordinates. Each source pixel contains a 2x2 native sample grid.\n"
   "    from.x /= 2;\n"
   "    from.y /= 2;\n"
   "    from.width = (from.width + 1) / 2;\n"
   "    from.height = (from.height + 1) / 2;\n"
   "  }"
  ),
  (
   "  to.width = from.width;\n  to.height = from.height;\n  copied = from;",
   "  to.width = from.width;\n  to.height = from.height;\n"
   "  if (hulk_reverse_alias) {\n"
   "    to.width = std::min(from.width * 2, target->guest_width -\n"
   "        std::min(target->guest_width, uint32_t(std::max(to.x, 0))));\n"
   "    to.height = std::min(from.height * 2, target->guest_height -\n"
   "        std::min(target->guest_height, uint32_t(std::max(to.y, 0))));\n"
   "  }\n"
   "  copied = from;"
  ),
  (
   "  bool scaled = source->reduced || aliased || hulk_sample_grid || hulk_host_scale_mismatch;",
   "  bool scaled = source->reduced || aliased || hulk_sample_grid || hulk_host_scale_mismatch || hulk_reverse_alias;"
  ),
  (
   "      !surface.depth && surface.msaa() == 2 &&\n"
   "      surface.width * 2 == g_profile->output_width &&\n"
   "      surface.height * 2 == g_profile->output_height &&",
   "      !surface.depth && (hulk_reverse_alias || surface.msaa() == 2) &&\n"
   "      (hulk_reverse_alias || (surface.width * 2 == g_profile->output_width &&\n"
   "                              surface.height * 2 == g_profile->output_height)) &&"
  )
 ]
}

# D127 already tracks the host-scale mismatch for ordinary NativeX resolves.
# The Hulk reverse alias must preserve those terms instead of restoring the
# earlier, incomplete scaled-copy expression.
if PIN == "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e":
    old, new = PATCHES["src/renderer.cpp"][3]
    baseline = "  bool scaled = source->reduced || aliased || "
    upstream = (baseline + "source->scale.x != target->scale.x || "
                "source->scale.y != target->scale.y || ")
    if not old.startswith(baseline) or not new.startswith(baseline):
        raise ValueError("NativeX D127 reverse-alias scaling transform drift")
    PATCHES["src/renderer.cpp"][3] = (
        old.replace(baseline, upstream, 1),
        new.replace(baseline, upstream, 1)
    )


# HULK/REVERSE-ALIAS-EXACT-RECT: the original half-size conversion loses
# subpixel (odd guest coordinate) placement and rounds widths up by two host
# pixels. Use the actual 1280x720 caller rectangle for the verified 1x/4x
# shared-storage alias when source and destination have the SAME integral host
# scale. Leave every unmatched/smaller/shadow/indirect resolve untouched.
HOST_RECT_OLD = """  from = HostRect(source->scale, from);
  to = HostRect(target->scale, to);"""
HOST_RECT_NEW = HOST_RECT_OLD + """
  if (hulk_reverse_alias && surface && source && target &&
      hulk_reverse_original_from.x >= 0 &&
      hulk_reverse_original_from.y >= 0 &&
      hulk_reverse_original_to.x >= 0 &&
      hulk_reverse_original_to.y >= 0 &&
      surface.width && surface.height &&
      target->guest_width == surface.width &&
      target->guest_height == surface.height &&
      source->width % surface.width == 0 &&
      source->height % surface.height == 0 &&
      target->width % target->guest_width == 0 &&
      target->height % target->guest_height == 0 &&
      source->width / surface.width == target->width / target->guest_width &&
      source->height / surface.height == target->height / target->guest_height &&
      uint32_t(hulk_reverse_original_from.x) < surface.width &&
      uint32_t(hulk_reverse_original_from.y) < surface.height &&
      uint32_t(hulk_reverse_original_to.x) < target->guest_width &&
      uint32_t(hulk_reverse_original_to.y) < target->guest_height) {
    // HULK/REVERSE-ALIAS-EXACT-RECT: calculate host coordinates directly
    // from the full-resolution guest region. Integer x/2, ceil(width/2)
    // lost one or two source pixels for odd starts/widths, creating seams
    // or a 2-pixel sample-grid mismatch in a 2560x1440 Vulkan frame.
    const uint32_t src_x = uint32_t(hulk_reverse_original_from.x);
    const uint32_t src_y = uint32_t(hulk_reverse_original_from.y);
    const uint32_t dst_x = uint32_t(hulk_reverse_original_to.x);
    const uint32_t dst_y = uint32_t(hulk_reverse_original_to.y);
    const uint32_t scale_x = source->width / surface.width;
    const uint32_t scale_y = source->height / surface.height;
    const uint32_t copy_w = std::min({
        hulk_reverse_original_from.width,
        surface.width - src_x, target->guest_width - dst_x});
    const uint32_t copy_h = std::min({
        hulk_reverse_original_from.height,
        surface.height - src_y, target->guest_height - dst_y});
    from = {int32_t(src_x * scale_x), int32_t(src_y * scale_y),
            copy_w * scale_x, copy_h * scale_y};
    to = {int32_t(dst_x * scale_x), int32_t(dst_y * scale_y),
          copy_w * scale_x, copy_h * scale_y};
  }"""
PATCHES["src/renderer.cpp"].append((HOST_RECT_OLD, HOST_RECT_NEW))


def transform(source, rules):
    ready = [new in source for old,new in rules]
    if all(ready):
        return source,False
    if any(ready):
        raise ValueError("Partially applied Hulk native sample-grid alias patch")
    for old,new in rules:
        if source.count(old)!=1:
            raise ValueError("Pinned NativeX alias patch anchor mismatch")
        source=source.replace(old,new)
    return source,True

def main():
    if len(sys.argv)!=2:
        raise SystemExit("usage: patch_native_x_hulk_reverse_alias.py <NativeX checkout>")
    root=Path(sys.argv[1]).resolve()
    rev=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if rev!=PIN:
        raise ValueError("NativeX version not pinned: "+rev)
    changes=[]
    for rel,rules in PATCHES.items():
        dst=root/rel
        new,changed=transform(dst.read_text(encoding="utf-8"),rules)
        changes.append((dst,new,changed))
    if len({c for _,_,c in changes})!=1:
        raise ValueError("Mixed native sample-grid alias patch; no partial writes")
    for dst,new,changed in changes:
        if changed:
            dst.write_text(new,encoding="utf-8",newline="\n")
    print("Hulk Vulkan guest-surface views alias native image by storage identity:",
          "installed" if changes[0][2] else "already installed")

if __name__=="__main__":
    main()
