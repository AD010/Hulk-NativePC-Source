# Hulk GPU log153: failure localization and D3D11 bridge gate

Source evidence: user screenshots of the main menu and playable city plus `incredible_hulk_153.log` on 2026-11-09. These show **Vulkan present functioning** (2560×1440 composited into a 2560×1600 swap chain with 80px vertical borders), while render pass content and textures remain invalid.

Evidence (approximately 77 seconds of runtime):
- `[HULK/PM4-GUARD] skipped 210000 unbound indirect draws` at the last logged threshold.
- `[HULK/PM4-SURFACE] skipped ambiguous ... total=100000` and fallback `recovered ... total=100000`. Fallback means *best-effort reuse* of a late XDK object; it does not demonstrate packet-time GPU identity.
- 4× guest 640×360 color/depth views share host 2560×1440 target with a 1280×720 guest view, but `[HULK/SCENE-RESOLVE]` additionally documents intermediate 1280×720 host targets. Correct final Present sizing does not validate intermediate scene passes.
- Repeated invalid 1D texture dimensions above 4 million, unsupported primitive types 0/48/49, and zero-index DMA reads establish invalid or unsupported GPU state while replaying the scene.
- The renderer's frame 1800 log says about 1552 draw calls, ~1502 submitted, 49 without targets, 28 resolves. Many submitted Draw do not establish correct pixels.
- The new screenshot shows geometry drawn across more than a single left quarter, yet large green cut-outs remain; stretching Present or widening output scissor cannot restore missing passes.
- `[HULK/PM4-REGISTER-ONLY]` does not occur in this log; it appears to be from the #27 known-good release, **not** proof that #31's PM4-first patch executed.
- A failed UKENGLISH/INDEX.IND file lookup is a **separate** localization/resource issue; SDL joystick-disabled warning is unrelated to GPU pixels.

## Engineering decision

Do not describe the rendering as fixed until full frame GPU verification. Do not add more guessed RT/Resolve/scissor compensation from screenshot coordinates. The current NativeXRenderer/PM4 route remains the *reference implementation* only.

For D3D11 (FL11_0) modeled on the architecture of The Darkness, find authenticated Edge of Reality functions **above** PM4 and XDK Draw/Resolve/Present. The source-only `ci/hulk_engine_render_callgraph.py` runs against the validated USA XEX's ReXGlue-generated PPC comments and exports a graph of direct call ancestors to the **PRIVATE** build area. This is a discovery tool only: it cannot infer virtual calls, material layouts, guest ABI, or draw ordering by itself. Candidate functions must then be verified with read-only runtime tracing, vertex/index buffer + material extraction, and RenderDoc Pixel History.

Do not transplant The Darkness's GPL rendering/runtime wholesale into Hulk's MIT repository or pretend that changing the DirectX API solves missing guest render passes.
