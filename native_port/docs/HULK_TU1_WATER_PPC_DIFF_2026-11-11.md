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

## Input recovery verified in the current workspace

The owner-uploaded original `default.xex` and
`TheIncredibleHulkEurope_TU1.zip` were materialized and the real
`prepare_hulk_tu1_stage.py` completed successfully. The XEX SHA-256 is
`ede8b62b5d4d0357ace2e08caf06a6bd702783d1a9b036bbf405124bd84f3351`;
MediaID `2BA1D2F6`, region mask `00FF0000`, D3D9/XGRAPHC `2.0.6534`.
The 31 authenticated update files are staged privately with the matching XEX.
This resolves access to those two inputs, not application/recompilation of
XEXP or identification of the water-jump function. No TU bytes, PPC code or
unverified USA hook addresses were added to the public repository.

Required next evidence is still a same-tool original/patched EU executable
comparison, then semantic mapping to the USA code and runtime validation of
water entry/exit. The separate uploaded file also called `default.xexp`
was not substituted for the authenticated patch extracted from the TU ZIP.


## Conservative USA function correlation (source-only)

The comparator now accepts an optional `--us-generated` directory produced
by **the same** ReXGlue revision from the SHA-pinned USA base XEX. For each
changed EU function it compares the **unpatched** EU PPC instruction sequence
to USA functions. It normalizes only relocation-sized branch destinations
and requires an unambiguous exact match of at least 24 instructions. Short,
ambiguous, or changed functions remain *unmapped*, never assigned a guessed
hook address.

This produces `usa_candidates` with a status for every changed EU function.
It is **not** proof that the candidate is the water handler or that an
EU patch can be copied over. Before a USA game change, verify direct
callers, data accesses, state transitions, and water-entry/exit behavior;
a matching instruction signature by itself is insufficient. No proprietary
generated C++ or binaries belong in the public repository.

Example (private codegen outputs only):

```bash
python ci/diff_hulk_tu1_guest_ppc.py \
  --eu-xex /private/original-EU/default.xex \
  --xexp /private/staged-EU-TU1/default.xexp \
  --eu-base-generated /private/eu-original-generated \
  --eu-tu1-generated /private/eu-tu1-generated \
  --us-generated /private/us-base-generated \
  --us-xex /private/verified-USA/default.xex \
  --output /private/tu1-function-candidates.json
```


### 11 November: safe branch-topology correlation

Region mapping now keeps the **internal PPC branch destination structure**,
indexed by local instruction position. Only external call targets are normalized
as relocations. Previously two functions with the same mnemonics but different
internal loop/branch destinations could have received the same signature.
This is not acceptable for water-state-machine fixes. An invalid/duplicate
instruction address also fails closed. Local synthetic regression: six tests
passed (relocation, topology, external call, changed constant, duplicate and
missing address). No game input changed, no Windows build or water claim.

## 28 November — actual EU XEXP decode and first USA transfer

Used the existing private TU archive, EU base and already decoded USA PE. A
standalone bounded LZX reader, based on the format and the ReXGlue/Xenia
reference-window approach, decoded the XEXP without running a console VM or
downloading the SDK. No proprietary binary or generated guest source is
committed. Verified the pinned XEXP SHA256 and base signature, original key
via the patched header, SHA1 of all 22 encrypted-body blocks after decryption.
Header decode: one compressed record. Image records: 56 zero, 136 copy,
329 compressed. Private mapped EU original PE SHA256:
d1e94d0e5b2ec2528d1f0720788e778edc372abc20283b6cbe9c5eea866ecc45.
Private mapped EU TU1 PE SHA256:
ddfd359182cfe6eb9b99ba5ddf12918ed3f54dc0ddc7bbf169a8d23a8567b807
(21626880 bytes). Header SHA256:
36bdbd6458cefdf047168edf87a8c16ebf899ec64a93c8bed874ab6055866065.
These hashes are locally measured, not independently published image hashes.
The reconstructed PE headers and function/RTTI string neighborhoods are
coherent; full independent decoder cross-validation remains outstanding.

Cross-version registration string HulkWaterJump: USA 8228EC70, EU base
8228F1C8, EU TU1 82295F40 (load references). WaterJumpData references:
8228D358, 8228D8B0, 822945F8 respectively. Matched nearby method entry
8228D760 / 8228DCB8 / 82294A00; exact method-range SHA256 respectively
09a4d5a3d1314897f4d4adfdbf653182b9332871d708890082cb2250ac8fc583,
fa2a523a97cdcda1874aa31ed4c739fa0666057f9718774a84e8b812f5cd3b91,
fa9792007270c93855bed676d9d823babbd6d3aabb2a2f22bea3e53036c5bbb3.
The ranges end at 8228DAA0 / 8228DFF8 / 82294D60. Vtable references at
820C64E0 / 820C64E8 / 820C4DD0. TU1 82294A54..82294A6C compares
state+220 against float -1 (820005D0), and if CR6.GT is set stores
float -5 (82002F04). In USA the corresponding original copy of actor+84
to state+220 is at 8228D7A8. Transfer exactly this conditional store in
the generated USA C++ after verifying the source XEX hash and four
contiguous original assembly comments and translated store. The patch
runs before the existing Windows recomp target, is idempotent, and
preserves original code when the expected generated form is absent.
Three synthetic Python tests pass; no actual Windows codegen/build or
gameplay confirmation has been performed.

Further matched candidate method 8228DD18 / 8228E270 / 82294FD8:
EU TU1 82295514 onward changes vertical difference scale from .5 to 2,
and 822955A0..822955B8 updates state+196 from current actor+84 and
recomputes +200 using the new target height. These are NOT transferred
by the first fix; additional register/data-layout and control-flow
verification is necessary, including separate building-ledge cases.
The first patch is not proof of a complete jump bug fix.
Reference: ElGuslml/rexglue-sdk recomp src/system/lzx.cpp,
src/system/xex_module.cpp; kyz/libmspack LZX format implementation.

### TU1 trajectory arithmetic transfer

Second matched method range USA 8228DD18..8228E5B8 SHA256
9854dcddddecaa94b4dc17eb691123e0c46971e0db734cb373046095eb6fef64,
EU base 8228E270..8228EB10 SHA256
57497a5ec730d300fa12320dfbe9e8c8b2477419ce58edc0a5153c5435ec5586,
EU TU1 82294FD8..82295878 SHA256
54e817cee18def85eb6fcccaedcdde37e73ae54db93f188d3c199f7220dcfd7c.
The unchanged original USA actor+84 height and state+192/+196/+200/+220
are matched to the corresponding EU TU1 fields. USA 8228E264 scales
(previous height - current actor Y) by .5; TU1 82295520 scales by 2.
TU1 822955A0..822955B8 stores current actor Y at state+196, calculates
absolute target Y = fmadds(curve, amplitude, initial Y) and stores
current Y - target Y at state+200. The USA generated patch changes the
matched arithmetic and store sequence without importing EU addresses,
register layouts or branch destinations. The absolute target is retained
in USA f29 for the same original collision/position path. The current
actor coordinate is sampled once for the state write and subtraction.
The generator pins the original source XEX, requires the original
assembly sequence and matching translated floating-point/memory operands,
and rejects unknown or partially modified input. Three synthetic Python
tests passed. No actual Windows recomp or gameplay tested.

Remaining source difference: EU TU1 82295678..8229567C clears an actor
byte at +1021 after the transition call; the USA member offset and
semantics have not yet been established. Other jump/ledge methods and
PS3 updates have not been exhaustively mapped. The two committed
transfers must not be reported as a proven complete gameplay fix.
