#!/usr/bin/env python3
"""Hulk: host-only resolution of 640x360/4x MSAA into a 1280x720 Vulkan scene.

The original Xenos guest surface retains its 640x360 size, pitch, MSAA and
register values. Only the independent Diligent/Vulkan texture is scaled 2x
relative to other targets, and its Resolve into the 1280x720 guest destination
has a matching 2x guest rectangle. This replaces the unsafe experiment of
rewriting GuestSurface to 1280x720 and masking its MSAA bits.

Run on the pinned NativeXRenderer AFTER all other Hulk patches.
"""
from pathlib import Path
import subprocess
import sys

import os
PIN = os.environ.get("HULK_NATIVEX_SOURCE_SHA", "bf965e952c98e050753e91d08f32685a9f754b12")
PATCHES = {
  "src/renderer.cpp": [
    (
      "namespace native_x_renderer::renderer {\nnamespace {",
      "REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_expand_msaa_sample_grid, false, \"NativeXRenderer\",\n"
      "                    \"Hulk: host-only 640x360 4x scene scale and full 1280x720 Resolve; no EDRAM\");\n\n"
      "namespace native_x_renderer::renderer {\nnamespace {"
    ),
    (
      "  if (!REXCVAR_GET(native_x_renderer_hulk_gap_single_draw)) return;\n"
      "  TrackedCall synthetic{};",
      "  if (!REXCVAR_GET(native_x_renderer_hulk_gap_single_draw)) return;\n"
      "  // PM4 indirect helpers with no color/depth target are not geometry.\n"
      "  // Keep tracker state for the next draw; don't enqueue a null-target draw.\n"
      "  bool has_surface = false;\n"
      "  for (uint32_t i = 0; i < 5; ++i) {\n"
      "    const GuestSurface s = ReadBoundSurface(base, device, i);\n"
      "    has_surface |= bool(s) && s.width && s.height;\n"
      "  }\n"
      "  if (!has_surface) {\n"
      "    static std::atomic<uint64_t> hulk_no_target_skipped{0};\n"
      "    const uint64_t n = ++hulk_no_target_skipped;\n"
      "    if (n == 1 || n % 10000 == 0)\n"
      "      REXLOG_INFO(\"[HULK/PM4-GUARD] skipped {} unbound indirect draws\", n);\n"
      "    return;\n"
      "  }\n"
      "  TrackedCall synthetic{};"
    ),
    (
      "  g_targets.Initialize(device->device(), g_scale);\n"
      "  g_resolved.Initialize(device->device(), g_scale);",
      "  g_targets.Initialize(device->device(), g_scale);\n"
      "  g_targets.SetHulkSampleGrid(REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid),\n"
      "                              profile.output_width, profile.output_height);\n"
      "  g_resolved.Initialize(device->device(), g_scale);"
    ),
    (
      "  to.width = from.width;\n"
      "  to.height = from.height;\n"
      "  copied = from;",
      "  to.width = from.width;\n"
      "  to.height = from.height;\n"
      "  copied = from;\n"
      "  // The Hulk 4x-MSAA 640x360 scene must cover a full 1280x720 Resolve.\n"
      "  // Do not falsify guest surface width, pitch, registers or MSAA metadata.\n"
      "  const bool hulk_sample_grid = REXCVAR_GET(native_x_renderer_hulk_expand_msaa_sample_grid) &&\n"
      "      surface.msaa() == 2 && surface.width == g_profile->output_width / 2 &&\n"
      "      surface.height == g_profile->output_height / 2 &&\n"
      "      target->guest_width == g_profile->output_width &&\n"
      "      target->guest_height == g_profile->output_height;\n"
      "  if (hulk_sample_grid) {\n"
      "    to.width = std::min(from.width * 2, target->guest_width -\n"
      "        std::min(target->guest_width, uint32_t(std::max(to.x, 0))));\n"
      "    to.height = std::min(from.height * 2, target->guest_height -\n"
      "        std::min(target->guest_height, uint32_t(std::max(to.y, 0))));\n"
      "    static std::atomic<uint32_t> logged_hulk_grid{0};\n"
      "    if (logged_hulk_grid.fetch_add(1, std::memory_order_relaxed) < 8)\n"
      "      REXLOG_INFO(\"[HULK/MSAA-GRID] guest {}x{} {}x MSAA -> {}x{} Resolve;\"\n"
      "                  \" native source {}x{}\", surface.width, surface.height, surface.samples(),\n"
      "                  to.width, to.height, source->width, source->height);\n"
      "  }"
    ),
    (
      "  bool scaled = source->reduced || aliased || source->scale.x != target->scale.x || source->scale.y != target->scale.y;",
      "  bool scaled = source->reduced || aliased || source->scale.x != target->scale.x || source->scale.y != target->scale.y || hulk_sample_grid;"
    ),
  ],
  "src/targets.h": [
    (
      "  void Initialize(Diligent::IRenderDevice* device, const RenderScale& scale);\n"
      "  // The scale of the reduced targets",
      "  void Initialize(Diligent::IRenderDevice* device, const RenderScale& scale);\n"
      "  void SetHulkSampleGrid(bool enabled, uint32_t width, uint32_t height) {\n"
      "    hulk_sample_grid_ = enabled && width >= 2 && height >= 2;\n"
      "    hulk_output_width_ = width;\n"
      "    hulk_output_height_ = height;\n"
      "  }\n"
      "  // The scale of the reduced targets"
    ),
    (
      "  bool depth_unorm24_ = true;  // the device has D24_UNORM_S8_UINT depth targets (D103)",
      "  bool depth_unorm24_ = true;  // the device has D24_UNORM_S8_UINT depth targets (D103)\n"
      "  bool hulk_sample_grid_ = false;\n"
      "  uint32_t hulk_output_width_ = 0, hulk_output_height_ = 0;"
    ),
  ],
  "src/targets.cpp": [
    (
      "  static const RenderScale kNativeScale;\n"
      "  const RenderScale& scale = reduced ? reduced_scale_ : shadow ? shadow_scale_ : native ? kNativeScale : scale_;\n"
      "  desc.Name = name.c_str();",
      "  static const RenderScale kNativeScale;\n"
      "  RenderScale scale = reduced ? reduced_scale_ : shadow ? shadow_scale_ : native ? kNativeScale : scale_;\n"
      "  // Only Hulk's output/2 4x scene render/depth targets: give them the\n"
      "  // same full host pixel extent as the output target, without changing\n"
      "  // the Xbox surface identity, EDRAM address, guest pitch or sample flags.\n"
      "  const bool hulk_grid = hulk_sample_grid_ && !reduced && !shadow && !native &&\n"
      "      s.msaa() == 2 && s.width == hulk_output_width_ / 2 &&\n"
      "      s.height == hulk_output_height_ / 2;\n"
      "  if (hulk_grid) {\n"
      "    scale.x *= 2.0f;\n"
      "    scale.y *= 2.0f;\n"
      "    static std::atomic<uint32_t> logged{0};\n"
      "    if (logged.fetch_add(1, std::memory_order_relaxed) < 8)\n"
      "      REXLOG_INFO(\"[HULK/MSAA-GRID] native {} {}x{} 4x surface at scale {}x{}\",\n"
      "                  s.depth ? \"depth\" : \"color\", s.width, s.height, scale.x, scale.y);\n"
      "  }\n"
      "  desc.Name = name.c_str();"
    ),
  ],
}

# The known-working pinned backend and D127+ have different native-scale
# geometry. Keep both audited paths intact until the new Windows build passes.
CANDIDATE_PIN = "919371b5bdae0c9c4c7120ca321f5a4d72b7e78e"
LEGACY_PIN = "bf965e952c98e050753e91d08f32685a9f754b12"
if PIN == LEGACY_PIN:
    PATCHES["src/renderer.cpp"][-1] = (
        "  bool scaled = source->reduced || aliased;",
        "  bool scaled = source->reduced || aliased || hulk_sample_grid;"
    )
    old, new = PATCHES["src/targets.cpp"][0]
    def legacy_scale(text):
        return (text.replace("  static const RenderScale kNativeScale;\n", "")
                    .replace(" : native ? kNativeScale : scale_", " : scale_")
                    .replace("&& !shadow && !native &&", "&& !shadow &&"))
    PATCHES["src/targets.cpp"][0] = (legacy_scale(old), legacy_scale(new))
elif PIN != CANDIDATE_PIN:
    raise ValueError("Unsupported NativeX revision for Hulk sample grid: " + PIN)


def transform(source, rules):
    finished = [new in source for old, new in rules]
    if all(finished):
        return source, False
    if any(finished):
        raise ValueError("Partially installed Hulk host-only sample-grid patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Unexpected pinned NativeXRenderer anchor")
    for old, new in rules:
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_msaa_grid.py <NativeXRenderer checkout>")
    root = Path(sys.argv[1]).resolve()
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise ValueError("NativeXRenderer revision mismatch " + sha)
    changes = []
    for relative, rules in PATCHES.items():
        file = root / relative
        after, modified = transform(file.read_text(encoding="utf-8"), rules)
        changes.append((file, after, modified))
    if len(set(changed for _, _, changed in changes)) != 1:
        raise ValueError("Mixed sample grid patch states; refusing partial write")
    for file, after, changed in changes:
        if changed:
            file.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk host-only 4x MSAA scene+Resolve:",
          "patched" if changes[0][2] else "already installed")

if __name__ == "__main__":
    main()
