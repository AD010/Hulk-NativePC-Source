# Original Hulk USA TexStreams → Vulkan: verified state (16 Nov 2026)

This is a status/provenance ledger for ongoing engineering, **not** a game
playback validation and **not** proof of corrected texture rendering.

## Original owner inputs already provided — DO NOT ask for them again

- Original TEXSTREA.7z.001 (471,859,200 bytes) and TEXSTREA.7z.002
  (252,506,863 bytes) uploaded 14 Nov 2026 and materialized privately.
- USA global index (owner file `index(2).ind`) SHA-256
  `ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d`.
- Older Xbox/global index (owner `indexXBOX360.ind`) SHA-256
  `46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820`.
- Pinned USA `default.xex` SHA-256
  `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`
  (stored privately in `AD010/hulk-cloud` Release).
- A prior index comparison found the 4,250 differing records between Xbox
  indexes in **QuickDatas, Animations and Datasets**; `Textures` and
  `TexStreams` group metadata were not among these differences. This
  comparison does **not** prove the full original archive bytes are identical.

## What was actually implemented

- `ci/audit_hulk_texstreams_split.py` — read-only original multipart 7z
  extraction/integrity, strict USA index SHA, LFXT→TexStreams provenance,
  byte-range checks and sanitized structural statistics.
- `src/eor_xenos_texture_mips.{h,cpp}` — native CPU BC1/BC2/BC3/RGBA8
  untile, explicit 16/32-bit byte ordering, linear layouts, packed-tail block
  origins and bounded contiguous mip-chain reconstruction. All format,
  dimension, layout and endian arguments must be supplied from separately
  verified original source; no defaults silently guess them.
- `src/eor_diligent_xenos_textures.{h,cpp}` — DiligentCore immutable
  GPU texture upload from those reconstructed mip levels, with explicit
  linear vs sRGB and valid original USA index provenance.
- `src/eor_diligent_scene_targets.{h,cpp}` — host physical color/depth
  attachments and exact-size MSAA Resolve; an invalid/rejected frame can no
  longer reuse an old prepared color/depth target.
- Single existing manual-only Release workflow receives the source-only
  Xenos mip regression; no new workflow, fork, private binaries or forced
  intermediate game build.

Independent arithmetic cross-check, pure JavaScript in the development
session: 86,016 Xenos tiled block addresses across 32/64/128-block pitches,
128 rows and 4/8/16 bytes per block matched a second independently authored
formula. This is **not** a C++ compiler or gameplay verification.

## Explicit blockers (no false completion)

1. The private `TEXSTREA.7z` multipart **has not been successfully
   decompressed/analyzed** in the current session: container/Python execution
   returned `ClientError` even for trivial commands. The two uploaded parts
   EXIST. **Do not request reupload**. Re-run the strict USA audit when file
   execution becomes available.
2. The exact original `LFXT` GPU texture format, dimensions, endian flags,
   source offsets and packed mip-tail placements are not decoded from genuine
   archive payloads. The generic CPU converter does not discover them.
3. Host textures have not been attached to a verified live game's EoR
   material sampler registers, original shader-pair PSOs and actual USA Draw.
   Direct EoR engine Draw dispatch is still not connected in game.
4. Current HEAD has **no post-change successful Windows CI run**, no Vulkan
   gameplay evidence and no proof colored flicker, pop-in, signs or geometry
   are fixed. No new user-facing game ZIP should be advertised as ready.

Next work is source-faithful private binary decode and cross-check, then
a concrete runtime sample/Draw/PSO ownership bridge and a compiled Windows
runtime test. Only a release with observable changes should be offered to
the owner for 60 FPS screen-recording analysis.
