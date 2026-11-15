# Havok 5.1 physics assets: PC 2008 vs user-supplied USA Xbox 360 index

Evidence: the four user-owned HAVOKS/HAVOKMOP archive uploads and
the exact supplied index(2).ind. PC uses the independently verified
little-endian indexPC.ind, not the newer Xbox index. The new index differs
from a previously supplied Xbox index, so do NOT reuse old archive offsets.

Xbox index SHA256: ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d
PC index SHA256: 3309ac7650c53c7bf64f797f962f6c5961c33774718347a2a0999fc04b04a33a
Index endianness and hash consistency prove the archive pairing, not the
region of a default.xex. USA executable address pin still takes priority.

| Group | PC resources / bytes | Xbox resources / bytes | Shared hashes |
|---|---:|---:|---:|
| Havoks | 465 / 1966560 | 1582 / 7007776 | 321 |
| HavokMopps | 360 / 3275104 | 1018 / 14333408 | 120 |
| Total | 825 | 2600 | 441 |

All 3425 indexed collision resources passed hash, bounds, non-overlap,
valid Havok magic, file-version 5 and Havok-5.1.0-r1 tests.
All have 3 sections, 32-bit pointer layout. PC layout-rule bytes are
04 01 00 01 (LE); Xbox are 04 00 00 01 (BE). No shared resource
payload is byte-for-byte identical. Of 2600 Xbox resources, 2159
have no matching PC index hash; the PC archives also have 384 exclusive
resources. The difference in total asset counts is 1775, but that is
NOT the number of Xbox-exclusive resources.

Full source-only and user-owned asset verification decoded every local,
global and virtual Havok 5.1 relocation across all four archives:
- PC Havoks: 3515 local, 4530 global, 4681 virtual.
- PC HavokMopps: 2520 local, 1440 global, 1800 virtual.
- Xbox Havoks: 12185 local, 16264 global, 17382 virtual.
- Xbox HavokMopps: 7126 local, 4072 global, 5090 virtual.
- Total: 18486 PC and 62119 Xbox original relocation links.
The original section names each end with a 0xFF marker byte, not an all-zero
20-byte padded string. Native C++20 and the independent Python audit both
validate that exact on-disk layout. The C++ reader reconstructs a type-safe
section-relative fixup graph without creating unsafe x64 host pointers.

The combined generic Havok class-name inventory contains 74 names for
Havoks and 45 for HavokMopps in BOTH PC and Xbox files, with no difference
in those inventory sets. Those names include hkpRigidBody,
hkpMoppBvTreeShape and hkpExtendedMeshShape. Class-name presence is
NOT proof an instance of every class exists in every resource, and
matching class inventories do not imply compatible byte layouts.

The C++20 EoR reader validates original indexed packfile envelopes and
decodes all three fixup kinds as logical section-reference pairs without
reinterpreting original 32-bit pointers or MOPP instructions. Full
deserialization of actual host physics objects still requires verified
endian-aware class layouts, materials, collision shapes and job runtime.
Nothing in the provided Havok assets identifies the guest CPU Havok
stepDeltaTime, native worker thread scheduler or USA guest function addresses.
Do not claim physics decoupled to 60+ FPS or host Havok running.

The same Xbox index contains 373 FmodProjects and 387 WaveBanks records.
These are evidence of console-side FMOD sound project resources, not
evidence that Windows fmod_event.dll can directly load Xbox content.

Remaining gates: inspect/fix real USA Havok step/queue callsites, convert
original Havok class fields and construct collision scene instances, implement native
EoR pause-menu, complete Shader/Draw Vulkan binding, PC save conversions,
audio and sustained gameplay profiling before any playable Release claim.
