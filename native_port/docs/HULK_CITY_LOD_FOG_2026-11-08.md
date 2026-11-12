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
