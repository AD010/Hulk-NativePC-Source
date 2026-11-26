# NativePC reference decisions — 2026-11-25

This is a short, persistent implementation reference, not a new extraction,
an audit backlog, or evidence that any external fix already works in Hulk.
Use the existing source, private banks, and current main; implement the
remaining runtime paths rather than expanding observational infrastructure.

## External evidence and immediately applicable decisions

- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp#asynchronous-shader-compilation):
  prepares actual graphics pipelines as part of asset streaming, with boot-time
  preparation for UI/post effects that are not associated with streamed assets.
  Hulk: use existing HEORMAT2/HEORSPV1 and original model/material relations to
  prepare exact host PSOs before publishing a resident model. Key by full
  shader pair, vertex declaration, fixed render state, target formats and
  samples; retain current source-verified assets while replacements stream.
  A module bank alone is not a PSO bank. Never skip a persistent-output pass
  just because its pipeline is pending.
- [Dante's Inferno native renderer design](https://github.com/florinp93/hells-gate-recomp/blob/main/docs/NATIVE_RENDERER_DESIGN.md):
  practical DiligentCore Vulkan example. Their documented issues include
  draw-time patched vertex shader variants, retained literal constants,
  Resolve identity with changing dimensions, and two-byte-offset 16-bit index
  streams incorrectly aligned down to four bytes. Hulk: compare these specific
  mechanisms against existing live shader, vertex/index and Resolve code;
  do not transplant Dante addresses or assume its formats match EoR. Native
  resource creation/ownership belongs to the engine and host API, not a PM4
  packet player. Their document explicitly leaves CP removal as a separate
  milestone; do not confuse native GPU rendering with a completed runtime.
- [ITC-Recomp](https://github.com/Spooky098529/ITC-Recomp/blob/main/docs/rexglue-removal.md):
  public migration notes describe replacing heap, collision, audio and graphics
  adapters by subsystem. The repository says main's document is behind its
  dedicated migration branch; latest branch document was not retrieved in
  this review. Hulk: finish live host ownership and remove obsolete imports
  subsystem by subsystem; no new wholesale SDK dependency.
- [ReGPU](https://github.com/zer0ducksgiven/ReGPU) and
  [Reddit release discussion](https://www.reddit.com/r/decomps/comments/1xsktha/regpuglue2gpu_native_dx12_frameworks_for_llm/):
  useful shader/format reference, not the target architecture. Its README
  explicitly describes ring packets, Vd imports and EDRAM emulation.
  Do not integrate as the final independent renderer. User-reported FPS
  is anecdotal, not a benchmark for Hulk.
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp):
  offline HLSL to DXC SPIR-V/DXIL is a valid build-time technique, distinct
  from runtime hardware emulation. Reuse the existing private shader bank;
  only regenerate affected outputs if an actual compiler fix requires it.
- [Xenia Edge commits](https://github.com/has207/xenia-edge/commits/edge/):
  reference behavior only, no emulator dependency in final target.
  5317a83 caches present pipelines by swapchain format; f9431eb protects
  nonrepeatable draws from async PSO skips; a671574 orders Resolve barriers
  before copy; 4fb8372 handles centroid interpolation at MSAA edges;
  9a37a36, a5627ba, c47487a, 0558d62 correct clipping/culling/position
  arithmetic. Check equivalent native EoR behavior before adapting logic.
  None is established as the root cause of a particular Hulk artifact.
- [Khronos synchronization discussion](https://community.khronos.org/t/artifacts-when-resolving-msaa-depth-buffer-and-reading-in-later-render-pass/109558):
  preserve resource usage/layout and producer-consumer ordering across
  color/depth/Resolve sampling; use Diligent transitions correctly, avoid
  global waits when a precise dependency suffices.
- [Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/):
  separate fixed simulation and presentation; keep fractional remainder
  and interpolate presentation, do not vary physics dt with FPS. Hulk
  already has a source-verified original 60 Hz scheduler and catch-up
  path; complete real runtime integration and verify destruction, collisions,
  animation and input timing at multiple rates, including stalls. Do not
  double-step the world's existing scheduler or confuse catch-up with a
  native Havok replacement.
- [ResHax](https://reshax.com/topic/183-ultimate-tutorial-for-reverse-engineering-a-game-model-wip/)
  and [texture-format reference](https://reshax.com/topic/18588-reverse-enigneering-textures-using-raw-texture-cooker/):
  consult only for specifically unresolved layout/format questions; the
  existing Hulk archives and extracted catalogs must not be re-extracted.

## 2026-11-26 — Xbox360XEXRecomp reviewed source changes

The TGP482 SDK is a separate D3D12 runtime, not a ReXGlue fork. Adapt
only proven semantics into the existing EoR/Vulkan implementation.
Status below distinguishes source integration from gameplay acceptance.

- [e14876f3382ec77d91380188df37338d5f5e10ea](https://github.com/TGP482/Xbox360XEXRecomp/commit/e14876f3382ec77d91380188df37338d5f5e10ea)
  (2026-11-26 00:10:26 UTC): half-open scaled Resolve rectangles use
  pixel-center rounding, and the shader adjusts source origin/span for
  destination rounding and unequal source/destination scale. Hulk's
  [1b6521b128048f741e80d58a81c565a2c35e7148](https://github.com/AD010/Hulk-NativePC-Source/commit/1b6521b128048f741e80d58a81c565a2c35e7148)
  already implements exact same-grid fractional copies in
  eor_original_resolve_region.h. Preserve that fast path. For a verified
  differing-phase or differing-scale original Resolve, implement a separate
  shader pass with destination pixel-center mapping and source-coordinate
  compensation; preserve untouched destination pixels, samples/format and
  transitions. Do not loosen CopyTexture's exact-grid precondition.
- [baf71ae433fde6a9f54f9e1598d5d09634be98b2](https://github.com/TGP482/Xbox360XEXRecomp/commit/baf71ae433fde6a9f54f9e1598d5d09634be98b2)
  (2026-11-25 22:15:40 UTC): whole-pixel-center mode uses half a HOST
  pixel of position shift, with remaining interpolation compensation
  in pixel shader inputs. This is not merely a viewport/scissor reset.
  Hulk must first verify original PA_SU_VTX_CNTL and the affected shader
  semantics; account for separate X/Y scales at 16:10. Do not apply a
  global half-pixel offset or double-correct already translated shaders.
- [639bfc4f420c304f0701b9f0c7b20b8e9e15bca4](https://github.com/TGP482/Xbox360XEXRecomp/commit/639bfc4f420c304f0701b9f0c7b20b8e9e15bca4)
  (2026-11-25 21:59:08 UTC): despite its upload-ring commit title, the
  retrieved diff shows border colors, capture-induced command rebinding
  and raw float Resolve readback, not an upload allocator change. Hulk
  [4c9d119828c65690384d1e4a41a6f04580d05129](https://github.com/AD010/Hulk-NativePC-Source/commit/4c9d119828c65690384d1e4a41a6f04580d05129)
  integrates standard Vulkan black/white border modes in material and
  Resolve samplers; YCbCr and cube border cases remain unsupported.
  Windows compilation and actual image acceptance are still required.
  Do not claim upload-ring integration on the basis of the title.
- [c8e8d35b80ce78b4dfdc5c4f335e778bb720697e](https://github.com/TGP482/Xbox360XEXRecomp/commit/c8e8d35b80ce78b4dfdc5c4f335e778bb720697e)
  (2026-11-25 23:46:12 UTC): adds an optional PCF compare hook after
  flagged normalized 2D fetches and getWeights, not a universal shadow
  fix. For Hulk, identify exact original shader pairs and validate
  fetch/compare dataflow and depth format before changing SPIR-V; do not
  substitute hardware PCF globally. Preserve comparison direction,
  weights, addressing, precision and dynamic control flow. No affected
  Hulk shader identity or GPU result has been verified in this review.

## Owner's durable acceptance criteria

Independent Windows x64 native EoR Vulkan renderer, real host 2560x1600
16:10 and HDR, no colored flashes or upscale pixelation, improved LOD
and distance, original interactive EoR PC options replacing ImGui,
native mouse/keyboard, independent stable 60+ FPS and Havok with
destruction/collisions, native host audio (PC-compatible FMOD using
legitimately available assets and libraries), and native PC-style saves
in Documents/SEGA/The Incredible Hulk. Preserve PC save field semantics
and structure, allow a larger versioned file if needed for complete
state; no Xbox containers, XUID, emulated profiles or emulator state.
Physical controller support is a later enhancement, not a prerequisite
to these priorities. Final application must not depend on Xenia, ReXGlue,
NativeX, virtual Xbox controller or emulated Xenos/EDRAM/PM4 runtime.
Preserve license notices, never publish proprietary game resources.

Work from current main, preserve all newer commits after older CI starts.
No reset, repeat extraction, speculative CI series or diagnostics in place
of implementation. Tests are necessary for correctness, not a substitute
for a playable independent frame, complete native menus and verified
final Release ZIP. Build on owner command under the one manual workflow.
