# US INDEX.IND metadata audit, 8 November 2026

User supplied new USA index (no asset bytes in repo): 534176 bytes,
SHA256 ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d.
Previously supplied Xbox index: same size, SHA256
46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820.

Both have 26 categories and identical resource keys/counts, but exactly
4250 offset/size changes: QuickDatas 2369, Animations 1849, Datasets 32.
All 1114 Datasets record ranges remain inside a previously supplied
582239200-byte DATASETS.ARC, no overlaps; this is only a bounds check,
not proof that this ARC contains the matching USA payload.

Win32 now accepts only the pinned US XEX SHA-256 and logs probable
USA index file checksums. No game files are copied into public Git.
Do not rename regional UKENGLISH indexes without verifying XEX requests.
