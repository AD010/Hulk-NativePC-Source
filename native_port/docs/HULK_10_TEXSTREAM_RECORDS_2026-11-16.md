# TexStreams sample records: 10 original Hulk uploads (2026-11-16)

The owner provided ten individually extracted binary TexStreams records and
`manifest.txt`; no game-owned bytes are committed.

**Actual parsed manifest**: source original ARC size 1,330,443,104 bytes.
Ten record lengths: 98,360; 8,248; 360,504; 10,485,816; 98,360;
65,592; 360,504; 180,280; 5,242,936; 8,248 bytes.

Every listed record length minus **56** is a multiple of 4,096.
This is evidence of a candidate 0x38-byte header, **NOT** proof
of field layout, Xenos texture compression format, mip packing, or
any live EoR shader sampler slot.

The manifest says **"original Xbox 360 IND"** without the pinned USA
index SHA-256. It cannot independently attest that these bytes came
from the same disc as the pinned USA `default.xex`. The earlier
region-to-region 4,250 entry differences in `QuickDatas`,
`Animations`, `Datasets` did **not** include `TexStreams`,
but this still does not prove the exact ARC identity.

`ci/audit_hulk_texstream_records.py` reads these extracted samples
without requiring the original 1.33 GB archive. It reports the first 56
byte hex prefixes, big/little-endian 32-bit interpretations, candidate
4 KB boundaries, cryptographic digests, and consistency checks.
Its report is **private original-asset metadata** and must not be
pushed to GitHub. The source and synthetic unit tests are safe to
publish. The parser will not claim decoded texture formats without
original source field corroboration.

Research cross-check: Acewell released a partial X360 Hulk LFXT
Noesis script in January 2019 and explicitly noted variable-length
headers; do not reuse that script blindly as proof for TexStreams
records.
