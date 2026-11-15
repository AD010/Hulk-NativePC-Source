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


## Addendum: original PC assets recovered from the owner's clean licensed install (2026-11-15)

The original owner provided seven more archives. Checked against the
**actual PC index** SHA-256
`3309ac7650c53c7bf64f797f962f6c5961c33774718347a2a0999fc04b04a33a`.
For every archive the initial little-endian dword equals the highest
indexed resource end, and **all** PC indexed record slices fit the file.
This supersedes the earlier paragraph saying that the original PC animation
and QuickDatas archives had not been supplied.

| Incoming filename | Original PC filename | PC resource count | PC record-end match | Xbox record-end match |
| --- | --- | ---: | --- | --- |
| ANIMATIO(2).ARC | ANIMATIO.ARC | 1,994 | yes | no |
| BINARIES.ARC | BINARIES.ARC | 1 | yes | **yes (ambiguous)** |
| FMODPROJ(3).ARC | FMODPROJ.ARC | 371 | yes | no |
| SKELETON.ARC | SKELETON.ARC | 100 | yes | no |
| TONEMAPC.ARC | TONEMAPC.ARC | 24 | yes | no |
| quickdat(4).arc | QUICKDAT.ARC | 2,338 | yes | no |
| OBJECTS(2).ARC | OBJECTS.ARC | 2,444 | yes | no |

Archive SHA-256 values in the same order:
- ANIMATIO(2): `ff2b0c3423e99602d9301f07657d25ea7064ae6f65d713afd2cd9d347ee06bd4`
- BINARIES: `235550520c3345e8d75f8828d15e3f9dc77ef2885cca90f098b07f6c269db7ff`
- FMODPROJ(3): `808024cd0a6953dae8f9ec797a3cd18d7f13b570cef0dc2b9d16a8ceecbb0901`
- SKELETON: `4b2c6d1c4989744cce4eff39f63343f982a38718e71b7d2dec9b4e4136e24a02`
- TONEMAPC: `be233080f34cbd305758f6f0dbace07436b695ba952a1dc9534668f14029e511`
- quickdat(4): `03a6f92ae18dbe27e84f5eccac4304489c42754fd5f44271e1306372a73c65aa`
- OBJECTS(2): `2d080575a4608f6ebf0fb04f1005a3f8b2ec96233fb4cb6e5e790af7d0d4f7b5`

**Interpretation:** Six archives are strongly attributable to the PC index.
`BINARIES.ARC` has the SAME resource extent in both PC and Xbox indexes,
so index bounds alone do not prove its platform, despite PC compatibility.
The original PC game's black screen may have resulted from prior Xbox data
substitutions, but gameplay recovery must be verified with the corrected
local installation. Restore only from the owner's clean source, backing up
replaced archives and retaining the paired PC index.ind. No automatic upload
or replacement of copyrighted assets in the public source repository.

With the new verified originals, future per-resource PC/Xbox conversion work
can now use **both original PC and Xbox animation/QuickData payloads**,
rather than treating PC animation and QuickData as missing.
