# Hulk PC 2008 + DXVK: original HUD protection & Xbox index incompatibility
Date: 2026-11-16. No official game executable, DXVK DLL or original ARC is published.

## User's real screenshots
- 2048x1280 16:10 visible 3D.
- Original menu lower black rectangle ends near 1707 px, matching a
  4:3 layout (1280*4/3). Pause/HUD layout does not expand with new frame.
- Our previous ASI changed the D3D9 backbuffer and 3D ViewProj without
  a separate HUD orthographic/layout remap. This is insufficient.
- The original D3DPRESENT_PARAMETERS::Windowed flag *is* DWORD[8] in
  x86 D3D9. It was not a misindexed field; verified against Microsoft.
- Original 4:3 UI can be preserved by leaving backbuffer and projection
  untouched. This is a safe rollback, NOT true Hor+ 16:10 HUD support.

## HUD-safe x86 ASI
- New freestanding C build HulkPC.asi checks
  ProtectOriginalHUD=1 **by default**, setting force_width, force_height
  and fix_aspect to 0 even if an older HulkPC.ini enables these.
- Validated as PE32 i386, links to KERNEL32, not against DXVK or VC CRT.
- The installation tool in the separate owner-private ZIP backs up old
  known ASIs and settings, leaves d3d9.dll / dxvk.conf / game assets
  and original HULK.EXE untouched.
- C++/MSVC alternative source pc2008_mod/src/HulkPC.cpp has matching
  protection. No 16:10 original UI layout rewrite is claimed.
- Live gameplay of the new ASI has NOT been executed in this environment.

## Real 26-category original indices (actual owner input)
- PC original indexPC.ind: 241,488 bytes, little-endian.
- Xbox original indexXBOX360.ind: 534,176 bytes, big-endian.
- Both have the same 26 group names, differing entry counts and data.
- The index header, sorted resource hashes, 12-byte record triples,
  category offsets, and footer can be reversed byte-for-byte with a
  strict endian-only translation. ci? No: converter lives at
  pc2008_mod/tools/eor_ind_endian_bridge.py.
- Translation does NOT convert internal Xbox ARC payload endian,
  video/audio headers, compressed GPU textures, TexStreams or
  ShaderEffects. Even equal resource hash means name identity only.
- Six actual archive pairs checked: Models (1197 common), Animations
  (1991), QuickDatas (2245), QDMetadatas (592), Sounds (1433),
  WaveBanks (333): 7,791 matching IDs but **zero identical records**.
- 5,836 Xbox TexStreams exist; original PC has zero.
- Xbox PC-endian index **MUST NOT** replace the game's index.ind.
  Use PC index/ARC pairing for playable installation. A compatible hybrid
  requires actual per-resource conversion + changes in PC loader/renderer
  and live Windows game test, not just endian conversion.

## Current decision
Continue independent Xbox EoR native renderer with source-level mesh and
shader conversion as primary console-quality port. For the original
PC engine, use the reversible DXVK/HUD-safe ASI as a separate working
baseline, not an unverified all-Xbox ARC injection.
