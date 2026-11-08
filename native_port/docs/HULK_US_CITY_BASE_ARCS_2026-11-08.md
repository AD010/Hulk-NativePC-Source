# Original USA Xbox 360 city ARC audit — 8 November 2026

The user supplied four additional US-base archives to enable the
world/LOD investigation. No original game bytes belong in public Git.

| Xbox file | Size (bytes) | USA index category | Indexed ranges valid |
| --- | ---: | --- | ---: |
| OBJECTS.ARC | 21646272 | Objects | 3938 / 3938 |
| QUICKDAT.ARC | 3412992 | QuickDatas | 3364 / 3364 |
| QDMETADA.ARC | 336864 | QDMetadatas | 614 / 614 |
| PCLEFFEC.ARC | 929472 | PclEffects | 365 / 365 |

Validated against the user-provided actual US index(1).ind, SHA256
ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d.
Every entry in those four categories has nonzero length and fits inside
the matching uploaded archive. Bounds do not verify gameplay linkage or
that every scene is genuinely from the same US title revision.

Archive fingerprints:
- OBJECTS.ARC SHA256 35c9a1d502f62a0fd4e3ef73da204195ead4598f132f21763f66d168d7801815
- QUICKDAT.ARC SHA256 0fc4d0354772c0430a410d6d4ca242c5a9cd7dc4a33dcf9dad677ae820f2acbb
- QDMETADA.ARC SHA256 adb01255c60ee87bf9fb23a8018dd13a02b8cd368f0f652863844442be0f8ec7
- PCLEFFEC.ARC SHA256 a62c8270a430d2a16aa5a078205ff1f118c207751ddff35c3e5772454ebeae31

Actual resource records found, not merely schema names:
- QUICKDAT.ARC contains 119 records named city_zone_*__lod in the first
  bytes of indexed resources, including zone 8_2, 9_3, 12_3, 6_5.
  Some resources are only 120 bytes, others multiple KB.
- 119 QuickDatas resource records contain class string
  bruceclasslib_zonelowlod.
- OBJECTS.ARC contains 463 resources with the field string LowLODModel,
  24 with HighLODModel, 21 containing both fields.
  For example 0B72F95E bldg_city_mt0022_m_lod2 (5627 bytes) contains
  LowLODModel and HighLODModel.
- QDMETADA.ARC includes 3 metadata resources mentioning ZoneStreamManager,
  and specific class records for sharedqdlib_ZoneStreamManager and
  sharedqdlib_ZoneStreamManagerLayer, and bruceclasslib_zonelowlod.

Those findings identify a concrete zone LOD data model. They do NOT yet
identify the numeric run-time distance for switching from low to high.
Never blindly replace references across files without validated reference
layout, free-space allocation, CRC and a real console/recomp rendering test.

Independent issues: NativeX continues to show black with user's latest
manual true setting; log100 was recorded with Xenos and lacks the NativeX
front resolve state, so don't claim these ARC findings fix GPU output.
