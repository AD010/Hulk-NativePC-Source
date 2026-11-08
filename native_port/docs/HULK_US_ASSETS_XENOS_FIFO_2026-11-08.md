# Hulk USA data-root / Vulkan FIFO investigation, 2026-11-08

Source analysis:
- Windows CI SHA-pins USA XEX to
  346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0.
- Previously assets_locator.cpp validated only the existence of default.xex,
  so the USA executable could select PAL resources inadvertently.
- New Windows data picker / configured path discovery hashes the actual
  default.xex via BCrypt SHA-256. A nonmatching XEX is refused. This
  verifies the XEX, NOT every nearby IND/ARC; US resource authenticity
  needs separate examination. Never rename UKENGLISH without proof.
- USA dashboard country=103, English user_language=1 explicit.
- Log 088 fallback Xenos, 11:47:03–11:49:02 Kyiv: draw-candidate
  0x8288E318 called 255518 times in a one-second interval at
  11:48:51, then 1817 at 11:48:52. From 11:48:53 the only
  GPU-PROBE top function is 0x8280F5C0 around 188 per second.
  This is evidence of stalled draw submissions, not a proven cause.
- Presenter created Vulkan present mode 0 (IMMEDIATE_KHR). Upstream
  rexglue-sdk/recomp src/ui/vulkan/vulkan_presenter.cpp defaults to
  allowing IMMEDIATE before MAILBOX/FIFO. Set all three non-FIFO
  modes false, preserve native resolution and physics. This is a
  testable frame pacing correction, not a guaranteed hang fix.
- UKENGLISH is also a resource directory name in the PC Hulk build.
  It is not proof by itself that compiled Xbox 360 PPC is PAL.
- No change to Raw Input, F4 cursor, guest shader translation,
  TU1/Europe XEXP, 30fps physics, Xenos FSI implementation.
- Must Windows compile/test before calling either defect fixed.

Additional verified fix: rexglue-sdk/recomp src/graphics/flags.cpp defines gpu_guest_fps_limit=0 (unlimited). src/graphics/command_processor.cpp ExecutePacketType3_SWAP has production implementation that sleeps after each guest swap when gpu_guest_fps_limit>0, throttling the guest thread via GPU ring-buffer backpressure. Set gpu_guest_fps_limit=30 in user TOML without shader/physics changes. This is a real limiter (not only an FPS counter), but its effectiveness on the user's hanging city gameplay is not yet playtested.
