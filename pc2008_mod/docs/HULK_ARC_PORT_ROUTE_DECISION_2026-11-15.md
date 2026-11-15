# The Incredible Hulk 2008 — original PC vs Xbox 360 asset interoperability (2026-11-15)

Source: twelve owner-provided original ARC files and the real PC/Xbox indices
`indexPC.ind` (little-endian) and `indexXBOX360.ind` (big-endian).
This metadata audit publishes NO original game assets.

## Proven archive mapping from exact index end-offset

| Group | PC archive | Xbox 360 archive | Shared record IDs | Exactly identical records |
| --- | --- | --- | ---: | ---: |
| Models | MODELS(1).ARC (34,301,152 bytes) | MODELS.ARC (157,919,776 bytes) | 1,197 | 0 |
| QDMetadatas | QDMETADA(3).ARC (325,472) | QDMETADA(2).ARC (336,864) | 592 | 0 |
| Sounds | SOUNDS(1).ARC (17,850,016) | SOUNDS.ARC (19,292,000) | 1,433 | 0 |
| WaveBanks | WAVEBANK(1).ARC (91,481,472) | WAVEBANK.ARC (69,570,528) | 333 | 0 |

IMPORTANT: larger-archive-means-Xbox is FALSE for WaveBanks: original
PC wavebank archive is larger than the Xbox one. Every verified pair
has a little-endian initial archive end pointer equal to the maximum
record end in the corresponding original platform index.

`ANIMATIO(1).ARC` (20,652,640 bytes) exactly matches Xbox animation
index end (20,539,910), whereas `ANIMATIO.ARC` has header end
20,539,914 (+4 Xbox; +9,579,796 PC). Neither archive matches the
provided PC animation index end (10,960,118), so **no verified PC
animation archive** was supplied in this pair.

`QUICKDAT(3).ARC` (3,413,216 bytes) exactly matches Xbox QuickDatas
index end (3,256,448). `QUICKDAT(2).ARC` instead has an end pointer
3,256,248 (-200 Xbox, +720,172 PC). Neither matches the given PC index
(2,536,076). Treat the second file as an unmatched version/region,
NOT an established PC conversion source.

## Index-only group delta (shared identifiers do not prove byte compatibility)

| Group | PC records | Xbox records | Shared hashes | Xbox-only |
| --- | ---: | ---: | ---: | ---: |
| Models | 1,823 | 4,174 | 1,197 | 2,977 |
| Animations | 1,994 | 2,015 | 1,991 | 24 |
| QDMetadatas | 594 | 614 | 592 | 22 |
| QuickDatas | 2,338 | 3,364 | 2,245 | 1,119 |
| Sounds | 1,439 | 1,507 | 1,433 | 74 |
| WaveBanks | 385 | 387 | 333 | 54 |
| Textures | 1,365 | 6,669 | 729 | 5,940 |
| TexStreams | 0 | 5,836 | 0 | 5,836 |
| ShaderEffects | 36 | 233 | 7 | 226 |
| Havoks | 465 | 1,582 | 321 | 1,261 |
| Objects | 2,444 | 3,938 | 1,866 | 2,072 |

ALL 1,197 original shared model IDs have the SAME readable model name,
but NONE has identical binary contents. Each model begins with a
platform-dependent 16-bit endian tag (PC bytes 01 00; Xbox bytes
00 01). Of these shared models, 63 have equal sizes, 487 Xbox records
are longer and 647 PC records are longer. This proves that switching
resource hash tables or archive files does NOT convert original models.
For QDMetadatas, 589/592 shared entries have equal lengths, but all
binary content differs (including the original endian fields).
For WaveBanks, 313/333 shared entries have equal lengths and none is
byte-identical: audio and streaming data must be handled separately.

## Engineering decision for user's requested console-quality remaster

For full Xbox-level effects/art, keep the **Xbox 360 recomp + independent
native EoR renderer** as the primary effort, because rewriting the
PC 2008 resource loader for 2,977 Xbox-only models, 5,940 console-only
textures, 5,836 TexStreams and 226 shader effects is another full
renderer/material/streaming redevelopment, NOT a shortcut.

For near-term stable and playable native graphics enhancements, the
official PC 2008 x86 Direct3D9 engine + optional ASI plugin is the
more practical secondary route: existing PC menus, physics, save
system and working native scene renderer can be retained. ASI/DXVK
D3D9 calls alone cannot automatically add the console's shaders,
textures, Havok data or DX12/Vulkan Frame Generation.

Neither path is technically guaranteed to produce a fully faithful
console remaster; do not claim visual parity without actual game scene
capture and correctness testing. Do NOT merge unvalidated original
assets into either PC or Xbox index. Never publish any proprietary
game bytes or create additional GitHub workflows/repositories.
