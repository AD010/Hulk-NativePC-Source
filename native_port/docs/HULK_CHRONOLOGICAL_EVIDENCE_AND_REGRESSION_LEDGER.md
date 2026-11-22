# The Incredible Hulk NativePC — chronological evidence and regression ledger

**Audit performed:** 2026-11-16. **Repository:** `AD010/Hulk-NativePC-Source`.

> Source-safe summary. No proprietary game microcode, original assets, raw GPU dumps or full user logs are republished.

## Continuation: 17–21 November 2026 (USA/main; source-safe)

This section extends the **16 November** audit below. Its 72-log/67-unique
counts still end on **15 November**; no fresh gameplay capture is attributed to
the current binary. All dates here are UTC unless otherwise stated.

| Date | Recoverable milestone | What it does NOT prove |
|---|---|---|
| 17 Nov | Native EoR indexed Draw/SPIR-V/mip resource code and original archive streaming progressed; commit `3ef24cb23af302c0e6b474f9acb498fae1e7c5f3` observed | No real completed direct EoR Vulkan gameplay image |
| 18 Nov | The native host output/source provenance report recorded `2cd854fe5454be9e316e23ed8c080e72d9be95c0` and a checked Vulkan output/composite contract | A compositor contract does not create authenticated EoR draw witnesses |
| 19 Nov | Genuine USA XDK Present/Draw snapshot, strict worker ownership and Vulkan preflight became the intended path; camera/material/effect identity still source-gated | No invented ABI for `UIPage_Pause_Settings` or mesh/material owners |
| 20 Nov | Original draw-geometry capture lifetime and MSVC test fixes advanced; `96d1e27` was reported during the day | Source unit tests are not Windows GPU image confirmation |
| 21 Nov 09:15–09:42 | Original PC menu art in the native menu, direct EoR Vulkan worker selection, complete per-slot live Xbox vertex-stream capture, viewport/scissor reset per indexed draw, deduplicated original mesh search, and USA callgraph duplicate checks, through `8a9bbad85ff78c152a83a2bb7b849cecc97d2d2b` | The Xbox pause page is not redirected into native Settings; source-backed `VerifiedWorkerEorFrame` producer remains absent |
| 21 Nov 09:52 | `2de0aabf670ea1fe26ab30ffd1076b4af0502966` and `15e98540b62825aa272cbff5198386065a0d86c2` correct index-width bounds for original 32-bit guest IB reads and add a negative test | Source change plus committed test; neither an executed Windows test nor a released full port |

### 21 November 10:03–10:10 UTC: continuation, applied to `main`

- `f37d819` / `8748d0c`: the original `UIPage_Pause_Settings`
  Ghidra name search now handles exact ASCII, UTF-16LE and UTF-16BE
  references with per-hit byte-order attribution and synthetic regression.
- `ac19b97` / `ad08e61`: the same xref report now provides direct
  Ghidra call-site/caller-function candidates rather than treating a
  resource/string address as an event-dispatch ABI.
- `f46da44` / `51a4754`: original guest indexed Draw snapshots
  prioritize source VB0 and source index-buffer bytes BEFORE secondary
  bound vertex streams consume the shared immutable per-Present budget;
  all 16 slots are still captured when available. This reduces a known
  ordering-related missing-index-snapshot path, NOT missing engine
  mesh/material shader witnesses.
- `3c6c5b0` / `ff9b49e`: the redacted CI failure classifier now
  retains only the whitelisted `index-pool`/`packed-mip`/
  `spirv-scalar` mode of the source-safe Xenia Edge patch.
  This is needed because the previous public failure showed a
  `ValueError` near repeated uses of that script with **no mode**
  and no underlying private error message.

The previously failed `44886654922` Windows run remains **failed**,
not a test of any of the above commits. Original uploaded Library XEX
files inspected during continuation are **not** SHA-identical to the
pinned USA source `346A7369...`; no addresses or original game content
from them were transplanted into the USA runtime. The appropriate
private source input remains in `AD010/hulk-cloud`, and could not be
materialized into the current development container.

Source-level sanity checks confirmed current source patterns and
exactly matching pinned ReXGlue upstream texture/index patch anchors,
but **no current Windows game executable or new gameplay frame was run**.

### 21 November — original pause UI VM source recovered from real archives

Original supplied Xbox OBJECTS.ARC SHA256 35c9a1d502f62a0fd4e3ef73da204195ead4598f132f21763f66d168d7801815
and PC-2008 OBJECTS(2).ARC SHA256 2d080575a4608f6ebf0fb04f1005a3f8b2ec96233fb4cb6e5e790af7d0d4f7b5
were inspected locally; no copyrighted bytes were published. Both encode the
same ordered page/event route: OptionsMode_Pause, UIPage_Pause_Settings,
OptionsMenuState, Settings_Page, uiwidget_pausemenu_z, ChangeState_r,
Init_z, StateEnter_z and StateExit_z. Both include an independent
uipage_pause_gamesettings_z object with Init_z. QUICKDAT.ARC contains
the adjacent original UI_ActivateOptions and UI_OptionScreen symbols.

Five real Xbox big-endian method lengths and CRC32(ASCII_UPPER(name))
footers agree with the PC little-endian definitions. For
uiwidget_pausemenu_z, ChangeState_r, Init_z, ActivateUpgradeMenu_z,
ActivateCurrentStatusMenu_z, the payload lengths are 51, 13, 263,
58, 15 bytes in BOTH archives. ChangeState_r contains an opaque original
0x28 VM word followed by an operand F7A5B1D1 after byte-order normalization.
This is NOT a verified native PPC function address or caller ABI.

The reproducible private-asset scanner is
ci/audit_hulk_original_pause_ui_objects.py. Its synthetic regression is
ci/test_audit_hulk_original_pause_ui_objects.py and is included in the
existing single manual Windows Release workflow. A source-only JSON witness
resides in native_port/analysis/eor_original_pause_ui_source_witness_2026-11-21.json.
It contains source hashes, symbol offsets and method metadata, never VM code
or raw game assets. Ghidra's existing ci/ghidra_hulk_ui_xrefs.py now searches
these original VM identity CRCs and candidate PPC lis/ori immediate loads.
Any resulting source address remains a CANDIDATE pending decoded USA XEX
guest VM dispatcher validation and real PPC ABI reconstruction.

The user's Library default.xex, default(1).xex and bruce.xex are NOT
SHA-identical to pinned USA XEX 346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0.
They were not used for USA hook addresses. A previously released native
Windows executable also contained no contiguous literal word matching
the identified UI VM method CRCs or opaque ChangeState operand.

Separately, main commits 1bcb604 / e3df028 reject a real captured
indexed original Xbox vertex/index allocation that contradicts an EoR
mesh, even if a single possible material witness exists. No producer
of complete authentic Vulkan frame Draw witnesses or original guest
Pause Settings event hook was established, and no new Windows Release
or gameplay-tested ZIP was created by this investigation.

---
### 21 November later continuation — actual original VM graph and draw capture budget

Read-only investigation of the owner's actual Xbox OBJECTS.ARC
(SHA256 35c9a1d502f62a0fd4e3ef73da204195ead4598f132f21763f66d168d7801815)
and independent PC OBJECTS(2).ARC
(SHA256 2d080575a4608f6ebf0fb04f1005a3f8b2ec96233fb4cb6e5e790af7d0d4f7b5)
showed the bytecode opcode+operand 28 F7A5B1D1 occurs **314 times** in
Xbox and **303 times** in PC, including unrelated HUD/frontend/gameplay
scripts. It is a globally reused VM invocation operand, NOT a unique
Pause Settings event/callback address. The Ghidra candidate filter now
excludes this immediate from alleged pause-specific PowerPC hooks.

Exact uppercase-CRC32 **field** references inside original pause object
scripts were independently reconstructed in both source byte orders:
Init_z **13**, StateEnter_z **25**, StateExit_z **2**,
ActivateUpgradeMenu_z **1**; **all 41 names and relative VM byte offsets**
match Xbox BE and PC LE. Notable genuine source field IDs include
inputDeviceListener_Pause 3C13623E, UISystem_Pause_CurrentStatus
D8A1E3F5, SetCanReturnToCity_b F615984D.
These identify original script references, not Windows PPC function args.
Exact whitelisted reference metadata is in the original pause UI JSON
witness file and script `ci/audit_hulk_original_pause_ui_objects.py`;
synthetic source-only regressions were added. No raw VM instructions or
proprietary game payloads were committed.

Original XDK per-Draw capture also received:
- 4204049 / e7c810a: release per-Present allocation budget when a dynamic
  guest buffer's replaced immutable revision has no captured Draw owners;
  keep budget charged while earlier Draws still own their old revision.
- 0285225 / 8d7f4de: treat two endian interpretations of an identical
  physical 16-bit guest index window as one source mesh; still reject
  duplicate resident model owners or distinct source geometry.

**Still unavailable:** source-proven USA guest VM dispatcher address/PPC
ABI and an end-to-end live producer supplying a FULL verified engine
model/surface/effect/shader/sampler owner for each original USA XDK Draw.
Private pinned USA default.xex and the latest release-only diagnostics
are present as access-controlled GitHub release assets but raw asset bytes
could not be retrieved through the current connected API. The Library XEX
copies do not match the USA source SHA. No Windows Release was launched;
no fully native gameplay frame or original Xbox Pause Settings handler
has been verified in the actual running game.

---
### 21 November — original USA decrypted PPC code and ShaderClass runtime ownership

The owner's original 5.87 GB Windows crash dump, recovered from their
previously provided six-part private 7z, contains a fully mapped
Xbox-360 PPC executable image at guest base 0x82000000.
**Private-only** mapped PE SHA-256:
`fa1d06e634146318c994678dc7cc1d770ac158e7292d07b30dcaf573a0964386`.
The loaded executable is PPC PE machine 0x1F2, 0x140E200-byte image,
entry 0x824C6158; genuine DrawIndexedVertices 0x8288E318 and
DrawVertices 0x8288DF30 original PPC prologues match the
SHA-pinned USA 98,726-function AOT diagnostic report. Neither the
private dump nor the original mapped instructions were committed.
Linux LLVM PPC disassembler can now decode the original source in
private local analysis instead of looking only at European XEX copies.

The original source .rdata supplied **52 named EoR shader class
virtual method tables** (recorded as addresses/names ONLY in
`native_port/analysis/hulk_usa_eor_shader_vtables_2026-11-21.json`).
The source-only verification command is
`python ci/hulk_usa_eor_shader_vtables.py --private-usa-mapped-image PRIVATE.bin --output PRIVATE_REPORT.json`.
Independent real guest heap instances contain those exact class-vtable
pointers, including Decal, TexturedRect, EScreenRectShader, EFontShader,
ESkyShader and EFlatShadowShader. Original object first word is the
BE original guest vtable; six original slot-4 methods can be
verified without treating unrelated PPC LR or shader hashes as
engine model ownership.

The existing single generated AOT hook unit now has **six source-only
shader-method wrappers** separate from real XDK hook methods; their
guest-to-native AOT mappings and original method addresses have been
added to the existing mapping and hook allowlists. Every wrapper
calls the original PPC implementation unchanged. A Windows
checked-memory vtable check and bounded host thread-local RAII
scope capture the authentic original shader instance and original
r4..r7 values before a nested XDK Draw, then restore the prior
scope at method return. A wrong/unreadable vtable suppresses
attribution; no guest memory or proprietary bytes are modified.

That exact per-Draw source-parent snapshot flows via
`RawDraw::original_engine_shader_method` and
`AttachLiveEorShaderMethodParents` into the **existing sole EoR
Vulkan worker**. The original XDK command timeline and material
resolver reject a candidate whose asserted engine shader parent
does not match the same guest Draw. The per-Present log now counts
source-parented original draws and original EFontShader draws.
Source-only regression checks are added to the existing manual
Release workflow; Release was NOT launched.

IMPORTANT: This closes a real portion of the original engine
shader class -> XDK Draw provenance chain, but does NOT yet
identify every model/mesh/surface, effect/pass and original shader
sampler slot or supply a finished VerifiedXdkResourceWitness for
all Draws. Nor does EFontShader class discovery prove the original
UIPage_Pause_Settings guest VM event dispatcher or its PPC ABI.
No authentic independent end-to-end EoR/Vulkan gameplay frame
and no playable Windows ZIP have been verified yet.

---
### 21 November — original Decal item → material descriptor → XDK geometry

The PRIVATE owner-mapped USA PPC guest image supplies direct PPC ABI
instructions at the original engine method `0x8265B420` (Decal).
At source site 0x8265B4DC the real material render item is saved in r28;
at 0x8265B4E0 the original engine calls `0x82642C68` to prepare
the CURRENT material. `lwz r11,60(r28)` obtains exact original guest
geometry owner. The engine calls `SetStreamSource(0x8287E650)` with
r5 pointing to `geometry+24`, then `SetIndices(0x8287E7F8)` with
r4 pointing to `geometry+56`, before original guest
`DrawIndexedVertices(0x8288E318)` at direct `bl 0x8265B538`
(runtime return LR `0x8265B53C`).
Independent original PPC code from those XDK implementations confirms
the vertex descriptor's device shadow starts at `device+12460`,
the index descriptor is stored at `device+12436`, and those are
exactly what the original USA `CaptureUsaVsMatrices` captures per Draw.

Original material preparation PPC in `0x82642C68` proves:
`r3` on entrance is the current Decal shader, `r4` the original
render item; `source shader+48` is the render/material context;
the render item's original virtual method at vtable slot `+48`
returns a source material descriptor in `r3`, transferred to `r4`
before calling ORIGINAL native material setter `0x825F8A50`
at guest `0x82642CB0` (runtime return LR `0x82642CB4`).
The original setter reads BE32 descriptor+0 and descriptor+8.
The preceding default-material call at `0x82642C90`
returns LR `0x82642C94`; a read-only observer explicitly resets
prior per-item source state at that point, avoiding stale reuse.

**Implemented** in source-only main: separate generated original
shader-class and material-setter raw PPC hooks; original game
functions called unchanged; checked original guest r28/r31/r27,
vtable identity, source material context, original material descriptor
and actual device-bound vertex/index buffer object identity
are copied into immutable per-Draw `RawDraw` ownership. Original
XDK command timeline and Vulkan resource-witness crosswalk reject
invalid or cross-frame source owners. Per-Present diagnostic line
counts source-parented, original Decal geometry and original
item-material descriptor parented Draws.

Original PRIVATE source reproducibility script:
`ci/hulk_usa_decal_guest_geometry_abi.py` checks **31 source PPC
instructions/entry/callsite facts** spanning engine Decal,
per-item material virtual method/descriptor, original XDK setter
and complete indexed Draw. A synthetic negative regression test and
the sanitized JSON `native_port/analysis/hulk_usa_decal_geometry_abi_2026-11-21.json`
are checked into the existing manual workflow. No original decoded
instruction bytes, script payloads, extracted textures, XEX or original
proprietary archives have been published.

**Not established**: full ShaderEffects effect/hash, pass/VS/PS
digest identity, original sampler slot/fetch identity, source
model/surface ownership for EVERY original Draw, original UIPage_Pause_Settings
VM page-specific dispatch and a fully presented independent Vulkan
gameplay scene. The material descriptor is a proven ORIGINAL GUEST
pointer, NOT a proven native shader effect or runnable texture pipeline.
The new Windows code has NOT been compiled or runtime-tested;
no Windows ZIP/Release has been dispatched.

---
### 21 November — original USA VS/PS, sampler and XDK Draw source ABI

The owner-private original USA `SHADEREF.ARC` (source index
`indexXBOX360.ind`) contains 233 ShaderEffects objects, 231 of
which contain authored programs: **2,597 unique original
technique/VS/PS pairs**, **5,194 unique stage programs** and
**4,420 original sampler declarations** across 1,306 programs.
The original source/PDB effect technique catalog is strongly
identified, but it is NOT equivalent to the original guest runtime
per-Draw ShaderObject or texture object ownership.
Only 46 of the 52 source-proven EoR shader classes have a
uniquely matched source EFFECT NAME; this is a source name candidate,
not a verified runtime class-to-effect association.

User-private coherently generated caches include HEORMAT2 (effect
pass identities), HEORREF1 (original matrix register reflection),
HEORSMP1 (original sampler declarations), and HEORVRT1 (unique
original Xenos virtual-header SHA256 -> full original shader source
SHA256). The last bank contains exactly **5,194 non-colliding
72-byte records**, private SHA256
`82064e54dd6ca98796b08cc92b1f7063321d8e4ca4386bfe861e4569d54f22cd`;
its 5,194 header/full source hash pairs and stages were independently
checked against ALL owner-original source ShaderContainers.
No copyrighted shader bytes or original guest game code were committed.
The 5,194-entry HEORVRT1 bank has a C++ SHA256 source identifier
and the game now loads and lifetime-pins it atomically with the
original EffectCatalog, shader reflection and sampler bank.

The previously missing **original EoR runtime material-pass
VS/PS XDK shader-object chain** was found in SHA-pinned original USA
PowerPC entry `0x825F84C0` (native AOT 0x001419FCDC0).
Original material context `r3`, 1-based state `r4`:
the state records are 92 bytes at BE32(material+112),
VS/PS byte indices at record+84/+85, original stage handle
tables at BE32(material+124/+136) with eight bytes per entry.
Original source functions `0x82675C70` (VS) and
`0x82675D20` (PS) pass the selected guest ShaderObject
pointers to original XDK `0x82874390` (SetVertexShader)
and `0x82874090` (SetPixelShader), which write the
current VS/PS into device shadow +12696/+12692, already
captured by the native EoR XDK Draw observer.
The new generator emits an unchanged original PPC call-through
hook for the source EoR state selector. During a live original
shader-class call it snapshots exact original stage handles and
only tags them verified if BOTH match the current Draw's actual
bound XDK ShaderObject guest pointers.

Continuing at the same exact source function, +80 on the 92-byte
pass record is a BE16 sampler-state record start index, +87
is the entry count; material+196 supplies the four-byte per-record
sampler STATE table, register slot = byte0 & 0x7F.
Original calls `0x82675E00/0x82675EA0` set original sampler
state values. The exact state source slot mask is attached to
the original source stage record and compared against nonzero
actual XDK device+12544 original texture object pointers
at the SAME Draw. These slots represent sampler STATE changes;
they are not yet proven all live shader fetch registers or
actual source texture hashes/mips/sampler SRVs.

**29 genuine original USA PPC source instructions/branch sites**
for this dual-stage pass and sampler-state pipeline were verified
against the user's decrypted original PPC binary. A mistaken
byte3 load site was corrected from 0x825F85BC (register move)
to the real 0x825F85B8 original `lbz` before recording the
passing 29/29 original source instruction audit.
Public safe source scripts and sanitized proof outputs:
`ci/hulk_verify_usa_original_shader_effects.py`,
`ci/hulk_eor_virtual_shader_identity.py`,
`ci/hulk_usa_eor_pass_stage_abi.py`, plus CI tests.
The existing manually dispatched SINGLE Windows workflow now
runs synthetic private-manifest SHA collision checks and MSVC
native C++ virtual shader header digest tests. No full Windows
Release build was dispatched to avoid publishing an unfinished
gameplay executable.

**Remaining proof gates:** (1) original currently bound XDK
ShaderObject -> its exact full virtual metadata header; this
link is essential before the original source program digest
can be resolved on a real Draw, (2) mapping one-based source
runtime material state to original ShaderEffects authored
technique index and exact model/surface material, (3) original
sampler fetch register -> genuine Xbox GPU texture/image format
and ARC provenance for every original Draw, (4) source-verified
camera, depth/resolve/composite and fully translated/validated
SPIR-V binaries, (5) unique original
`UIPage_Pause_Settings` page owner/VM event dispatcher ABI
and connections to the native Windows settings, (6) a Windows
runtime end-to-end fully native Vulkan gameplay Present.
The original common `state_r` callback is NOT a unique pause
menu callback. Do not open Windows Settings on it alone.

---
### 21 November — 52 shader classes → original render item → virtual material

The SHA-pinned ORIGINAL USA PPC binary proves general source engine
renderer `0x82642A88` traverses original render items. At
`0x82642AF8` it passes `r28` as original render-item `this`,
loads this item's own vtable+48 method, executes it at
`bctrl 0x82642B08`, then saves the returned material source
pointer from `r3` into callee-saved `r27` at `0x82642B10`.
The shader renderer subsequently dispatches its own original
vtable+44 method at `bctrl 0x82642BD4` with exact guest PPC
`r3=shader instance (r31)`, `r4=source render item (r28)`,
`r5=original rendering context (r23)` and genuine guest
return LR `0x82642BD8`. **r27 at this bctrl is the
item's original virtual+48 material return**, not a guessed
ShaderEffects hash.

Source verified directly from the owner's original PPC binary:
**17/17 exact original PowerPC instruction/ABI checks**,
**52/52 named shader class vtable+44 method mappings**,
**22 unique original render-item PPC methods**; 27 shader
classes share the original `0x82642370` guest item method.
All 22 are mapped to authentic USA AOT host functions and
added to the existing guest call-through hook allowlist.
`ci/hulk_usa_shader_render_item_dispatch_abi.py` reconstructs
the 17 instruction checks privately. Its output contains no
game program bytes or proprietary asset data.

`ScopedOriginalEorRenderItem` obtains this exact original source
shader, render item, render context, class vtable and, when
guest memory is accessible, the original material pointer
returned by the item's own virtual getter. Source validation
rejects wrong original `r31/r3`, `r28/r4`, `r23/r5`,
wrong guest LR, wrong known shader vtable/method pair,
invalid item class vtable, and invalid material method.
RAII preserves original guest function behavior, clears stale
per-item state at sibling entry and restores only into the
same source shader instance, without guest writes.
The complete XDK Draw timeline / Vulkan worker witness now
compares all source ownership fields before GPU replay.
Per Present diagnostics distinguish source shader-class draws,
true per-item-owned draws and source-returned material-owned
draws. Unverified instances never become owner evidence.

This is a concrete part of model→material provenance, **NOT**
a recovered archive model hash, surface ordinal, per-material
Xenos source SHA digest, full sampler/image fetch owner,
original UIPage_Pause_Settings event dispatcher,
nor a rendered verified native Vulkan gameplay frame.
The new C++ changes have not been compiled and replayed
on the user's Windows notebook, and no unfinished Windows
Release ZIP was launched.

---
### Current verified path and exact missing producer

The existing source contains `CapturePpcDraw` → immutable XDK
`CompletedFrameMailbox` → `TryWorkerEorFrame` →
`ReplayNextVerifiedPpcFrame` → `ReplayDeliveredResourceScene` →
`ReplayAotScene` → native Vulkan backend. However
`InstallVerifiedWorkerEorFrame` / `InstallVerifiedWorkerEorFrameWithResidents`
require an **independent source-authenticated original engine producer**
to supply every model/mesh/surface/effect-pass/VS/PS/camera witness for the
same original Present. XDK objects, callsite LR and indexed archive names
alone do not prove these bindings; no full real-world frame should be
declared successful until that producer exists and runs.

`UIPage_Pause_Settings` is still only a real game/UI resource identifier
and a Ghidra xref analysis target. The separate F10 PC ImGui menu persists
working Windows options but has **not** intercepted the game's actual
Pause Settings selection, original guest event ABI, focus and return path.
`ci/ghidra_hulk_ui_xrefs.py` and the original-PC/Xbox UI asset index
are the source-safe starting evidence, not proof of a callable USA hook.

### 21 November Windows Release evidence

The latest observed manual workflow run before these two source changes,
[44886654922](https://github.com/AD010/Hulk-NativePC-Source/actions/runs/44886654922),
ended in **failure** at “Compile Vulkan NativeXRenderer Hulk under Windows”.
The public job reports exit 1 and points to private diagnostics; it does not
expose a reliable MSVC error in the public log. Private
`Hulk-PrivateRunner-Diagnostics.zip` was updated at 08:55:58 UTC, but its
contents were not inspected in this continuation. **No executable/ZIP from
the 09:52 commits has been built, published or gameplay-tested.**

Next implementation must prioritize the *actual* USA engine EoR Draw owner
and pause UI dispatch ABI; no new repository, workflow, speculative guest
offsets, or public redistribution of proprietary XEX/ARC/TU is authorized.
Preserve the existing manual-only Release workflow and the private
`AD010/hulk-cloud` resource boundary.

---

## What was actually checked

- **72 accessible uploaded log files** (`incredible_hulk_*.log`) dated 2026-11-07 to 2026-11-15; **67 distinct byte streams** after SHA-256 deduplication. Every line was scanned for a fixed diagnostic category set. This is not an exhaustive human reinterpretation of every individual event.
- **All 916 GitHub commit metadata entries checked initially, plus 2 subsequent new commits (918 total)** returned by the repository history through 2026-11-16 (`main`); this is not a diff-level audit of all 916 patches.
- Two RenderDoc captures `render.rdc` and `render2.rdc` were uploaded 2026-11-08 and audited 2026-11-09. They are not evidence of current HEAD output.
- Earlier conversation context and saved engineering reports were cross-referenced. A complete literal export of every ChatGPT message is not available here.
- The log timestamps do not include a UTC offset; they appear consistent with local Europe/Kyiv wall-clock time, but this is not embedded proof of their timezone.

## GitHub commit chronology (UTC)

| Day | `main` commits in history |
|---|---:|
| 2026-11-07 | 11 |
| 2026-11-08 | 73 |
| 2026-11-09 | 38 |
| 2026-11-10 | 116 |
| 2026-11-11 | 70 |
| 2026-11-12 | 87 |
| 2026-11-13 | 144 |
| 2026-11-14 | 226 |
| 2026-11-15 | 136 |
| 2026-11-16 | 17 |

**Last available gameplay log:** `incredible_hulk_213.log`, within-file timestamp `2026-11-15 16:38:50`–`16:43:03` (apparently local Kyiv), uploaded at 14:44:10 UTC.
**70 main commits were authored after the latest recording ended** (53 late 15 Nov and 17 on 16 Nov). Latest observed HEAD: `46915b13671430b5280024d288599fa6ee4fa065` (2026-11-16 10:26:12 UTC).
**Latest observed successful Actions run:** `43724703789`, source SHA `48c3f9e62d5e40e4ddc54141f0fc15d6ed02482d` (started 2026-11-15 14:30:20 UTC). No newer run was observed; therefore no current HEAD Windows runtime validation is established.
**Unmapped version:** the last two game logs do not independently establish an exact Git commit SHA. Do not attach them to a specific CI run without verifying the binary build identity.

## Concurrent-writer safety finding

While preparing this report, `main` advanced independently from `787f10bd` to `c5c8d96e` (PC 4:3/HUD safeguard, 10:24 UTC) and `46915b13` (Xbox IND to PC metadata bridge, 10:26 UTC). This audit did not create those commits. **Another writer may be modifying the same branch.** Do not infer whether the writer is a chat, schedule, human, or other process without further evidence. Before committing, re-fetch HEAD and use an `expected_sha`/compare-and-swap update; never force push or rebase over unexpected changes.

## Most recent logs: observed diagnostics, NOT current confirmed faults

| Log | Internal time window | Invalid vertex sample lines | Invalid texture sample lines | Incomplete XDK sample lines | Largest cumulative incomplete draw marker |
|---|---|---:|---:|---:|---:|
| incredible_hulk_211.log | 14:25:57–14:28:37 on 2026-11-15 | 20 | 7 | 22 | 60,000 |
| incredible_hulk_212.log | 16:36:09–16:38:42 on 2026-11-15 | 8 | 2 | 18 | 20,000 |
| incredible_hulk_213.log | 16:38:50–16:43:03 on 2026-11-15 | 20 | 14 | 21 | 50,000 |

> **Critical:** `#N` is often a throttled *cumulative sequence number*, not the count of warning lines. Different captures have different play durations/scenes/options; do not interpret count differences as a verified GPU regression or improvement.

The latest logs still contained shader translation errors involving `getCompTexLOD` and real Vulkan presents. The subsequent source changes MAY have altered these behaviors, but no newer in-game observation exists.

## Release integrity and decisions

1. **Historical evidence gate:** never treat a `render.rdc`, log, screenshot or saved analysis as a live report after a newer code revision. Each source observation must have both capture date and matching executable/revision identity; otherwise mark it historical/unmapped.
2. **Already implemented, not automatically game-verified:** C++ geometry parser, independent material candidate parser, native upload contract, Diligent buffer adapter, optional LOD policy and asynchronous original archive worker. A compiled object and passing synthetic test are not real game Draw invocation or a proven world-stream patch.
3. **Known blockers not yet proven solved in live HEAD:** original USA game Draw provenance, mesh→surface/pass/material/shader PSO correspondence, color/depth/resolve/texture correctness, native camera ABI, Havok fixed tick under unlocked present, real engine-controlled city LOD, and GPU/device lifetime on Alt+Tab.
4. **Regression risk controls:** no speculative PM4 header reinterpretation, no blanket shader/mip/LOD/resolve dimension override, no duplication of native upstream caches. Preserve existing single manual Release workflow and public-source/private-game-data separation.
5. **Build gate:** run one user-authorized Windows x64 Release only after source-level compatibility and complete expected artifact checks. A `success` build is a compile outcome, not gameplay sign-off. Do not ask the user to check every invisible intermediate change.
6. **Next user-supplied video:** original MP4 60 FPS preferred. Frame-reference visible flashes, black/green triangles, LOD transitions while moving, HUD/3D menu, Alt+Tab and FPS overlay; correlate exact time offsets with matching log and binary commit. A video alone does not reveal the underlying GPU descriptors/driver faults.

## Chronological index of every accessible log

| Date (within log) | Filename | First–last time | Error-tag lines | Invalid VB / texture sample lines | Incomplete-XDK sample lines | Max XDK sequence | Identical to |
|---|---|---|---:|---:|---:|---:|---|
| 2026-11-07 | `incredible_hulk_042.log` | 11:12:22–11:12:22 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_043.log` | 11:12:28–11:12:29 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_044.log` | 11:12:52–11:12:52 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_045.log` | 11:14:00–11:14:00 | 3 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_046.log` | 11:15:38–11:15:38 | 3 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_047.log` | 11:17:26–11:17:27 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_048.log` | 11:27:38–11:27:39 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_050.log` | 15:36:39–15:37:10 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_051.log` | 15:37:15–15:37:16 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-07 | `incredible_hulk_052.log` | 15:43:25–15:46:06 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_077.log` | 07:56:59–07:57:49 | 3 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_078.log` | 08:02:40–08:04:25 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_079.log` | 09:17:16–09:18:36 | 1733 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_085.log` | 11:35:49–11:35:59 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_087.log` | 11:41:56–11:42:36 | 57 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_088.log` | 11:47:03–11:49:02 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_099(1).log` | 14:25:15–14:25:42 | 3 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_099.log` | 14:25:15–14:25:42 | 3 | 0 / 0 | 0 | 0 | incredible_hulk_099(1).log |
| 2026-11-08 | `incredible_hulk_100.log` | 14:26:26–14:32:16 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_101.log` | 17:15:08–17:15:37 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_115.log` | 18:27:16–18:28:53 | 364 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_116.log` | 18:30:09–18:31:16 | 177 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_119.log` | 19:03:39–19:05:38 | 343 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_122.log` | 19:27:26–19:29:24 | 383 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_124.log` | 20:32:52–20:35:00 | 402 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_130.log` | 21:19:21–21:21:09 | 238 | 0 / 0 | 0 | 0 | — |
| 2026-11-08 | `incredible_hulk_131.log` | 23:00:54–23:03:17 | 404 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_137.log` | 12:57:27–12:58:58 | 255 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_145.log` | 15:51:24–15:51:36 | 4 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_146.log` | 15:53:25–15:56:13 | 192 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_149.log` | 17:15:41–17:18:47 | 526 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_153.log` | 19:12:14–19:13:33 | 499 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_158.log` | 20:47:08–20:48:22 | 344 | 0 / 0 | 0 | 0 | — |
| 2026-11-09 | `incredible_hulk_160.log` | 21:45:38–21:47:42 | 294 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_167.log` | 08:27:37–08:32:38 | 2126 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_170.log` | 10:58:39–11:01:34 | 435 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_171.log` | 11:32:45–11:36:13 | 465 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_172.log` | 12:22:26–12:26:15 | 656 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_173.log` | 18:09:58–18:13:07 | 687 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_174(1).log` | 21:12:33–21:16:23 | 662 | 0 / 0 | 0 | 0 | — |
| 2026-11-10 | `incredible_hulk_174.log` | 21:12:33–21:16:23 | 662 | 0 / 0 | 0 | 0 | incredible_hulk_174(1).log |
| 2026-11-10 | `incredible_hulk_175.log` | 21:38:08–21:41:10 | 319 | 0 / 0 | 0 | 0 | — |
| 2026-11-11 | `incredible_hulk_176(1).log` | 10:06:19–10:09:07 | 561 | 0 / 0 | 0 | 0 | — |
| 2026-11-11 | `incredible_hulk_176.log` | 10:06:19–10:09:07 | 561 | 0 / 0 | 0 | 0 | incredible_hulk_176(1).log |
| 2026-11-11 | `incredible_hulk_177.log` | 13:03:13–13:04:06 | 5 | 0 / 0 | 0 | 0 | — |
| 2026-11-11 | `incredible_hulk_178.log` | 21:13:59–21:15:02 | 181 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_179.log` | 11:44:12–11:58:40 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_180.log` | 12:07:26–12:07:43 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_181.log` | 12:08:26–12:10:20 | 2 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_184(1).log` | 13:17:59–13:19:40 | 196 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_184(2).log` | 13:17:59–13:19:40 | 196 | 0 / 0 | 0 | 0 | incredible_hulk_184(1).log |
| 2026-11-12 | `incredible_hulk_184.log` | 13:17:59–13:19:40 | 196 | 0 / 0 | 0 | 0 | incredible_hulk_184(1).log |
| 2026-11-12 | `incredible_hulk_185.log` | 15:34:19–15:40:10 | 617 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_189.log` | 19:11:47–19:11:48 | 1 | 0 / 0 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_190.log` | 19:13:25–19:14:06 | 1 | 20 / 20 | 0 | 0 | — |
| 2026-11-12 | `incredible_hulk_191.log` | 21:31:19–21:31:40 | 336 | 0 / 0 | 16 | 16 | — |
| 2026-11-12 | `incredible_hulk_192.log` | 22:24:17–22:28:16 | 3 | 20 / 2 | 20 | 40,000 | — |
| 2026-11-13 | `incredible_hulk_194.log` | 08:34:19–08:41:01 | 8 | 20 / 14 | 29 | 130,000 | — |
| 2026-11-13 | `incredible_hulk_195.log` | 13:37:04–13:41:45 | 8 | 20 / 8 | 21 | 50,000 | — |
| 2026-11-13 | `incredible_hulk_197.log` | 14:58:25–15:01:11 | 6 | 20 / 11 | 19 | 30,000 | — |
| 2026-11-13 | `incredible_hulk_198.log` | 17:14:44–17:17:19 | 2 | 20 / 6 | 19 | 30,000 | — |
| 2026-11-13 | `incredible_hulk_200.log` | 21:55:05–21:57:48 | 2 | 11 / 1 | 18 | 20,000 | — |
| 2026-11-14 | `incredible_hulk_201.log` | 11:11:01–11:15:29 | 7 | 20 / 4 | 30 | 140,000 | — |
| 2026-11-14 | `incredible_hulk_202.log` | 13:17:49–13:20:12 | 1 | 3 / 0 | 16 | 16 | — |
| 2026-11-14 | `incredible_hulk_203.log` | 16:31:10–16:33:53 | 9 | 20 / 8 | 22 | 60,000 | — |
| 2026-11-15 | `incredible_hulk_205.log` | 12:43:03–12:52:04 | 8 | 20 / 8 | 41 | 250,000 | — |
| 2026-11-15 | `incredible_hulk_207.log` | 12:59:15–13:06:06 | 8 | 20 / 7 | 33 | 170,000 | — |
| 2026-11-15 | `incredible_hulk_208.log` | 13:17:47–13:20:05 | 7 | 5 / 0 | 18 | 20,000 | — |
| 2026-11-15 | `incredible_hulk_210.log` | 14:05:30–14:07:44 | 9 | 20 / 5 | 18 | 20,000 | — |
| 2026-11-15 | `incredible_hulk_211.log` | 14:25:57–14:28:37 | 8 | 20 / 7 | 22 | 60,000 | — |
| 2026-11-15 | `incredible_hulk_212.log` | 16:36:09–16:38:42 | 6 | 8 / 2 | 18 | 20,000 | — |
| 2026-11-15 | `incredible_hulk_213.log` | 16:38:50–16:43:03 | 7 | 20 / 14 | 21 | 50,000 | — |

## Evidence limitations

- All dates of message receipt, contents of every prior ChatGPT conversation, and every old screenshot are not available as a single unabridged export. The original 916 commit entries were reviewed as repository *metadata*; two newer entries were then observed. None of these 918 entries constitutes a diff-level audit of every patch.
- Raw log category counts are diagnostic, frequently throttled, and do not establish image correctness; `Window closing, shutting down` / `hard-exiting` do not necessarily prove gameplay stability.
- Current source check and user video/game footage are required to determine whether an apparent old regression remains. Do not invent an updated GPU result from a stale capture.

### 2026-11-21 · Original shader and texture replay

In the USA process dump, 2,405 vertex shader objects and 2,405 pixel shader objects matched the original shader archive by their embedded virtual headers. Their 2,405 pairs matched passes in 187 effects, including Font. The header starts at VS object +872 or PS object +40. The physical program-data pointers are separate.

The native texture loader now accepts the USA single-level resident LFXT schema. Offline replay decoded 538 source textures, including 391 with `ui_` or `font_` names. For the loading logo (`995AD5DC`) and font atlas (`DAD1DD22`), each 524,288-byte tiled allocation matched the original archive and the captured process memory byte for byte. Both decoded to 512 × 256 at a 2,048-byte row pitch. An independent in-game frame, stable lighting and original settings-page dispatch still require runtime verification.

The indexed `uiwidget_pausemenu` widget references the actual settings object (`D7A9E027`) in its 225-byte `StateEnter_z` method at object offset 13,920. The settings object references its game-options child (`04AA3A30`). Both `IncreaseEvent_z` and `DecreaseEvent_z` call the game's registered yaw and pitch setters. The child reads both values in `Init_z`. The setter/getter pairs agree on the two USA option bytes. The direct mouse-camera path reads these flags from game memory after checking the original getters. This change applies when a direct camera profile is active. Additional Windows display controls and runtime rendering of this page remain unverified.

### External navigation

- https://github.com/AD010/Hulk-NativePC-Source/commits/main
- https://github.com/AD010/Hulk-NativePC-Source/actions
- https://github.com/AD010/Hulk-NativePC-Source/blob/main/native_port/docs/HULK_WORLD_POPIN_LOD_2026-11-12.md
- https://github.com/AD010/Hulk-NativePC-Source/blob/main/native_port/docs/HULK_D140_LOG201_VISUAL_PERFORMANCE_2026-11-14.md
- https://github.com/AD010/Hulk-NativePC-Source/blob/main/native_port/docs/HULK_NATIVE_EOR_INDEXED_VERTEX_STREAMS_2026-11-15.md
### 22 November — USA archive reader and offscreen Vulkan readback

The earlier pause-object analysis used an index with SHA-256 prefix 46e6c6ec.
The canonical USA index has prefix ad22432b. Its 3,938 Objects and 6,669
Textures entries agree with the earlier index on hash, offset, size and
reserved field. The pause-page check now requires the complete USA digest.
The model and texture loaders authenticate and decode the same byte snapshot.

The offscreen integration test loads the USA logo and font through the runtime
texture cache. A staging readback matched all 524,288 decoded bytes of each
512×256 BGRA atlas on Mesa llvmpipe and SwiftShader. The test needs private
USA inputs. In the existing native_port/tests CMake project, set
HULK_DILIGENT_CORE, HULK_USA_INDEX and HULK_USA_TEXTURES, build
hulk_original_ui_vulkan_test, and run ctest -R original_ui_vulkan.
