# Hulk Xbox 360 USA LFXT and TexStreams: exact mip-allocation evidence
16 November 2026, source-only audit. **Do not mistake size families for proven Xenos GPU formats.**

## Provenance

- Owner uploaded `Hulk_USA_LFXT_Header_Report.txt`, 10 original source descriptors,
  from Xbox 360 USA `TEXTURES.ARC` (351,883,360 bytes), INDEX.IND SHA256
  `ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d`.
- Owner uploaded 10 original `TexStreams` .bin samples + manifest and a text
  hex report: source archive 1,330,443,104 bytes. Each BE record-prefix u32
  is exactly `record.size() - 4`.
- The original game bytes and report bodies are NOT committed to GitHub.
- The stream manifest originally lacked its own USA-index hash. However
  the LFXT-linked stream keys and original indexed sizes match exactly
  across all ten independently supplied samples.

## Actual LFXT structure (all 10 match)

After `LFXT + ASCII name + NUL`:

- Resident mip dimensions are 16-bit BE fields at offsets `+3` (width)
  and `+7` (height) relative to the byte **following** the name's NUL.
  Big-endian float32 at `+9` equals `sqrt(resident_width * resident_height)`.
- Original `_STREAM` CRC32 hash is at `base+34` in 8 descriptors, or
  `base+22` in 2 directional-lightmap descriptors.
- Original stream hash `+8`: u8 number of streamed mip records (1-4 in
  current samples).
- Original stream hash `+9`: count times `(BE u32 tiled allocation size,
  BE f32 sqrt(pixel_area))`. The record order is largest to smallest
  texture, each level doubles the preceding lower level's width/height.
- Exactly after those entries: two nested BE remaining-length fields.
  Outer and inner values respectively equal `remaining_bytes_after_field`,
  such as `0x8038, 0x8034`. This is variable-offset within LFXT.
- All ten have `sum(streamed aligned allocations) == stream_RECORD_size-56`.
  All ten have `inline_second_length-52` equal to the sum of their
  *resident* tiled mip allocations (usually 2 compressed, 1 HDR).
- A 32x32 minimum alignment in **compressed 4x4 blocks** explains larger
  allocations for 64x64, 128x64 and 256x64 mips. For uncompressed
  8-byte texels, align the texel grid to 32x32.

## Ten authentic original mip reconstructions

The format column is only the **size/tiling family**, not DXT subformat.
Other GPU fetch controls, endian and color-space handling must be confirmed
from the actual original USA engine/Xenos fetch declaration.

| Source texture (original name) | Resident mip WxH | Streamed mip dimensions (largest first) | Stream alloc bytes | Resident mip count | Tiled size family |
|---|---|---|---|---:|---|
| building_brick41c_grunge_ds | 32x32 | 256x256;128x128;64x64 | 65,536;16,384;16,384 | 2 | BC 16-byte block |
| signswap_64x32_eats10_d | 64x32 | 128x64 | 8,192 | 2 | BC 8-byte block |
| building_empirel_n | 128x32 | 1024x256;512x128;256x64 | 262,144;65,536;32,768 | 2 | BC 16-byte block |
| frontend_lightmap_dir | 256x256 | 1024x1024;512x512 | 8,388,608;2,097,152 | 1 | 8-byte texel |
| bldg_city_mt0098_egc_marble02b_ds | 32x32 | 256x256;128x128;64x64 | 65,536;16,384;16,384 | 2 | BC 16-byte block |
| perimeter_sidewalk01_curb_n | 256x32 | 512x64 | 65,536 | 2 | BC 16-byte block |
| storefront_mid_016d_n | 32x32 | 512x512;256x256;128x128;64x64 | 262,144;65,536;16,384;16,384 | 2 | BC 16-byte block |
| building_windows07a_n | 64x32 | 512x256;256x128;128x64 | 131,072;32,768;16,384 | 2 | BC 16-byte block |
| bf_area1_lightmap_dir | 256x128 | 1024x512;512x256 | 4,194,304;1,048,576 | 1 | 8-byte texel |
| signswap_32x32_misc01_d | 32x32 | 64x64 | 8,192 | 2 | BC 8-byte block |

Concrete code:
- `FindOriginalLfxtInlineSection` searches actual nested lengths without
  hard-coded name or array-body offsets.
- `ParseOriginalLfxtStreamTable` validates original count, BE u32
  allocations, finite BE f32 metrics, original indexed stream size.
- `ReconstructOriginalLfxtMipPlan` validates resident dimensions, float
  sqrt(pixel_area), all streamed and resident allocations with 32x32
  tiled block alignment; **fails closed** on unknown/ambiguous families.
- `UploadExplicitOriginalTexture` now requires a complete reconstructed
  mip sequence and matching explicit GPU format family before allocating
  a DiligentCore image. BC4/BC5 and 8-byte RGBA16F uploads are available
  when independently validated from real Xbox GPU fetch state.
- Synthetic tests use original sizes/metadata only; not original pixels.

## Remaining blocker before game-quality Vulkan results

**The unexplained +52 bytes in the nested LFXT/TexStreams payload
containers have not been located as an actual prefix versus suffix in the
original byte stream.** Both candidate offsets are currently unverified;
do not blindly advance or clip 52 bytes. Source allocation size matching
alone cannot prove actual physical payload beginnings, Xenos BC1 vs BC4,
BC2 vs BC3 vs BC5, channel swizzles, endian, sRGB, or original shader fetch
slot/descriptor set.

The real USA engine Draw and shader/PSO/SRB runtime still must provide
those verified parameters. Do not make up binding flags or ship a
graphics-fix build without the matching GPU capture and real test.

The compute sandbox in this session returns ClientError on even
`print(1)`; original metadata TXT was read via the file connector,
and actual cross-file arithmetic was checked in isolated JavaScript.
No Microsoft C++/DiligentCore compilation or in-game Vulkan test has
been claimed for the current HEAD. Existing CI remains manual-only.
