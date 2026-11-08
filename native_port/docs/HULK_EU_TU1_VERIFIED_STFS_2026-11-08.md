# EU Title Update 1 audited; USA port requires code-specific translation

User supplied: TheIncredibleHulkEurope_TU1.zip, STFS LIVE payload
TU_19KA1VH_0000008000000.0000000000082 (2441216 bytes).
Container header verified: Title ID 0x534507F1,
Media ID 0x2BA1D2F6, type 0x000B0000, TUPD marker.
Parsed 36 STFS entries, 31 payload files; every extracted block
passed SHA-1 block hash verification using the STFS L0 chain.

Root payload files include:
- default.xexp 1351680 bytes, SHA256 f0a34fdf59112ef144782da60c64cc56fa7ee4799e95d91992f4d0da298e5aae
- DATASETS.ARC 31712 bytes, SHA256 7b6c02a56fe38edeae832533e3220ef361e2b4a4e142b0d0143507cef2ca0e96
- index.ind 8500 bytes, SHA256 783dc5df04956ebf875032c9a8d46d65d03b7d4af056e48bc8491df0fa517e00
- MODELS.ARC 25216 bytes
- OBJECTS.ARC 546016 bytes
- QUICKDAT.ARC 45440 bytes
- localized FRENCH, GERMAN, ITALIAN, SPANISH, UKENGLISH archives and indexes.

These data files are *differential* title update resources. They cannot
replace the base 534176-byte INDEX.IND or complete large ARC containers.
The XEXP was built against the EU/PAL XEX, NOT the US XEX that the
native recompiler uses. The signature (and changed PPC codegen) differs.
Matching title ID does NOT prove binary patch compatibility.

User confirms EU Title Update 1 fixes the water-jump anomaly on
Xbox 360 and in an emulator. To port it, disassemble an EU XEXP
patched executable and compare the changed PPC control-flow to
US base PPC at validated functions; translate only verified code/data
changes, retain US codegen and recompiled guest hooks.
Do not claim this TU1 is installed in a US build without that work.
Original STFS, XEXP and ARC binaries are user data, NOT public Git.
