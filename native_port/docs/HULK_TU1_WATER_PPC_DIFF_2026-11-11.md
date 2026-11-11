# Hulk USA NativePC — verified TU1 water-transition porting evidence (11 November 2026)

The user reported a repeat water jump/hovering cycle present on unpatched
Xbox 360/PS3, but absent with Xbox TU1. This is a game-state/animation
correctness requirement, NOT a GPU-only bug or an FPS-only workaround.

## Verified owner-supplied TU1 inputs

- User's EU update \`TheIncredibleHulkEurope_TU1.zip\`: SHA-256
  \`3792037f8af804d34f4c264188597222b0d8ffe982ad64ba8136f3fcde767b50\`.
- Authenticated STFS LIVE / title 534507F1 / media 2BA1D2F6:
  31 entries, 31 block-hash-verified files, including 1,351,680 byte
  \`default.xexp\`, \`OBJECTS.ARC\`, \`QUICKDAT.ARC\`, \`DATASETS.ARC\`,
  \`MODELS.ARC\`, differential \`index.ind\` and localizations.
- Original EU/PAL base XEX, SHA-256
  \`ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351\`,
  has RSA signature hash \`8ea1670d19d66c322f50a4d1a6409466a86abada\`,
  exactly matching the TU1 delta descriptor. EU XEXP target version
  \`0x00000102\`; source version \`2\`.
- The USA recomp accepts only SHA-256
  \`346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0\`.
  The EU XEXP does NOT match this image and must not be applied to it.
- \`ci/audit_hulk_tu1_resource_index.py\` established 525 changed
  resource hashes, 524 appearing in US base index. This is insufficient
  to claim code/data compatibility for the water transition.

## Direct next provenance: changed European guest PPC functions

\`ci/prepare_hulk_tu1_stage.py\` already stages the matching original EU
XEX and its sibling \`default.xexp\` in a private codegen root. ReXGlue
applies the sibling XEXP during codegen. Compare two *isolated* EU ReXGlue
source-generation outputs made with the same recompiler revision:
one from the original EU XEX only; one from this exact EU XEX+XEXP.

\`ci/diff_hulk_tu1_guest_ppc.py\` now authenticates BOTH proprietary
inputs (EU original SHA, EU update SHA, XEXP RSA/source/target versions)
and compares the PPC instruction comments in the generated
\`*_recomp.N.cpp\` files, preserving original guest addresses. It writes
a metadata-only JSON of changed functions and changed instruction
addresses; never outputs instructions, proprietary shader bytes, or
render data, never overwrites originals, and never generates a USA hook.

A changed function is only a candidate. To prove the water fix, inspect
the modified EU branch/control-flow and its callers during water entry,
exit and the repeated jump/animation transition; map the semantics to
the corresponding USA PPC function using control flow, not matching
addresses. Verify a single return to land after contacting water,
without repeated arm flailing or airborne bounces, at 30 and 60 Hz.
Also check missions, water particles and legitimate repeated jumps.

## No premature claims

This commit does NOT port the actual water logic or install EU data in
the USA game. It establishes an exact, reproducible, content-authenticated
path to isolate the TU1 changed PPC before any safe USA adaptation.
No additional GitHub Actions workflow, no new game binary and no
automatic Windows build are introduced; the existing workflow remains
manual-only as requested. The graphic defects in build 57/58 also
remain open and must be tested on hardware before release.
