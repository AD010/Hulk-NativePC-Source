# Hulk Xbox 360 USA — real 6,669-texture format and mip audit
16 November 2026. Source: owner-provided original USA INDEX.IND and TEXTURES.ARC,
ten full original TexStreams .bin examples and original LFXT bytes.
Original game data is NOT included in this repository.

## Cryptographic and archive source provenance

- INDEX.IND SHA-256:
  `ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d`
- TEXTURES.ARC original byte size: 351,883,360.
- Original TexStreams.ARC source size: 1,330,443,104.
- USA index resource counts: **6,669** Textures; **5,836** TexStreams.
- Audited every one of the **6,669** original indexed LFXT records, using
  the original Bytes, NOT names/types from a synthetic unit test.
- All 6,669 have 52-byte GPU descriptors at the END of the resource.
  Their BE format IDs were parsed directly from descriptor offset +32.
- All 6,669 resident width/height values match the LOW 26 bits of
  the packed trailer dimensions at +36. Exactly 11 no-stream environment/
  cube maps use additional nonzero top dimension/type flags and must not be
  mistaken for ordinary 2D images.
- Exactly 5,836 stream hash references had valid count + 8-byte
  aligned-size/float entries, with zero mis-sized source streams.
- All **5,836** stream-linked mip chains match the 32×32 Xenos
  tiled allocation model and original image-level sqrt(area) floats,
  together with the resident inline mip allocations.
- Of 833 without TexStreams, 822 have a fully matching simple resident
  2D allocation, 11 are special cube/environment image types, and
  one other no-stream resident allocation requires a separate check.
  (The special dimensions and one exceptional allocation may overlap;
  do not sum these counts as distinct categories.)

## All original GPU image format IDs

| ID | Xbox 360 Xenos enum | Count | Native host physical format |
|---|---|---:|---|
| `0x14` (20) | `k_DXT4_5` | 4,557 | BC3 (DXT5) |
| `0x12` (18) | `k_DXT1` | 2,051 | BC1 |
| `0x06` (6) | `k_8_8_8_8` | 44 | RGBA8 |
| `0x1D` (29) | `k_16_16_16_16_EXPAND` | 16 | 8-byte/texel half-float storage family |
| `0x02` (2) | `k_8` | 1 | R8 (no TexStreams) |

Refer to Xenia `src/xenia/gpu/xenos.h` for Xenos GPU format enum
and Xenia `texture_info.h::GetBaseFormat` for `_EXPAND`
sample-format conversion. Shader swizzle, original signedness, colorspace,
fetch register number and PSO remain independent state; never infer
them from a filename or from block size.

All original 52-byte GPU descriptor suffixes follow this observed
structure (BE 32-bit fields): first five words `3,1,0,0,0`,
next two words `0xffff0000,0xffff0000`, word 8 at +32 the GPU format,
word 9 at +36 `(width-1)|((height-1)<<13)` for 2D,
word 10 at +40 `0xD10` for DXT1/DXT5,
`0xD11` for expand RGBA16, `0xC14` for RGBA8.
Other suffix words are still format-/texture-specific; do not
interpret them as addresses without additional evidence.

## Real physical start/end of texture data

The owner-provided ten original TexStreams .bin files EACH begin with
a 4-byte BE field equal to the number of bytes in the rest of the
record. Their first GPU blocks therefore begin at original record
byte **4** and are followed by the 52-byte GPU descriptor trailer.

The full source LFXT records have variable-length headers with two
nested BE lengths; their inline mip bytes begin directly after the
second length field and terminate before the exact last 52 bytes.

Direct untile and BC decoding of original game bytes (not modeled
textures) produced recognizable signs, windows/building facades and
normal maps when 16/32-bit byte swap was DISABLED, and scrambled images
with those byte-swaps enabled. The original tiled address uses Xenos'
32×32 layout in blocks (compressed formats) or texels (uncompressed).
The two verified `RGBA16_EXPAND` lightmaps produced finite half-float
samples without byte swapping. This is evidence for the tested format
families; actual game shader sampler state needs separate validation.

Full stream and resident mip dimensions and source byte offsets are
now computed directly and conservatively by
`eor_original_texture_gpu_layout.cpp`, which validates both original
GPU descriptor suffixes and all actual byte ranges before marking
`absolute_source_offsets_proven`. `UploadVerifiedOriginalTexture`
uses these source-proven layouts, not fabricated mip pointers.

## Live rendered frame / release status

**No in-game visual improvement is certified by this audit alone.**
The actual D140 NativeXRenderer rendering path reads original Xenos
fetch bindings from guest memory and does NOT yet call the new
OriginalXboxTextureAssets / UploadVerifiedOriginalTexture bridge.
We still need a faithful mapping between a live guest fetch register,
the guest material hash and the exact SRB used by the current Draw.
Source-only format decoding must never stand in for that mapping.
Current Windows build has not passed post-audit Release CI and a new
game frame has not been captured.


## Complete 1.33-GB original TexStreams.ARC binary verification

The two owner-provided TEXSTREA.7z volumes were reconstructed and
decompressed with libarchive; the extracted TEXSTREA.ARC is exactly
1,330,443,104 bytes. Original full-file SHA-256:

`4e49b38f7357f37b8563653e928a034ec3bdb3f77bf533ec0acad5e5a4c76229`

The entire original stream archive was then memory-mapped and EVERY indexed
USA TexStreams record's actual bytes were inspected (not only the ten
samples and the INDEX metadata).

- 5,836 / 5,836 physical records have correct BE remaining-length header
  `record_bytes-4`.
- 5,836 / 5,836 have authentic structured 52-byte GPU descriptor
  **trailers**, byte-verified against their linked source LFXT records,
  including the original GPU format number and extended mip dimensions.
- Every 5,836 / 5,836 physical high-res descriptor's dimensions are
  exactly `resident_width * 2^stream_level_count` by
  `resident_height * 2^stream_level_count`.
- Every record's actual size equals the original LFXT per-mip aligned
  allocations + 4-byte BE prefix + 52-byte trailer.
- Zero errors in original prefix, trailer formats, source bounds, linked
  mip-table sizes, and high-level dimensions across all records.

Original original-stream physical format counts (5,836):
DXT4_5 = 4,172; DXT1 = 1,648; RGBA16_EXPAND = 8; RGBA8 = 8.
The remaining 833 original texture records have no TexStreams link.

**This proves the full ORIGINAL ARCHIVE byte layout, not that the
NativeXRenderer guest GPU memory or its PSO/SRB uses these exact resource
names or file offsets.** The live per-Draw guest texture-fetch address
still needs to be joined with original asset bytes with strict identity
and lifecycle checks. Until then, the new codec cannot be described as
an on-screen graphics fix.
