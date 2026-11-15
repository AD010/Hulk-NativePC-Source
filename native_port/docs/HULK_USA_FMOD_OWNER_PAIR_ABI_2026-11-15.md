# FMOD Event original Xbox USA pair and Windows 32-bit DLL ABI

User-uploaded FMODPROJ(1).ARC and FMODPROJ(2).ARC compared by the
NEW owner-pinned index(2).ind SHA256
ad22432b3484d079080047a5d6b8a0bffa54b7e8affba1d2273003d9b3c6090d.
Old indexXBOX360.ind (SHA 46e6c6ec) is expressly not used.

373 of 373 indexed Xbox FMOD Event resource spans are byte-for-byte identical.
The original two archives differ in 2,235 bytes, entirely OUTSIDE
the indexed resources. Last indexed byte offset 1,250,867; first difference
offset 1,250,933; last difference 1,279,351. Do not silently discard
the 28,493-byte unindexed suffix, whose semantics are unverified.

Both uploaded original PC fmod_event.dll and fmodex.dll are PE32 x86
(machine 0x014c), NOT x64. FMOD Event depends on fmodex.dll and
legacy MSVCR80; FMOD Ex uses a UPX container. These original 2008 DLLs
CANNOT be loaded into the 64-bit native Hulk process. Never bundle them
with the new x64 release or treat raw FEV1 assets as playable without
a compatible event API, WaveBank and sound decoder.

Original archive SHA256 A:
6391816b5811fe08761e90a2ae582add82c833131304385d5065d8333d1b65d4
Original archive SHA256 B:
133a9d3e19664b8ddc9174c5fa869d0b7f01da6611490d18502d28101b320956

ci/audit_hulk_fmod_owner_pair.py is a read-only, private-input ABI scanner.
ci/test_audit_hulk_fmod_owner_pair.py uses no owner proprietary data.
No original DLL, XEX, FEV, shader, archive, or save bytes in public Git.
These checks do not implement physics timing, Vulkan PSOs, actual EoR
pause-menu replacement, native FMOD mixer, or retail PC save conversion.
