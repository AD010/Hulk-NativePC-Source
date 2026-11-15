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
payload is byte-for-byte identical. Xbox has 1775 resources absent
from the PC archive indices.

The C++20 EoR reader validates original indexed Havok packfile envelopes
without loading/reinterpreting guest pointers, MOPP instructions or
physics objects. Full binary Havok object graphs need endian conversion,
fixup/class tables and verified material/shape associations.
Nothing in the provided Havok assets identifies the guest CPU Havok
stepDeltaTime, native worker thread scheduler or USA guest function addresses.
Do not claim physics decoupled to 60+ FPS or host Havok running.

The same Xbox index contains 373 FmodProjects and 387 WaveBanks records.
These are evidence of console-side FMOD sound project resources, not
evidence that Windows fmod_event.dll can directly load Xbox content.

Remaining gates: inspect/fix real USA Havok step/queue callsites, validate
native collision section fixups and scene instancing, implement native
EoR pause-menu, complete Shader/Draw Vulkan binding, PC save conversions,
audio and sustained gameplay profiling before any playable Release claim.
