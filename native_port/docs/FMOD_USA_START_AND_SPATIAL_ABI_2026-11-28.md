# USA FMOD: selection, successful start, and spatial observation

Source provenance: original USA XEX SHA-256 `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`; decoded mapped PE SHA-256 `aeb9ff9e6a16f5867b69c0cb626aa8e56d9bd96f820ddd34ab5b25f6faadde29`. Verified locally against the owner's previously supplied XEX. No original binary or instruction dump is distributed.

- Selector 82838650: 82838664 loads parent from child r27 +76; 828388FC stores selected r11 at parent r21 +48. A cached branch without a new store is deliberately unresolved.
- Start 82839368: calls selector at 828393DC; 8283960C stores active flag in child r31 +20. This is not proof of success: a later error can branch to 82839B98. V2 saves the child at the flag store and only confirms on the verified successful return at 82839B94. Exact instruction, register and owner checks are required by the generator.
- EventSystem listener setter: vtable +120, 82844368, forwards core +16 to 827783D8; 8277ABD0 validates and returns the same core pointer. 8277D2D0 writes listener position +33288, velocity +33216, forward +33252, up +33228 (stride 136). Only listener zero is observed.
- Channel setter 82787B10 resolves opaque handle via 8278FB48: system ID high nibble, channel index bits 16..27, generation low 16; core ID +34140, count +332, array +336, stride 416. Slot +112 stores full handle. 82791C18 checks sound pointer +48 and mode bit 0x10 at sound+100, then stores position +292 and velocity +304. Capture requires exact nonzero generation and rejects invalid coordinates, bounds or inconsistent ownership.
- Live snapshots now carry these optional original spatial values. They do not feed X3DAudio yet: handedness conversion, distance scale, attenuation and listener selection need validation. Original playback remains unchanged.

Local PE inspection is not Windows gameplay or a successful build of actual generated translation. Synthetic generator and memory tests are separate from in-game verification.
