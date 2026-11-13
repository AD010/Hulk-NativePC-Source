#!/usr/bin/env python3
"""Hulk: restrict scene AA to a real native 4x-MSAA scene resolve, not the HUD.

The Hulk XDK profile does not yet have an observed scene_resolve_caller.
Instead of enabling AA on the final front-buffer (which includes the UI),
enable native SMAA/FXAA only after a *directly identified* resolve from the
640x360/4x main scene into the full 1280x720 output. If no such resolve
happens, AA is not applied. The trace discloses that explicitly.

No Xenos or EDRAM renderer is activated, and the guest scene is not mutated.
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
   'REXCVAR_DEFINE_BOOL(native_x_renderer_hulk_auto_scene_aa, false, "NativeXRenderer",\n'
   '                    "Hulk: apply AA only on direct 640x360/4x -> 1280x720 scene resolves before the HUD");\n'
   'namespace native_x_renderer::renderer {\nnamespace {'
  ),
  (
   "    if (mode != Antialiasing::Mode::kOff && !profile.scene_resolve_caller) {",
   "    if (mode != Antialiasing::Mode::kOff && !profile.scene_resolve_caller &&\n"
   "        !REXCVAR_GET(native_x_renderer_hulk_auto_scene_aa)) {"
  ),
  (
   "  if (g_aa.mode() != Antialiasing::Mode::kOff && c.scene_caller && slice == 0 &&\n"
   "      target->guest_width == g_profile->output_width &&\n"
   "      target->guest_height == g_profile->output_height &&\n"
   "      !(g_profile->scene_resolve_once && g_scene_antialiased)) {",
   "  // The game has no reliable scene_resolve_caller yet. This is a strict\n"
   "  // content-bound auto candidate, not a fallback at Present after the HUD.\n"
   "  const bool hulk_scene_resolve =\n"
   "      REXCVAR_GET(native_x_renderer_hulk_auto_scene_aa) &&\n"
   "      // HULK/SCENE-RESOLVE-CALLER-6534: only the observed 4x-scene\n"
   "      // LR=0x8263BF50 may receive AA, never other 640x360 surfaces.\n"
   "      c.scene_caller && surface &&\n"
   "      !surface.depth && surface.msaa() == 2 &&\n"
   "      surface.width * 2 == g_profile->output_width &&\n"
   "      surface.height * 2 == g_profile->output_height &&\n"
   "      target->guest_width == g_profile->output_width &&\n"
   "      target->guest_height == g_profile->output_height &&\n"
   "      slice == 0 && level == 0 && !g_scene_antialiased;\n"
   "  if (hulk_scene_resolve) {\n"
   "    static std::atomic<uint32_t> hulk_aa_resolves{0};\n"
   "    const uint32_t count = hulk_aa_resolves.fetch_add(1, std::memory_order_relaxed) + 1;\n"
   "    if (count <= 8 || count % 1000 == 0)\n"
   "      REXLOG_INFO(\"[HULK/SCENE-AA] native pre-HUD 4x MSAA resolve #{}\"\n"
   "                  \" source={}x{} dst={}x{} caller={:#x} AA={}\", count,\n"
   "                  surface.width, surface.height, target->guest_width,\n"
   "                  target->guest_height, c.caller, REXCVAR_GET(native_x_renderer_antialiasing));\n"
   "  }\n"
   "  if (g_aa.mode() != Antialiasing::Mode::kOff && (c.scene_caller || hulk_scene_resolve) && slice == 0 &&\n"
   "      target->guest_width == g_profile->output_width &&\n"
   "      target->guest_height == g_profile->output_height &&\n"
   "      !(g_profile->scene_resolve_once && g_scene_antialiased)) {"
  ),
  (
   "    g_aa.Apply(g_device->context(), target->texture, target_view);\n"
   "    g_scene_antialiased = true;",
   "    g_aa.Apply(g_device->context(), target->texture, target_view);\n"
   "    g_scene_antialiased = true;"
  ),
 ],
}
# Remove intentional no-op marker; the source is already exactly this way.
PATCHES["src/renderer.cpp"].pop()

# HULK/SCENE-AA-HALF-RESOLVE: Log195 shows 640x360/4x -> 640x360
# resolves whose source AND destination already contain 2560x1440 host
# pixels. The earlier detection erroneously required a 1280x720 guest
# destination, so SMAA/FXAA was never applied (0 times per 600 frames).
# Both forms are accepted ONLY for a full native-size, color-only,
# 4xMSAA scene resolve, never for the HUD, depth or reduced targets.
_old, _new = PATCHES["src/renderer.cpp"][2]
_candidate_old = """      target->guest_width == g_profile->output_width &&
      target->guest_height == g_profile->output_height &&
      slice == 0 && level == 0 && !g_scene_antialiased;"""
_candidate_new = """      // HULK/SCENE-AA-HALF-RESOLVE: accept guest output-size OR half-size
      // guest resolve only when its host pixels cover the full native view.
      source && source->width == target->width &&
      source->height == target->height &&
      target->width == g_scale.HostWidth(g_profile->output_width) &&
      target->height == g_scale.HostHeight(g_profile->output_height) &&
      ((target->guest_width == g_profile->output_width &&
        target->guest_height == g_profile->output_height) ||
       (target->guest_width * 2 == g_profile->output_width &&
        target->guest_height * 2 == g_profile->output_height)) &&
      slice == 0 && level == 0 && !g_scene_antialiased;"""
if _new.count(_candidate_old) != 1:
    raise ValueError("Hulk scene AA half-resolve condition drift")
_new = _new.replace(_candidate_old, _candidate_new, 1)
_gate_old = """      target->guest_width == g_profile->output_width &&
      target->guest_height == g_profile->output_height &&
      !(g_profile->scene_resolve_once && g_scene_antialiased)) {"""
_gate_new = """      ((c.scene_caller &&
        target->guest_width == g_profile->output_width &&
        target->guest_height == g_profile->output_height) ||
       hulk_scene_resolve) &&
      !(g_profile->scene_resolve_once && g_scene_antialiased)) {"""
if _new.count(_gate_old) != 1:
    raise ValueError("Hulk scene AA outer gate drift")
_new = _new.replace(_gate_old, _gate_new, 1)
PATCHES["src/renderer.cpp"][2] = (_old, _new)


def transform(source, rules):
    present = [new in source for old, new in rules]
    if all(present):
        return source, False
    if any(present):
        raise ValueError("Partially installed Hulk native scene AA patch")
    for old, new in rules:
        if source.count(old) != 1:
            raise ValueError("Pinned NativeX scene AA anchor mismatch")
        source = source.replace(old, new)
    return source, True

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_native_x_hulk_scene_aa.py <NativeX checkout>")
    root = Path(sys.argv[1]).resolve()
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise ValueError("Unexpected NativeX commit " + revision)
    file = root / "src/renderer.cpp"
    after, changed = transform(file.read_text(encoding="utf-8"), PATCHES["src/renderer.cpp"])
    if changed:
        file.write_text(after, encoding="utf-8", newline="\n")
    print("Hulk scene-only NativeX antialiasing:", "installed" if changed else "already installed")

if __name__ == "__main__":
    main()
