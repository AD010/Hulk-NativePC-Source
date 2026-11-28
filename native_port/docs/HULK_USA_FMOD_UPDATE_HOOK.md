# USA EventSystem update hook evidence

Original decoded USA PE SHA256 aeb9ff9e6a16f5867b69c0cb626aa8e56d9bd96f820ddd34ab5b25f6faadde29.
EventSystem vtable at 0x82089600: first three entries 0x82841988, 0x82841AB0, 0x82841DB0.
Update body 0x82841DB0, 192 bytes, SHA256 2684b297a1b640f1ad5e9ec5653bd9c0b9ee89b4ca0f26f8dfbcd492fe87efbb, FNV-1a64 e5d96c39496dd8fe.
At 82841DC0 the original checks this+160. At 82841DF4..82841E0C it computes and stores its clock delta from this+28; on wrap it uses the current clock. At 82841E3C it calls recursive event updater 8283A718, which calls 82839BA8 at 8283A7C0; at 82841E64 it calls the core update 82778260. The original function is called exactly once by the dedicated generated wrapper; no guest result is replaced.

Experimental HULK_EXPERIMENTAL_NATIVE_FMOD=1 creates a native mixer at app setup and updates it on this verified call only, retaining the original FMOD playback. Shutdown removes the mixer under the same mutex. First timer initialization reports zero elapsed because its first sample is internal. No automatic event admission or original FMOD suppression is installed. Native event identity, complete original event ABI, exact mixing and Windows execution are still unverified. This is an actual clock/lifecycle connection, not completion of native audio.
