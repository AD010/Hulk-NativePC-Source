# Original Hulk Xbox texture-stream audit — region corrected 2026-11-16

## Scope and correction

The previous report incorrectly called the audited data **USA**. The
6,669 `Textures` records and 5,836 `TexStreams` references were
examined using the owner's older `indexXBOX360.ind`, which is **not
the separately identified USA global index**. They MUST NOT be called
USA coverage figures or treated as validation of the USA resource pack.

Existing source `assets_locator.cpp` independently distinguishes:

- **Pinned USA global INDEX.IND SHA-256:** `ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d`.
- **Older Xbox INDEX.IND SHA-256:** `46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820`.
- The USA index had **4,250 different resource entries** versus
  the earlier Xbox index in the user's prior audit.
- **CI-pinned USA default.xex SHA-256:** `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`.
- Earlier **PAL** `default.xex` from the owner Library had SHA-256
  `ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351`;
  it is NOT the USA executable.

A big-endian index is an Xbox **format check**, not a region or
matching-disc **identity check**. An Xbox texture hash matching an index
entry is not proof of applicability to another title/disc revision.

## What actually remains established

For the earlier Xbox index + accompanying source `TEXTURES.ARC`, all
6,669 indexed texture records had LFXT names hashing to their indexed
CRC32 resource key. Among those, **5,836** have a matching indexed
`ResourceHash(name + "_STREAM")` entry with a stored BE stream hash
at offset NUL+1+34 for 5,820 records or NUL+1+22 for 16 lightmaps;
**833** have no indexed streaming counterpart. These exact totals
describe **only the audited older data set**, not USA.

The generic EoR archive parser continues to support earlier and
synthetic sources for read-only diagnosis. The **runtime USA model
streaming path** now rejects any index not matching the separately
pinned USA SHA-256, and the texture loader has a separate strict
`OpenPinnedUsa()` factory. The existing Xenos/NativeX fallback and
user configuration remain unchanged.

## Previously supplied split archive — found

The owner already uploaded `TEXSTREA.7z.001` (**471,859,200 bytes**)
and `TEXSTREA.7z.002` (**252,506,863 bytes**) on 2026-11-14.
Earlier statements that the TexStreams data had not been uploaded,
and requests for the owner to resend it, were wrong.
The split parts were located and materialized into the private working
area. Their decompressed payload and relationship to the **pinned USA**
archive have **not yet been independently verified**; do not claim
mip layout/texture format parity or include copyrighted data in Git.

No current Windows Release, real game render, or fixed pop-in/flash is
claimed. Resume from provenance-proof of the exact USA index, then
archive data checks, source-authentic GPU texture/mip decoding,
original sampler ownership, and host render-target PSO integration.
