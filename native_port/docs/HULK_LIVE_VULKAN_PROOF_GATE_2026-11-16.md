# Hulk 2008 USA: live Vulkan graphics proof gate (16 Nov 2026)

## Reality check: what is on screen today

The actual `native_port/src/native_x_renderer_hulk.cpp` initialises
`native_x_renderer::Initialize` and the D140 game-specific XDK
hook profile. It does **not** call
`OriginalXboxTextureAssets::LoadHash`,
`ReconstructOriginalLfxtMipPlan`, or
`UploadExplicitOriginalTexture` on production Draw submissions.
Those source-only modules are compiled and safety-tested, but cannot
alter Hulk's existing on-screen textures until an actual EoR
Draw/PSO/SRB backend is wired through them with proven fetch metadata.

The D140 NativeX source pin in `native_port/BUILD_WINDOWS.ps1` is
`fa19ea85fec5c7f7f8a2df82d6c4a86548817b52` (2026-11-11);
the upstream public NativeXRenderer head is the same commit in the
current audit. There is no newer upstream D140 visual fix to merge.

## Observed real guest shader/packet faults

Previously owner-uploaded `incredible_hulk_211.log` (15 Nov 2026,
14:25–14:27 local log timestamps) shows:

- Five logged instances of:
  `Shader translation error: getCompTexLOD requires pixel shader implicit derivatives`.
  Xenos `getCompTexLOD` is pixel-stage-only; the SDK's instruction
  definition specifically says it cannot be assembled as a vertex
  shader. It is unsafe to suppress the error and emit guessed VS LODs.
  Investigate PM4 shader-load stage, original shader program start,
  stream ownership and shader pairing *before* shader translation.
- Source-owned incomplete draw examples include guest scan bytes that
  are clearly not valid PM4 headers: `0xffcccccc`,
  `0x3f800000` (IEEE754 +1.0), `0xff666666`.
  A broader PM4 scan can interpret embedded shader constants/pixel
  values as commands and corrupt subsequent state.
- Invalid vertex draw sampled reports include `slot=95`,
  `type=0`, `words=69376`. These are signs of wrong
  guest fetch/shader/layout, not proof that host BCn decode is wrong.
- Runtime effective settings logged: bilinear output, sharpness
  0.90, AF 4, `mip_probe=false`, scene pipeline wait true.
  A subsequent source-side CAS clamp exists, but is not a verified
  GPU texture/LOD repair.

## Current submitted source-only repairs

- Ten real Xbox USA LFXT image descriptors establish true stream
  link hashes, resident mip dimensions and per-level padded storage sizes.
  `native_port/docs/HULK_USA_LFXT_MIP_EVIDENCE_2026-11-16.md`
  contains exact values and limitations.
- `eor_original_texture_mip_ranges` explicitly checks that a
  prospective `Textures.ARC` / `TexStreams.ARC` layout cannot
  overrun the logical aligned data areas and touch the remaining
  52-byte surplus observed in record sizes.
  **The physical semantics of the surplus are not yet verified from
  end-of-record source bytes**, so this is a conservative candidate
  range model, not proof of a GPU-ready upload offset.
- The actual DiligentCore upload path now additionally requires
  `original_mip_plan.absolute_source_offsets_proven == true`;
  all heuristic/source-only mip plans default to false.

## Conditions to build for meaningful user-visible test

1. Establish the actual 52-byte physical trailer/prefix placement
   in authentic USA `LFXT` and `TexStreams` records; verify Xenos
   format/channel-swizzle and byte-endian from game fetch state.
2. Prove texture hash -> original EoR material -> exact VS/PS
   sampler in a real Xbox USA Draw. Bind GPU SRVs to a functional
   DiligentCore Vulkan PSO and commit them before the game's Draw.
3. Fix live PM4/XDK provenance failures (shader stage and scan bounds).
   Do not replace pixel-only `getCompTexLOD` with fake VS LODs.
4. Compile and run the existing ONE manual-only Windows workflow,
   then check real game images against prior #211 and verify no
   regression in geometry, menu glyphs, mips or resource lifetime.

Neither a source-only test nor a successful compilation is proof of a
visual improvement. The latest Windows CI success currently targets
an older commit `48c3f9e62d5e40e4ddc54141f0fc15d6ed02482d`,
not the HEAD of these corrections. Do not publish a new "fixed
graphics" ZIP without the conditions above.

## Revised by full actual-byte audit

The old "52 surplus still unlocated" claim above has now been resolved:
original **both** LFXT and TexStreams contain structured GPU descriptors
in their FINAL 52 bytes, as verified in 20 full source records. The 4-byte
BE TexStreams prefix plus physical data (full source layouts) is confirmed.
All 5,836 indexed stream-linked USA textures also pass exact mip size and
sqrt-area checks, with 6,669 LFXT records scanned. Formats IDs and physical
byte range evidence: see
`HULK_USA_ALL_TEXTURES_BYTE_AUDIT_2026-11-16.md`.
The **on-screen NativeX renderer still does not call** the new host asset
uploader, so no actual Vulkan game image improvement can yet be claimed.

## 2026-11-17: decouple camera FOV gate from genuine host Draw readiness

The original EoR host frame planner now permits a native GPU frame with the
**source-verified EoR engine Draw hook and original offline shaders** even when
the Xbox USA camera ABI is not yet verified. The scene is physically drawn
at host density inside an original-aspect viewport (e.g. 2560x1440 inside
a 2560x1600 display with 80-pixel top/bottom bars), not upscaled from a
1280x720 guest render target. A real, verified camera hook is still required
to use the entire 16:10 screen without distortion. This does not alter HUD,
projection constants, game draws, or Xenos renderer settings.

**Current runtime remains blocked:** `eor_xdk_hook_bridge.cpp` only observes
actual USA PPC calls, while no validated call-to-model/surface/pass/VS/PS/
constant mapping has been connected to `ReplayVerifiedGeometryScene`.
Consequently the new frame-planning condition cannot activate unsupported
Vulkan Draws or prove visual improvements in the actual game. Existing
single manual Windows Release workflow remains untouched.
