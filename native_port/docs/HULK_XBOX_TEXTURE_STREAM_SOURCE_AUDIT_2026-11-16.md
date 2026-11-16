# Hulk USA original texture streams — source-verified 2026-11-16

Private owner files, read-only: Xbox `indexXBOX360.ind` and Xbox `TEXTURES.ARC`.

The **6,669** original indexed `Textures` records all have `LFXT`
names that hash to their original CRC32 key. **5,836** have an exact
`TexStreams` group counterpart with `ResourceHash(name + "_STREAM")`;
the corresponding original `LFXT` header stores the big-endian stream
hash at offset **NUL + 1 + 34** for 5,820 textures, or **NUL + 1 + 22**
for 16 lightmap texture records. All 5,836 source references were
individually checked against the private original index and archive.
The other **833** textures have no indexed streaming counterpart.

This establishes a definitive original texture-resource link, **not**
that any particular bitstream contains high-resolution mips, which mip
layers it stores, how the Xbox blocks are tiled, the Xenos format code,
or the game's sampler/Draw ownership. None of those may be guessed.
The original `TexStreams.ARC` was not accessible in the owner's file
library during this audit; its payloads have not been inspected.

`eor_original_texture_streams.{h,cpp}` verifies the exact original
name/hash, stream header position, index, resource bounds and preserves
the unmodified streamed bytes when the user's local `TexStreams.ARC`
is supplied. `eor_original_texture_assets` now records original
stream provenance and optionally reads that archive. A missing stream
never fabricates mipmaps. The synthetic source regression plus the
original 6,669-resource read-only local test passed with GCC/Clang.
MSVC Windows compilation and real in-game texture parity remain
unverified. No original file bytes are published.
