# 2026-11-08: log 087 zero-frame NativeX regression

User black screen on build 52dbff545858, 11:41:56.420 to 11:42:36.989 Kyiv.
- XDK 6534 native GPU active, 9 compiled hook sites.
- GPU-PROBE sees inner Swap 0x82880778: 77 hits in first interval
  (r3=0x40002880, r4=0xe2f3f000, r5=0), 345 next interval.
- Outer 0x82880D10 absent FROM TOP-16 PROBE SUMMARY, not proof of
  zero calls because GPU-PROBE truncates reporting to hottest 16.
- ZERO completed renderer frames; ZERO front-buffer resolved telemetry.
- At 11:42:17.286 repeated DiligentCore "Space in dynamic heap is
  exhausted!" (256 MiB). Last 1-second probe was 160818
  DrawIndexedVertices. Log ends 11:42:36.989 without a clean shutdown;
  a dump would be needed to identify exact process termination cause.
- Earlier log 079 with inner Swap mapped as logical Present DID finish
  31800 GPU frames, but image remained black. This fix must NOT be
  described as repairing black output; it only restores recycling.

Correction: assign 0x82880778=Present and remove unobserved outer frame
hook. Restore upstream Present.r4 reading the real inner Swap front
texture. Preserve existing heap-null safety guards from 077. Instrument
FindNewest guest front texture lookup in ExecutePresent for every 600th
frame to distinguish missing Resolve from a black but valid texture.
No guest XEX/data, Raw Input, mouse, GPU format, physics or window edits.
