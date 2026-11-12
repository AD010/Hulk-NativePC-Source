# The Incredible Hulk (2008) NativePC — World pop-in / LOD / streaming evidence

Date: 2026-11-12. **Source-only engineering note; no new Windows Release started.**

## Safeguard: graphics baseline

Owner visually confirmed `Hulk NativePC Vulkan #68` (Actions
`43109758266`, source workflow SHA `ee84256001b2`) renders menu and
daytime city without the previous black sky, colored triangles, or missing
geometry in the tested area. This is a **graphics baseline**, not proof that
all missions, streaming paths or crashes are fixed. Maintain the same
NativeXRenderer/DiligentCore/Vulkan path. Do not change Clear/ClearF,
MSAA/depth ownership, or shader replay to mask world LOD popping.

## Evidence: not solely a Vulkan defect

Contemporary Xbox 360 reviews independently describe pedestrians, objects,
textures and city details appearing very close to the player. Thus it would
be wrong to fix the **native PC wrapper only** and claim engine-level distance
culling is solved. Main possible categories:

1. **Game-authored visibility and LOD selection:** near/far thresholds,
   model substitutions, camera-based culling, sector visibility, fog.
   Must find exact USA PPC functions/data and preserve indoor/cinematic and
   mission-specific overrides.
2. **Game resource residency:** which `MODELS.ARC`, `DATASETS.ARC`,
   `QUICKDAT.ARC` and `TEXTURES.ARC` ranges are loaded, when and how
   long they take, and whether nearby cells await asynchronous work.
3. **Native renderer artifacts:** a guest draw exists but is rejected or
   briefly suppressed after GPU residency loss. Compare guest draw counts,
   guest resource creation and Vulkan draws before blaming the game.

Reviews (historical evidence, not instructions for Hulk engine thresholds):
https://arstechnica.com/gaming/2008/06/review-the-incredible-hulk-360/
https://www.gamespot.com/reviews/the-incredible-hulk-review/1900-6192507/

## Implemented source work: persistent bounded stream profiler

`ci/patch_sdk_hulk_world_stream_trace.py` is installed by the **existing**
`native_port/BUILD_WINDOWS.ps1`. The `hulk_world_stream_trace` cvar
defaults to **off**. Previously its first 2,000 reads were logged and all
later traffic was dropped. Now the Hulk-only source patch logs the first 256
reads in each ten-second observation window, then one of every 64 reads up to
16,384 reads per window, with monotonic `t_ms`, measured `read_us`,
`ok`, original `seq`, archive/offset/request/read bytes. It **never
modifies asset data or file-read return codes**, and does not alter the
confirmed #68 runtime until a future owner-requested build.

`ci/map_hulk_world_stream_trace.py` remains backward-compatible with
existing five-field stream events and now reports:
- Per-archive access counts and indexed resource hashes, with unknown names
  clearly marked as unknown (not claimed high/low detail assets).
- `timeline_5s`: counts, bytes, failures, p95 read latency, slowest read.
- `slow_reads`: bounded list of individual events at or above 50 ms.
- Unknown reads per archive to expose index mismatch or unusual I/O.

Example offline usage (requires the owner's legally obtained original
USA index and an opt-in future log):

```
python ci/map_hulk_world_stream_trace.py INDEX.IND incredible_hulk.log --output world_stream_summary.json
```

A **fast** asset read is not proof that the game should draw that asset.
A missing read is not proof of absent high LOD if it is already cached.
No exact per-entity cull threshold has been identified or patched yet.

## Resource-name recovery, not fabricated asset classification

The indexed Xbox `INDEX.IND` entries expose hashed identities, archive
groups and offsets, not reliable original model names. The PC `.ARC`
variant is reported to have optional per-file names; its format differs.
The PC archive can be used **as a semantic comparison source** where layouts
really match, but the file-name mapping must be verified against identical
payloads/hashes before any Xbox resource is relabeled.

Historic research:
https://reshax.com/topic/17543-the-incredible-hulk-2008-quickbms-script-for-indexind-to-extract-arc/
https://reshax.com/topic/238-arc-files-from-the-incredible-hulk-game-2008/

## Safe path to an actual pop-in improvement

- Correlate world streaming with an authoritative guest camera position and
  the game visibility subsystem. Neither camera guest-memory offsets nor
  LOD branch addresses may be guessed from the PC menu or draw distance.
- Locate the exact per-class guest LOD selection/range calculations in the
  USA PPC AOT with multiple independent callsite and state checks. Distinguish
  *render distance* from activation of **physics/NPC AI/traffic simulation**:
  extending the former must not spawn extra active entities or run AI remotely.
- Only after proofs, implement per-class `Original / Extended / Far`
  profiles with conservative memory/CPU budgets and ability to restore
  original values. Preserve mission/scene-specific thresholds and shadows.
- Verify low-altitude movement, rooftop camera rotation and fast Hulk jumps.
  Compare *the same* route, draw submission counts, asset residency, 1%
  lows, GPU VRAM and near pop-in on the working #68 graphics branch.
  Fallback to Original if any geometry, animation, traffic or physics errors
  appear. Avoid suppressing texture mips or deleting far LOD mesh outright.

The Darkness Recomp's optional **shorter** draw distance deliberately
trades visible objects for performance; it is not an automatic improvement
for Hulk. Reference:
https://github.com/portingpete/The-Darkness-Recomp/releases

This milestone is a diagnostic improvement, **not** a claim that pop-in or
far LOD is fixed or that an official-remaster-quality visibility range is
present yet.
