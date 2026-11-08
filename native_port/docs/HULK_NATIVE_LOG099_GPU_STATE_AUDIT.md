# Hulk 099 actual GPU-state divergence and repair attempt

User log99 (14:25:15–14:25:42 Kyiv) proves:
- NativeX DiligentCore uses RTX5070Ti and 2x (2560x1440) GPU targets.
- Present #1 and #600: guest_front=0xe2f3f000, fetch1=0xe2ba7006,
  resolved=true selected=0x2ba8000, exact guest 1280x720 host 2560x1440.
  Therefore no proof of missing front or HWND/swapchain failure.
- frame600: 1397 draws captured/drawn, 28 resolves; 140 shaders,
  94 pipelines, 600 host frames completed. Still BLACK visually.
- GPU observer frame655: 1397 D3D calls, only 362 (25.9%) full state match,
  1035 wrong pixel programs (CPU 20cc53f01b95cee2 vs GPU
  2e372ea28cc404b7), 1035 SQ_INTERPOLATOR_CNTL 0x30007 vs 0x10003.
  43 draw packets unmatched, many emitted inside Resolve and outside
  any hooked call; 192 indirect buffer draw packets.
- All 1397 shader constant maps unavailable, so VS/PS constant parity
  was not tested (0 compared). Do not report as matching.

NativeX upstream StateTracker only scans PM4 inside methods for which
the game supplies XDK hooks; user's profile has 8 hooks and is missing
additional state/shader-setting methods. The new source-pinned state
patch scans bounded monotonically ordered PM4 gaps in the same command
buffer segment, capturing IM_LOAD and register writes between hooks.
No hardcoded pixel shader/register values; fail closed for invalid or
large gaps. It is an evidence-based native state repair EXPERIMENT
until a new GPU observer shows dramatically improved match ratio.
If > 100% mismatch remains the next task is mapping/adding actual
US XDK call hooks; do not infer methods from nearby offsets.

Correct existing compiled guest US PPC mappings:
DrawVertices 0x8288DF30, DrawIndexedVertices 0x8288E318,
Resolve 0x828897F8, RT 0x8287F2C0, depth 0x8287F628,
BeginTiling 0x8288A900, EndTiling 0x8288AD98,
logical Present inner Swap 0x82880778 (outer 0x82880D10 is wrong).
These are VIRTUAL guest addresses, not file offsets.

The user requested NativeX enabled by default in the new test build.
Black remains possible: native_x_renderer=false restores Xenos FSI.
F11 DID capture actual GPU observer frames 221..655 in log099, but has no visual UI.
