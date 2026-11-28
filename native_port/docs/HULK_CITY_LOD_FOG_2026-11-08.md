# Hulk 2008: high-detail skyline / fog / LOD investigation (2026-11-08)

User screenshot: red Hulk on roof, nearby rooftop detailed, distant
New York rendered as flat low-detail silhouettes hidden by thick blue-gray fog.
Contemporaneous 2008 reviews confirm this is a console engine limitation,
not evidence that modern PC Vulkan resolution alone would fix it.

Validated against genuine user-supplied Xbox360 DATASETS.ARC (582239200 bytes)
and newer USA 534176-byte INDEX.IND. The compressed EoR _app_ dataset
decodes to 320 64KiB blocks, 20941254 decoded bytes. _shader_effects_
decodes to 91 blocks, 5907202 bytes. All below are FIELD NAMES, not
verified live values or guest addresses.

__app__ decoded metadata (byte offsets from decoded stream):
- CameraFarPlane 19693603; CameraNearPlane 19693475
- FogNear 20856802; FogFar 20856795
- FogDensityFunction 20855216; HeightAmbientFalloffStart 20855259
- ZoneStreamManager 19662163; HiResDataset 19662818; ZoneLowLod 19662831
- LODDistSq 19652067 pertains to BruceBreakableData, not city LOD.

_shader_effects_ includes GroundFogGlobal, VolumetricFogBox, DistantWater,
SkyDomeDynamic and SSAO. Those effects are evidence of shaders present in
the Xbox build, not proof of their enabled state or fog parameter defaults.

To genuinely improve rooftop skyline, identify where active LevelData
sets CameraFarPlane and active atmosphere applies FogNear/FogFar; then
find which ZoneStreamManagerLayer references HiResDataset vs ZoneLowLod
and what guest code controls the high-resolution streaming radius.
Removing fog without loading HiResDataset creates empty/polygonal distant
buildings. Raising resolution also does not produce missing geometry.

New optional 'hulk_world_stream_trace' in TOML (default false) instruments
ReXGlue HostPathFile::ReadSync without changing file I/O. It logs the first
2000 guest .ARC/.IND read ranges to associate loaded game resources
with camera movement, using the user's own USA index. No host paths,
copyrighted assets or data payloads are logged. No runtime output changes.

Rebuild tests fail on changed SDK read routine, and compare immutable
return/status semantics. Disable tracing after inspection.


## 12 November 2026 — source-only resource-time correlation

The verified #68 Vulkan renderer stays untouched. The optional ARC/IND trace
now emits bounded events throughout gameplay with `t_ms`, `read_us`, and
an actual success bit. The existing `ci/map_hulk_world_stream_trace.py`
now additionally reports **top resource hashes within each five-second
window** (`timeline_5s[].top_resource_hashes`) and distinct indexed
resources by window, not only a whole-session aggregate. Failed/zero-byte
reads do not count as loaded resources. This permits identifying a late
resource read or repeated streaming of a given zone, but *not* automatically
a live LOD distance, CPU culling decision, or resident GPU object.

The known `CameraFarPlane`, `ZoneStreamManager`, `HiResDataset`,
`ZoneLowLod`, `FogNear/FogFar` fields are data schema names, not verified
live writable addresses. Do not increase far clip or disable fog until the
engine's stream radius and zone residency are correlated to camera movement.
No CI run or user game files are changed by this analyzer.

## 28 November: actual atmosphere render-copy hook

Verified USA mapped PE: FogNear string 82069858 and FogFar 82069860 are registered at 82C0D878/82C0D890 to identifiers 82E77DF4/82E77DFC. 826258DC..82625918 blends these into state offsets +76/+80. 82626140 passes global 82E78040 to the volume blending path. 825E7668 reads global +76/+80 and copies the same float4 into temporary render-state +1712 and +1920 (825E76B8..825E76F4 and 825E7728). Three direct callers use their own stack buffers: 8229A5F8, 82305B7C, 823539E8. The opt-in host wrapper preserves the original function, pins the complete 620-byte function and each caller prefix, validates original source and both copies, then changes only those two temporary pairs. TOML cvar hulk_original_fog_distance_multiplier accepts 1 (original, default), 2, 3 or 4; no accumulated multiplication, no modification of scene atmosphere source, FogYScale, physics, camera clip or streaming. Both writable copies are validated before modification. This is a distance-only experimental rendering change, not a guarantee of removing all blue-gray fog or high-detail skyline: far geometry residency and authored LOD/culling thresholds remain unresolved. GCC and Clang C++20 portable helper tests passed. No Windows compile/game test was performed.

Camera field provenance also verified: original parser 822D5544..822D556C uses CameraNearPlane string 82015B48 and CameraFarPlane 82015B34 and stores parsed floats at object +60/+64. This is a data parser, not proof of a safe live projection override. No camera modification is installed here.

The original EoR PC Settings > Graphics page now exposes Fog distance (experimental), choices 1–4, through the existing persistent option transaction and live cvar setter. The default remains 1. The menu integration regression is added but not yet executed; the earlier portable fog helper tests are separate.
