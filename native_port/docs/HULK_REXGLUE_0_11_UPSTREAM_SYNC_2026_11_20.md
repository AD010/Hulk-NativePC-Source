# Hulk NativePC — ReXGlue upstream synchronization (20 November 2026)

## Revisions checked

- Official ReXGlue SDK **v0.11.0**, released 14 Nov 2026, release commit ebac1cf0d04b.
- Development nightly **0.11.0.13-dev.g115171b4**, released 17 Nov 2026.
- Compatible Hulk SDK: ElGuslml/rexglue-sdk **recomp** at 4348a344eb86ae456e1ccf7d393085d720cc6ce6 (14 Nov). Its former checkout in BUILD_WINDOWS was an unpinned moving branch.
- NativeXRenderer D140 **fa19ea85fec5c7f7f8a2df82d6c4a86548817b52** (11 Nov): already pinned; unchanged.

## Integrated now: three official v0.11 PowerPC correctness fixes

The Windows builder now pins the compatible ReXGlue fork and runs ci/test_patch_sdk_upstream_v011_ppc.py then ci/patch_sdk_upstream_v011_ppc.py before PPC code generation. The source guard validates every modified function and refuses drift/partial application, without substituting official SDK module ABIs.

1. Upstream c9f19382fd84 — fctid/fctidz upper saturation at exactly 2^63, not an inaccurate comparison with rounded double(LLONG_MAX).
2. Upstream 02a002f81992 — srad/sraw large-shift carry flags (>=64/32) are computed from the source sign.
3. Upstream ab61f2e693f4 — vcmpbfp NaN two failure bits and record-form CR6.lt are correct.

Five local synthetic Python tests passed on the reviewed backport algorithm; the actual fork source has been inspected for the required original anchors using GitHub. The existing manual Windows workflow also runs the synthetic tests before touching private inputs. No new workflow, branch, release or proprietary game content.

## Not yet integrated

Official v0.11 module interface versioning, lifecycle changes and CMake restructuring are ABI-breaking against ElGuslml/recomp and NativeX D140, so an automatic SDK swap is unsafe. Nightly 17 Nov also changes CMake incremental codegen, Vulkan VMA pinning, SettingLife to SettingMode and interface lookups; none is claimed installed. Fork GPU adapter/audio/network changes are only source-level candidates until wired and tested for Hulk.

## Acceptance boundary

These are CPU PowerPC codegen edge fixes and reproducibility controls, NOT proof of a native EoR Vulkan gameplay frame or any fixed rendering artifacts. A real Windows build and game run remain necessary and were NOT launched by this change.
