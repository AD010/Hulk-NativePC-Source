# The Incredible Hulk (2008) — original EoR UI: PC / Xbox 360 source audit
Date: 2026-11-14
Status: **verified original-file inventory and static metadata**, not a working in-game menu patch

## Source and method

Private user-supplied source material, examined locally without committing
game bytes: Windows retail `HULK.EXE` (4,709,888 bytes), Windows
`datasets.arc` (340,105,664 bytes), `indexPC.ind`, `indexXBOX360.ind`,
and three previously extracted Xbox 360 dataset resources (`_app_`,
`_engine_`, `_shader_effects_`). The Xbox 360 `_app_` resource is
5,580,632 compressed bytes. Decoded **320** ZIP-local / raw-deflate blocks
into **20,941,454** bytes; the final **6,016 bytes remain unexplained** and
are not assumed to be decoded. The corresponding PC `_app_` record,
hash `0x721AAFDE`, is **4,791,770 bytes** at offset 144,711,680 in
PC `datasets.arc`.

Direct, bounded ASCII name/metadata extraction, PC executable strings and
actual resource index decoding; no runtime interception, guessing of
PPC addresses, undocumented game-code upload or private assets in Git.

## Verified shared frontend/menu concepts

The following *identical named metadata* exists in decoded `_app_`
from **both** platforms:

- `UIPage_Pause_Settings`, `uipage_pause_gamesettings`,
  `uipage_pause_camerasettings`
- `UI_ActivateOptions`, `UI_OptionScreen`, `UI_OptionsReturn`
- `uiwidget_menuselector`, `uiwidget_menuitemtxt`,
  `uiwidget_menuscrollbar`
- `bAudioOptions`, `iMusicVolume`, `iSFXVolume`,
  `iDialogVolume`
- `font_clean` and source metadata referring to
  `data\\bruce\\raw\\fonts\\font_clean.fdf`

Both datasets include menu asset references such as
`UIWidget_MenuItemTxt.ma` and `UIPage_Pause_CameraSettings.ma`.
Those are authored source-path references in serialized metadata;
they do **not** establish that loose `.ma` files are deployed.
PC dataset uses `TXFL` texture naming, Xbox 360 `LFXT`;
asset payloads and byte order are not interchangeable.

PC `_app_` explicitly contains PC prompts/assets:
`TXFLui_key_wasd_da`, `TXFLui_key_space_da`,
`TXFLui_mouse_leftbutton_da`, `TXFLui_mouse_wheelforward_da`,
`TXFLui_menuitems_da`, `TXFLui_menubands_da`.
Xbox 360 `_app_` references Xbox controller pictograms including
`LFXTui_xbox360_a_da`, `LFXTui_xbox360_rt_da`.

## Existing PC engine API evidence

The retail `HULK.EXE` contains named script-method metadata for actual
options: `Options_SetAudioVolumeMusic_i`,
`Options_SetAudioVolumeSFX_i`,
`Options_SetAudioVolumeDialog_i`,
`Options_SetSubtitles_b`,
`Options_SetMouseSensitivity_i`,
`Options_SetCameraYaw_b`, `Options_SetCameraPitch_b`,
`SetResolution_ii`, `GetNextResolution_z`, `GetPrevResolution_z`.

It also contains `EIUIWidget2DMenu`, `EIUIWidgetText`, `EFontData`,
`EFontPage`, `EFontShader`, `EIUIManager`, `EIUISubtitlesWidget`.
These are evidence for real authored UI handlers/reflection and font
rendering in the existing PC EoR engine, **not** a proved native export ABI
nor confirmed valid Xbox-USA PPC entry addresses.

## What the current Hulk NativePC source actually implements

`native_port/src/pc_menu.cpp` is an **independent ImGui F10 host
overlay**. Its recent seven-section themed UI does NOT inject options
into `UIPage_Pause_Settings`; nor is it evidence that the actual
Xbox game's UI scripts are editable or that original font/textures
are bound to the Vulkan renderer. The existing
`native_port/analysis/guest_host_mapping.csv` is principally a short
set of known XDK GPU hook candidates. The UI option command dispatch,
UI widget lifetime, event handlers and Xbox controller-to-PC glyph
selection are not identified in that AOT mapping.

## Asset inventory: available vs absent from current ChatGPT Library

**Already available (do not ask the owner to re-upload):**
retail PC `HULK.EXE`; PC `datasets.arc`; PC/Xbox
`index.ind` variants; Xbox `default.xex` plus the vetted
private USA original input in CI; Xbox `_app_`, `_engine_`,
`_shader_effects_` extracted resource payloads; Xbox
`OBJECTS.ARC`, `QUICKDAT.ARC`, `QDMETADA.ARC`,
`PCLEFFEC.ARC`, `SHADEREF.ARC` plus earlier PPC diagnostics.

**PC retail 2008 originals supplied on 2026-11-14 by the owner:**
- `FMODPROJ.ARC` (1,279,360 bytes) — audio-project resources
- `FONTS(1).ARC` (152,736 bytes) — original PC font archive
- `OBJECTS(1).ARC` (21,646,272 bytes) — claimed PC original object archive
- `PCLEFFEC(1).ARC` (929,472 bytes) — PC effects
- `QDMETADA(1).ARC` (325,472 bytes) — PC quick-data metadata
- `QUICKDAT(1).ARC` (3,413,216 bytes) — PC quick data

Their existence and lengths are confirmed by the current-conversation
attachment metadata, and their platform provenance is explicitly stated
by the owner. **Binary integrity checks and extraction have not yet run:**
Python/container file reads returned ClientError in this session, so no
SHA-256 or record-level equality is claimed. The older
`OBJECTS.ARC` (Xbox user original) and the newly supplied
`OBJECTS(1).ARC` have *identical lengths* (21,646,272 bytes);
this is not proof that their contents are equal. Compare hashes and
index-record bounds before treating them as distinct PC/Xbox resources.

**Source correction: full PC `textures(1).arc` received on
2026-11-14**, file size **131,721,440 bytes** (attachment metadata).
This supplies the PC `Textures` category previously listed as missing,
with **1,365** expected index entries. Use the existing PC
`datasets.arc` and `indexPC.ind` for the links. **Do not request this
texture archive again.** Contents have not been byte-verified because
the local Python/container runtime currently fails even a trivial
process launch.

**New Xbox 360 UI source archives uploaded in the current conversation
on 2026-11-14 (do NOT ask for them again):**
- `FONTS.ARC`, 136,928 bytes; Xbox index: 1 font resource.
- `TEXTURES.ARC`, 351,883,360 bytes; Xbox index: 6,669 textures.
- `TEXSTREA.7z.001`, 471,859,200 bytes and `TEXSTREA.7z.002`,
  252,506,863 bytes; Xbox index: 5,836 TexStreams resource entries.

The above file lengths are confirmed conversation attachment metadata.
**Their raw archive bytes, integrity, 7z unpacking and 6,669+5,836
resource payloads have not yet been verified programmatically.** At
the time of this update the local binary-inspection runtime returned
a tool error; binary text extraction also cannot validate the format.
Do not fabricate SHA-256, read results or claim all glyphs were
extracted. Work from the uploaded files when the runtime is available.
The large TexStreams upload must be checked as a two-part archive
without uploading copyrighted resource bytes to public GitHub.

**The complete Xbox `DATASETS.ARC` was already uploaded in split form**
(`DATASETS.7z(1).001` and `DATASETS.7z.002`). The earlier integrity
audit `hulk_x360_datasets_audit.json` independently confirms successful
reassembly of a 325,534,892-byte 7z archive (SHA-256
`d5f02054c34e564ce54a9f13f35441292c368711fff547e55ce23f3293372912`)
and extraction of the **582,239,200-byte Xbox DATASETS.ARC** (SHA-256
`4ee64338b1623a5011791573ebab119238517f117c5be86136369a7257dfcede`).
It validated all 1,114 dataset index entries against
`indexXBOX360.ind`, with no overlapping resources or out-of-bounds
entries. That archive is **available from the owner's existing Library
parts**, and must NOT be requested again. Any full-archive UI mapping
should use those two parts, not merely the three pre-extracted records.
The uploaded Xbox font, texture and texture-stream archives now
cover the archive *categories* needed for reconstructing authentic
pause-menu typography, artwork and prompt glyphs. The resource bindings
from the original UI pages still need to be reconstructed and verified
against actual Xbox index offsets and decoded bytes. Do not conflate a
file being supplied with its contents being fully inspected.
The owner's original installed games may also contain additional data;
do not ask again for these exact uploaded Xbox files.

**Source correction (2026-11-14):** The previous `full Xbox
`Datasets.arc` beyond the three extracted resources` shortage claim was
incorrect. The two multipart `DATASETS.7z` files and a verified complete
reconstruction already existed before this audit. They were omitted from
the initial Library-name search because their filenames were split
archive pieces. No re-upload is required.

## Actual implementation plan (not completed)

1. Add a read-only UI metadata parser for `_app_` on both platforms,
   validate serialized widget/page layout and cross-links to resource
   hashes, including source `.ma` logical references; determine which
   Xbox `Objects`, `Fonts` and `Textures` records feed the original
   `UIPage_Pause_Settings` screen.
2. Search the existing private, SHA-pinned **USA** AOT guest PPC output
   for command names/register bindings: `UI_ActivateOptions`,
   `UI_OptionScreen`, `Options_Set*` and the UI event-router.
   Reconstruct command signatures and ownership. **Do not copy PC
   offsets into Xbox PPC or infer one from an identically named method.**
3. Add the PC-specific options using these actual guest UI event routes,
   binding only verified CVar/API behaviors. Extend existing widget
   select/scroll controls and native keyboard glyph selection. Ensure
   no in-game settings require F4; persist via a verified settings writer.
4. Render authentic menu textures/font through the existing GPU backend,
   not from a mock ImGui skin. Verify pause/focus/DPI at 2560x1600,
   regression test using the real assets privately, and only then
   stop using F10 as the normal consumer interface.

**Conclusion:** There is a shared, already-authored EoR frontend/options
system across the two releases. The project's primary blocker is
missing reverse-engineered **UI dispatch, resource bindings and ABI**,
not an absent general-purpose GUI framework. The rendering path's
existing corruption is a separate blocker for pixel-perfect output.
Do not claim "full EoR engine reverse engineered" or "native menu done".

## Cross-platform original UI next-step after PC 2008 archive upload

Once binary inspection is available, use *both* index tables to compare
the shared font resource hash (PC/Xbox `Fonts`: 1/1 shared entry but
different bytes), and inspect typed UI widget objects and quick data
against the page names and native option-script entrypoints identified
above. PC objects are marked by the owner's provenance and must be
validated against `indexPC.ind` before classifying their widget
resources. Reconstruct actual Xbox input event dispatch and PC settings
handlers; no claims are made that a functional console-native menu
exists merely because the assets are present. All game binaries and
content stay private; only sanitized metadata may enter public Git.

## 2026-11-14 — PC `textures(1).arc` follow-up and source-level validation kit

The owner sent `textures(1).arc` from the official 2008 PC release,
confirmed as **131,721,440 bytes** through current-conversation attachment
metadata. The latest available previously decoded
`Hulk_index_decoded.json` contains the following *index* ranges:

| Platform/category | Number of indexed records | Last indexed byte (exclusive) | Owner-provided file size | Unindexed trailing bytes |
| --- | ---: | ---: | ---: | ---: |
| PC Textures | 1,365 | 131,661,931 | 131,721,440 | 59,509 |
| Xbox 360 Textures | 6,669 | 351,566,132 | 351,883,360 | 317,228 |
| PC Fonts | 1 | 152,675 | 152,736 | 61 |
| Xbox 360 Fonts | 1 | 136,863 | 136,928 | 65 |
| PC Objects | 2,444 | 7,308,551 | 21,646,272 (claimed PC file) | 14,337,721 |
| Xbox 360 Objects | 3,938 | 21,451,524 | 21,646,272 (earlier Xbox file) | 194,748 |

These **bounds only** are derived from the source indices and attachment
sizes. The font record has shared hash `0xA796ECFA` but different
payload lengths (PC **152,671**, Xbox **136,859** bytes). The
`OBJECTS(1).ARC` supplied as PC has exactly the same number of bytes
as the earlier Xbox `OBJECTS.ARC`; its trailing unused space is very
large under the PC index, but normal under the Xbox index. This is a
**platform-assignment anomaly**, not proof that the owner's PC archive
is wrong. The exact hashes and resource record signatures must decide
whether the files are copies or different game builds.

**No GPU/texture/font extraction is claimed in this follow-up.**
Attempts to open the private binary directly from the mandated mounted
sandbox path with the local Python and container tools failed with the
runtime's `caas.internal.errors.ClientError`. The generic file reader
also reports no readable text for archive bytes. Size/index agreement
is not a substitute for actual hashing, byte-order validation,
overlap checks and decompression.

To make this reproducible rather than another endless request for manual
tests, added **two source-only files** to the existing `ci/` directory:

- `ci/audit_hulk_pc_x360_ui_assets.py`: strict LE/BE index parsing;
  per-category offset bounds and overlap validation; streamed SHA-256;
  per-resource identical/different comparisons by hashed ID; small
  whitelist-based scans for original UI names and near-name texture/font/
  object-hash **candidates** (not certified links); safe JSON report
- `ci/test_audit_hulk_pc_x360_ui_assets.py`: generated PC/Xbox index
  fixtures, equal/different font records, tail mismatch, original
  resource containment, byte-order and overlap-failure cases, and
  synthetic UI-hash candidate tests

**The Python suite has NOT been executed** because the runtime fails.
Its source is present in main, but no CI green status or real asset
coverage can be inferred. The scripts are development audits only:
they are not part of the x64 game EXE and do not expose original game
bytes in public GitHub.

The near-name 32-bit hash probes are intentionally conservative leads:
matching an indexed ID in the local neighborhood of a
`UIPage_Pause_Settings` token **does not establish a valid event
handler, C++/PowerPC ABI, or texture binding**. Native options should
only be wired after validating those links with the original EoR UI
object format and the pinned USA guest PPC code.

**Needed for final asset correlation:** successful execution of this
audit over the already-uploaded PC and Xbox archives and the full
Xbox 360 dataset. No additional user file is presently required for
the PC texture category, and no replacement XEX or new GitHub workflow
should be created.

## 2026-11-14 — EoR resource name hashing REVERSED and indexed UI mapped

**New verified result, replacing guesswork about resource identity:** The
EoR `index.ind` resource key is `CRC32(ASCII_UPPER(name))` for the
three independently known dataset records, the common font record and
multiple real PC/Xbox menu artwork entries. Exact checks:

- `_app_` -> `721AAFDE`, `_engine_` -> `CA54CBC0`,
  `_shader_effects_` -> `2485AB96`.
- `font_clean` -> `A796ECFA`. The original PC Fonts index
  identifies the payload at offset **4**, size **152,671**; the original
  Xbox Fonts index identifies the SAME hash at offset **4**, size
  **136,859**. Both file sizes are compatible with these bounds.
- The four-character EoR resource type tags **TXFL** (PC) and **LFXT**
  (Xbox) appear in `_app_` references, but must be removed before
  hashing the `ui_*` name. Example:
  `TXFLui_menuitems_da` -> `ui_menuitems_da`
  -> `2F95A2B1`; original PC Textures record at offset
  **34,685,932**, size **32,942**.
- `ui_menubands_da` -> `20CE9F3C`, PC texture offset
  **24,712,998**, size **65,710**.
- `ui_key_wasd_da` -> `6274200A`, PC texture offset
  **58,601,534**, size **2,221**.
- `ui_key_space_da` -> `D813EA9A`, PC texture offset
  **115,157,098**, size **4,270**.
- `ui_xbox360_a_da` -> `16FEB6A7`, PC texture offset
  **20,149,222**, size **1,198**; Xbox texture offset
  **29,541,697**, size **16,486**.

All these matches were found *inside the user's previously decoded
PC and Xbox indices*, not guessed from a graphics screenshot.
Twenty exact font/texture records, including Xbox A/B/X/Y/LT/RT glyphs
on both platforms, are catalogued in the source-only file
`native_port/analysis/eor_ui_original_resource_map.csv`. It contains
resource IDs, offsets and sizes, **no original game bytes**. The map
was independently checked for uppercase CRC32 equality and archived
bounds with a separate JS validation step.

### Actual extraction and decompilation tools now in the existing repo

- `ci/hulk_eor_extract_named_assets.py`: deterministic, safe private
  extractor using both original index endiannesses; resolves a typed
  `TXFL`/`LFXT` link, hashes the normalized EoR name, seeks
  directly to the matching original `ARC` offset, hashes and copies
  exactly the indexed resource span into a **private** directory. It
  rejects malformed/missing/out-of-bounds names and does not rename
  raw unknown formats to PNG/DDS without evidence.
- `ci/test_hulk_eor_extract_named_assets.py`: synthetic PC LE and Xbox
  BE archive tests, independent name/ID truth table, 20-record index
  bounds regression. Wired into the existing single manual-only Windows
  Release preflight, not a second CI workflow.
- `ci/ghidra_hulk_ui_xrefs.py`: Ghidra's headless Jython Script for
  *original* `HULK.EXE`, and for correctly imported USA `default.xex`
  using **XEXLoaderWV**. Locates whitelisted EoR UI option strings and
  direct reference/caller addresses to reconstruct genuine pause-menu
  command dispatch, not another ImGui view. Its addresses are leads,
  **NOT** proven callable ABI signatures.
- Independent historical format source: Luigi Auriemma's
  `incredible_hulk_2008.bms` script for EoR `index.ind` / `ARC`,
  documented in the 2019 ZenHAX discussion:
  https://reshax.com/topic/17543-the-incredible-hulk-2008-quickbms-script-for-indexind-to-extract-arc/
  and script URL:
  https://aluigi.altervista.org/bms/incredible_hulk_2008.bms .
- Ghidra XEX loader reference: https://github.com/zeroKilo/XEXLoaderWV
  (a suitable verified loader must be installed when analyzing USA
  Xbox PPC; normal PE import suffices for the PC retail EXE).

### Reproducible, private extraction commands

From a checkout of the existing repo, with the user's original files
in their own separate/private directory:

```powershell
python ci/hulk_eor_extract_named_assets.py --index "D:\PrivateHulk\PC\indexPC.ind" --archive "Fonts=D:\PrivateHulk\PC\FONTS(1).ARC" --name "Fonts:font_clean" --out-dir "D:\PrivateHulk\ExtractedPC"
python ci/hulk_eor_extract_named_assets.py --index "D:\PrivateHulk\PC\indexPC.ind" --archive "Textures=D:\PrivateHulk\PC\textures(1).arc" --name "Textures:TXFLui_menuitems_da" --name "Textures:TXFLui_key_wasd_da" --out-dir "D:\PrivateHulk\ExtractedPC"
python ci/hulk_eor_extract_named_assets.py --index "D:\PrivateHulk\Xbox\indexXBOX360.ind" --archive "Fonts=D:\PrivateHulk\Xbox\FONTS.ARC" --name "Fonts:font_clean" --out-dir "D:\PrivateHulk\ExtractedXbox"
```

These are reproducible examples, **not claims that private binary
extraction already ran**. The original uploaded binary files remain
unreadable to the container runtime in this session; the Files content
tool also reports no readable text for raw `FONTS.ARC`. Thus actual
payload SHA256 and image/bitmap decoding remain UNVERIFIED. No Xbox or
PC game executable/asset was committed to public GitHub.

### Independent-native runtime status

The resource-name hash is now understood and an exact portable extraction
path exists; this is genuine source-format progress. It does **not**
mean a rebuilt EoR runtime already exists. A *true* independent Windows
x64 game would require original object deserialization, frontend scripts,
scene/animation/physics/audio, game rules, renderer, file I/O and saves.
Ghidra plus QuickBMS do not automatically reconstruct those programs.
The current runtime still depends on ReXGlue; no claim of complete
independence or an official-quality playable port is made. Before
removing the runtime, each subsystem needs verified original-data
behavior and executable functionality, not empty stubs.

## 2026-11-14: first portable native EoR runtime module and in-game TOML save

**Implementation, not an emulation shim:**

- `native_port/src/eor_archive.h/.cpp` now supplies a self-contained
  C++20 EoR index parser for the original PC (LE) and Xbox 360 (BE)
  formats, a canonical original-name CRC32 hasher including TXFL/LFXT
  authoring prefixes, sorted category/hash lookup, and bounded exact
  `.ARC` reads. It contains NO ReXGlue, Xenia, XDK, guest PPC or Vulkan
  references. It is compiled directly in the existing Hulk executable
  target as the first native asset subsystem.
- `ci/eor_archive_native_test.cpp` exercises both serialized index
  endiannesses, eleven original verified resource-name/CRC pairs,
  rejection of truncated/bad indexes, unknown names, exact binary
  resource boundaries and out-of-bounds access. This synthetic source
  is compiled with MSVC C++20 by the EXISTING Windows Release preflight
  before any user-owned game code is downloaded. It must pass before
  any new release is issued. No retail assets are included in Git.
- `native_port/src/native_pc_options_store.h/.cpp` is a portable
  native TOML writer (no SDK calls). The current F10 menu uses the
  REAL resolved `paths.config_path` and can save FPS limit, VSync,
  internal scale, FSR1/NIS/bilinear and sharpening into the
  SAME `incredible_hulk.toml` without requiring F4. Only enumerated
  supported fields are accepted; atomically replace a temporary
  adjacent file and keep all unrelated settings and asset paths.
  Failure is reported in the UI rather than advertised as a saved
  setting.
- `ci/native_pc_options_store_test.cpp` is a synthetic C++20 test
  that checks preservation of the game-data-root, existing comments,
  CRLF, unrelated graphics, refusal of invalid/duplicate TOML keys,
  scale/filter formatting, and no unwanted permanent sidecar.
  Compiled/run in the same existing manual-only Windows CI workflow.

**Testing limitation:** These C++ files and tests are now committed and
connected to the Windows runner, but no Windows runner was launched for
this change. The local Python/container runtime raised ClientError.
The last independent successful playable EXE remains #112 (which
still has GPU artifacts); #114 failed before release, with its full
failure log retained only in the user's private Hulk cloud release.
Do not claim new MSVC tests passed, that quality/flicker has been
validated, or that a new ZIP exists.

**Two strictly ordered completion gates:**

1. **Quality first**: rebuild cleanly, fix the corrupted shader/PM4/RT
   paths and intermittent display artifacts, validate ~60Hz physics
   independently of rendering, and test camera/input and Alt+Tab with
   frame-by-frame evidence. Open a native-feeling PC options screen
   from actual game title/pause state and save settings without F4.
   Compare production frame captures, no misleading '100%' GPU metric.
2. **Fully native next**: replace remaining platform-specific runtime
   systems with modules that consume original game resources directly.
   The present native EoR index/archive reader is one such module,
   NOT a replacement world renderer, object deserializer, audio mixer,
   gameplay script VM or physics engine. Port those only with verified
   original semantics and tests before removing the executable
   dependency on ReXGlue. Final game package should have a direct
   Windows x64 entrypoint, own renderer/input/audio/save/settings
   subsystems and official-PC-style structure.

**Useful architectural comparator**:
`hedge-dev/UnleashedRecomp` implements a heavily game-themed
`ui/options_menu.cpp`, with an original-title-state event hook in
`patches/CTitleStateMenu_patches.cpp` that opens/closes that screen.
The practical lesson for Hulk is to map and patch its authentic
`UI_ActivateOptions`, `UI_OptionScreen`, and
`UIPage_Pause_Settings` event owners. Do not confuse an ImGui
backend with a non-native presentation. No Xbox menu ABI has yet been
verified by the Ghidra source script, so no game-state hook is claimed.

## 2026-11-14 continued: original UI asset provenance, native rebind and mod-port parity

### What the three PC restoration projects actually demonstrate

The Xbox 360 Stuff Pack for **NFS Most Wanted 2005** uses a host PC
game that already executes its engine logic. Console visual restoration
combines converted/material-compatible artwork, shader and effect
changes, shadows, reflections and UI fixes rather than wrapping the
Xbox GPU. Source overview:
https://www.pcgamingwiki.com/wiki/Need_for_Speed:_Most_Wanted

**Saints Row 2 Juiced** changes the running PC game's renderer, shaders,
camera, gamma, physics and native UI/menu behavior. Its source proves
that in-engine mouse support, console gamma, animation fixes and native
option pages are separate patch categories; a texture file by itself
does not implement engine code:
https://github.com/kobraworksmodding/Saints-Row-2-Juiced-Patch

**Tony Hawk's American Wasteland ReTHAWed** shows a mod platform can
port content and console-era graphics while retaining a functional PC
game host. It is not a generic Xbox runtime replacement:
https://www.pcgamingwiki.com/wiki/Tony_Hawk%27s_American_Wasteland

**Action for Hulk:** maintain two technical paths without mixing formats:
(A) complete the existing USA Xbox 360 recomp's faithful Vulkan draw
and gameplay state; (B) use the original retail PC HULK.EXE / EoR
engine as a candidate future platform for ports of **verified, converted**
Xbox material/shader/texture content. Path B needs a separate actual
PC engine executable hook/loader analysis. Merely copying Xbox
`Textures.arc` over the PC one would misinterpret platform-specific
data and can break textures. It is NOT currently an installed mod.

### Native original UI asset reader (source committed)

`native_port/src/eor_ui_asset_catalog.h/.cpp` builds on the earlier
standalone `eor_archive` index and byte-reading API. It opens PC
little-endian and optional Xbox big-endian indices as separately tagged
sources; resolves original `font_clean`,
`TXFLui_menuitems_da`, `LFXTui_*` and keyboard/mouse glyph names
using the EoR CRC32 naming rules; checks the actual archive file,
record size and bounds; and rechecks source/provenance when reading.
An unknown Xbox proprietary format is **not** silently interpreted as
DDS; DDS/PNG/BMP signatures are only reported if they occur at byte 0.
There is still no claimed valid EoR texture deswizzler, material
converter, GPU font painter or original pause UI widget renderer.

`ci/eor_archive_native_test.cpp` now covers this source isolation,
wrong endian pairing, genuine UI name lookups, missing resources,
spoofed archive paths, and unknown guest texture format. The original
20 verified cross-platform UI asset records remain listed in
`native_port/analysis/eor_ui_original_resource_map.csv`.
The new catalog code is linked into the CURRENT game target and
gated by the pre-existing single manual-only MSVC Release workflow.

### Native PC menu: actual keyboard and mouse settings

`native_port/src/native_pc_bindings.h` cross-checks **21 actual
`hulk_bind_*` CVar actions** with the NativePC Win32 input bridge
and offers **50 real supported key/mouse tokens**. F10 Controls
now contains functional dropdowns for all actions and commits changes
directly through `rex::cvar::SetFlagByName` and the existing
single `incredible_hulk.toml`. The independent TOML writer
validates action and token, writes quoted strings, keeps unrelated
settings intact, closes the config input file before Windows atomic
replacement, and preserves UTF-8 BOM when editing the first line.
Unknown key names, injected TOML contents and duplicate setting keys
are rejected. The sharpen slider no longer writes to disk with every
mouse-movement event; it saves once on release.

C++ tests include single-TOML binding persistence and BOM, and
Python source-level regression checks that the dropdown's 21 actions
EXACTLY match the real Win32 input CVar definitions. These changes
have **not been MSVC-built or run in a new game release**; the new
Release gate is pending manual dispatch. F10 remains a **host ImGui
overlay** at this point. These controls are real; their graphics
alone are NOT the EoR original pause-menu system.

### Next source-native integration blocker

The original `_app_` assets already identify the
`UIPage_Pause_Settings` / `UIPage_Pause_CameraSettings` commands,
keyboard/mouse images and `font_clean`. To open our PC options
from the game's original title and pause flows, first validate the
USA guest's `UI_ActivateOptions` / `UI_OptionScreen` dispatch
entrypoint and widget lifetime via the private Ghidra XEX analysis.
The valid hook must preserve the original guest ABI and menu state,
not guess a PPC target from the PC EXE's string names. Then replace
the interim independent overlay with an adapted, original EoR
visual renderer, adding PC actions and persistence to the same
menu state. Continue Vulkan shader/RT repair in parallel; neither
asset lookup nor custom menu styling fixes the existing color flashes.

**Current limits:** private original resource image decoding remains
unverified; no full Windows Release run, no screenshots, and no claim
of restored Xbox 360 shaders, 60Hz game physics or official-quality
native engine yet. Do not ask the user for reuploads of existing
PC/Xbox `Datasets`, `Textures`, `Fonts`, `Objects` archives.

## 2026-11-14 — native physical-resolution, aspect and GPU-surface safety pass

### Resolved source-level behavior

The D140 NativeXRenderer source (`ElGuslml/NativeXRenderer` SHA
`fa19ea85fec5c7f7f8a2df82d6c4a86548817b52`) calculates its
scale from the *actual Vulkan swapchain dimensions* when the CVar
`native_x_renderer_resolution_scale` is **0.0**. Hulk previously
fixed the scale at 2.0; its shipped TOML now defaults to **0.0**.
This is host rasterization at computed real pixels, **not** Xenia's
after-the-fact guest 720p upscale. The existing guest profile
`output_width=1280` / `output_height=720` and
`video_mode_width=1280` / `video_mode_height=720` remain untouched
to protect guest XDK surface/PM4 storage calculations.

The original retail Hulk 2008 PC executable is documented as
**Vert-** at wider aspect ratios; another widescreen image or a
different Vulkan output is not a reliable camera/FOV fix:
https://www.pcgamingwiki.com/wiki/The_Incredible_Hulk_%282008%29

NativeX supports `Profile::widens_cameras` and per-axis host scaling
only **after the game hooks its actual camera/projection and HUD
lifetime**. No verified Hulk USA hook exists. The shipped config sets
`native_x_renderer_fill_screen=false`,
`native_x_renderer_aspect=0.0`, and the host startup rejects any
unverified widescreen fill request, preserving 16:9 composition
within 4:3, 16:10 and ultrawide physical swapchains.

`native_port/src/native_resolution.h` is a standalone native C++20
screen-fit / host-raster helper, not an Xbox emulation upscaler. F10
Display now reports the physical framebuffer dimensions available to
ImGui, actual 16:9 fit rectangle and bar thickness; it includes
11 host-window presets (4:3, 16:9, 16:10, 21:9-like, 32:9), plus a
deferred fullscreen preference. Window-size presets are **windowed
logical dimensions**, which can differ from physical pixels on
a 175%-DPI Windows desktop. Fullscreen uses the desktop output.

`native_pc_options_store::SaveWindowSize` saves the **two host-only**
`window_width` / `window_height` values as a single atomic edit
of the one original `incredible_hulk.toml`, not through the SDK's
`resolution` CVar, which would also modify guest video-mode fields.
The F10 Graphics page can choose Vulkan physical-pixel Auto, 1x or
2x at next launch, without destructive runtime swapchain resets.
The existing C++20 settings test covers sample outputs:

| Host output | Physical scene FIT | Safe presentation |
|---|---|---|
| 2560x1600 16:10 | 2560x1440 | 80px top/bottom |
| 1920x1200 16:10 | 1920x1080 | 60px top/bottom |
| 3840x2160 16:9 | 3840x2160 | no bars |
| 3440x1440 ultrawide | 2560x1440 | 440px left/right |
| 1024x768 4:3 | render at >=1280x720 | 1024x576 with 96px top/bottom |

### What log203 ACTUALLY establishes about pixel loss

The original user-supplied `incredible_hulk_203.log` contains:

- `[HULK/SCENE-RESOLVE] #1` guest 640x360, 4xMSAA,
  **host source 2560x1440 / destination 2560x1440**;
- `[HULK/RESOLVE-CALLER] kind=output #1` 1280x720
  guest to 1280x720 guest, **host 2560x1440 to 2560x1440**;
- `[HULK/FRAMEPATH] present` **2560x1440**
  picture to 2560x1600 swapchain, rectangle
  **(0,80 2560x1440)**.

These observations **do not support** a categorical claim that this
particular main 3D scene resolve is being reduced to 720p. They are
consistent with native host pixels already working in one frame path.
They do NOT prove all intermediate effects/mip levels/textures retain
native resolution, or that signs, color flashes, shader translation,
geometry, 4x-MSAA artifacts or 60Hz physics are fixed. A fixed Auto
scale merely adapts to other host resolutions; at the owner's
2560x1600 display the original forced 2x already generated a
2560x1440 host scene, as the log establishes.

### Concrete invalid Vulkan texture safety repair

The original `GuestTextures::GetMaxHostTextureWidthHeight`
(D140 `src/guest_textures.cpp`) returns **16384** for host 2D
and **2048** for 3D. Our previous Hulk descriptor guard allowed
32768 regardless of texture dimension: impossible 2D and 3D
dimensions could reach `IRenderDevice::CreateTexture`. The patch
`ci/patch_native_x_hulk_texture_resource_guard.py` now uses the
renderer class's **actual source-defined dimension-specific limit**
and rejects impossible guest texture fetches before Vulkan
allocation. It preserves existing mip, depth, cube aspect and
format guards. The corresponding source-only tests now require 2D
32768 rejection, 2D 16384 acceptance, 3D 2048 acceptance and
3D 4096 rejection. This prevents one proven invalid allocation
path, **not** the unresolved origin of corrupted PM4 fetches.

### Remaining runtime proof gates

- Color flashes, stale/ambiguous RT/depth bindings, malformed 27-dword
  EXEC shaders, texturing/LOD quality and GPU time must be diagnosed
  using real frame captures; do not skip original Resolve or draw
  commands on speculative grounds.
- Hulk USA `UI_ActivateOptions` / `UI_OptionScreen` and
  `UIPage_Pause_Settings` callback addresses/ABIs remain to
  be confirmed on the actual AOT PPC/XEX before replacing F10.
- Original EoR Fonts/Textures need real payload decoding; mapping
  original hashes is not equivalent to GPU-ready UI art.
- No new Windows Release, runtime test or ZIP was created for
  these source changes. The single workflow remains manual-only.

## 2026-11-14 — USA shader, camera, original pause menu and surface-lifetime work

**User's native target is Windows 2560×1600 (16:10), DPI 175 %.**
This does NOT authorize changing guest XDK `1280×720` framebuffer,
`640×360` 4× MSAA surface descriptors or original `0x8263BF50`
Resolve semantics. NativeXRenderer already renders these guest surfaces
at native host resolutions when the device and swapchain allow it.
Log203 proves one full frame path was **2560×1440 → 2560×1440**
with output letterboxing to **2560×1600**. Direct 16:10 view coverage
requires a verified original camera projection plus separate 2D HUD
boxing; it is **not** solved by blindly changing EDRAM pitch or scissor.

### 27-word shader source provenance: new fail-closed capture verification

In the original user-owned log203, three failing *vertex* shader
analysis records (hashes `EC7DD173F4761676`,
`0F5C6EDD918BC3F8`, `5C56143AEB4D1865`) report
`IM_LOAD_IMMEDIATE`, exactly 27 dwords. A private-only comparison
shows **26 of 27 words equal, with the changing dword at offset 23**.
Their common offset 22 has bits consistent with a Type3 PM4
`IM_LOAD` header (`opcode 0x27`, two payload dwords).
This is **evidence suggestive of shader/command-ring overlap**,
not proof that its bytes are invalid or that the source was recycled.
No guest microcode was committed publicly.

The original upstream NativeX D140 decoder had an inline shader size
truncation and no absolute source bounds check. **Those limits had
already been addressed** in this project's earlier
`patch_native_x_hulk_bin_predication.py` and
`patch_native_x_hulk_guest_memory_bounds.py`; they are not newly
discovered fixes. This iteration additionally gives
`patch_native_x_hulk_guest_memory_bounds.py` an *immutable copy*
check: compare each freshly owned IM_LOAD program with source bytes
before allowing it to enter the shader program cache. A changed source
within capture now fails the packet scan instead of being silently
accepted. It cannot detect a corruption that was already stable at
the instant of the first read.

`tools/audit_hulk_shader_pm4_overlap.py` now classifies candidate
Type3 shader-load signatures near the end of failed shader dumps and
reports ONLY hash, word offsets and classification. Its synthetic
three-program test reproduces a changing word at index 23 and a
candidate header at index 22. This is a forensic classifier, NOT
permission to suppress real PM4 draws. Existing manual CI adds
`python tools/audit_hulk_shader_pm4_overlap.py --test` before the
private game build.

### RT/depth ownership / CPU contention

The already installed `patch_native_x_hulk_packet_surfaces.py`
matches packet-specific render targets against the GPU register image
by the full `RB_SURFACE_INFO/RB_COLOR_INFO/RB_DEPTH_INFO` identity
and refuses ambiguous synthetic bindings. This iteration changes
the ordinary **hooked XDK draw's five surface-history updates** from
up to five separate mutex acquisitions to **one batched history
update**. It keeps the original full keys, 256-entry per-slot LRU,
timestamp order, and packet-specific fallback exactly as they were.
No shader, guest RT size, Clear, Resolve or physical surface was
invented or discarded. It is a source-level concurrency improvement
that still needs real frame/CPU profiling to prove FPS benefit.

### Original USA camera ABI: source-only research, no speculative hooks

`ci/ghidra_hulk_camera_projection.py` adds a private headless Ghidra
collector for the **original USA Xbox 360 XEX** (with XEXLoaderWV),
and separately the **original retail PC executable**. It finds
address-level references to observed EoR names such as
`UIPage_Pause_Settings`, `UI_ActivateOptions`,
`UI_OptionScreen`, `Options_SetCameraYaw_b` and
`Options_SetCameraPitch_b`, plus data candidates for
16:9, 4:3 and inverse-aspect projection floats, and function
names likely associated with camera and UI. The output explicitly
states `abi_proven=false`; it cannot establish the PPC calling
convention or matrix owner without inspecting the actual xrefs and
their callers. The user need NOT resend the game archives; private
source access is the remaining practical obstacle to running this
script here.

The independent `native_port/src/native_camera_projection.h`
implements vertical-FOV-preserving horizontal projection ratios:
16:9 1.0, 16:10 **10/9** for horizontal focal coefficient,
ultrawide less than 1.0, and 4:3 greater than 1.0.
`ci/native_pc_options_store_test.cpp` checks these numerically,
including invalid parameters. This code is **not yet connected**
to the unverified game camera or EDRAM/HUD. No unproven `widescreen`
flag has been turned on.

### Harry Potter PC menus as an implementable pattern

Reviewed the actual source for
`Barthes-Recompiled/HP1-Recompiled/port/src/scda/app/menu.cpp`
and `Barthes-Recompiled/HP3-Recompiled/port/src/scda/app/menu.cpp`.
Both use ImGui option-page rows, interactive keyboard navigation,
dedicated per-game TOML and viewport/DPI-dependent window sizing.
The F10 implementation by itself is **not evidence** of hooking a
game's original 2008 menu. The architecture works by sharing option
rows with host-specific title/pause events where those are mapped.

Applied the safe part immediately:
`native_port/src/pc_menu.cpp` now scales the option window and its
section/sidebar/footer layout against available logical display size
using `io.FontGlobalScale`. The old fixed `850×610` first-use
window could extend beyond a small or scaled client area.
`ci/test_hulk_pc_menu.py` requires the responsive-size code.
**Original Hulk EoR skin/assets and USA `UIPage_Pause_Settings`
entrypoint remain unconnected**, and GPU-ready parsing of EoR UI
texture data still requires verified format decoding.

### Release validation state

New source and synthetic tests have been committed to the existing
source repository `main`, all on the single manual-only Windows
workflow. The current tool environment cannot execute native
MSVC/Clang or run Ghidra over the user's original binaries.
**No new Windows build, shader capture, frame benchmark or ZIP was
created in this iteration.** Previous CI #114 failed in Windows
compilation and last known assembled release #112 still flickered.
A source patch or a correctly matched 16:10 pixel calculation MUST
NOT be represented as official-grade, play-tested Vulkan output.

Remaining blockers for an actual *complete* solution:
identify where the PM4-like trailer enters 27-word IM_LOAD records
and recover byte-identical original guest programs; prove texture
mip-chain source and sampler/LOD state from real captures; validate
RT/depth per draw with RenderDoc/reference renderer; find genuine
USA PPC 3D camera and UI pause event ABI; alter only 3D view
projection and box HUD separately; and match GPU/CPU performance
with one same-scene baseline capture before changing upload or
pipeline schedules.

## 2026-11-15 — source-only refinement, NOT a release

- `ci/patch_native_x_hulk_packet_surfaces.py`: `HulkPacketSurfaces`
  now snapshots all five actually bound D3D objects before acquiring a
  single RT/depth history mutex, matches the source-defined full GPU
  `RB_SURFACE_INFO`/`RB_COLOR_INFO`/`RB_DEPTH_INFO` values, and
  preserves the full-key LRU semantics. There is no per-slot mutex
  acquisition and no speculative reconstruction of missing guest
  surface dimensions. Ordinary hooked draws already use one grouped
  history update. No real Draw or Resolve is intentionally suppressed.
- `native_port/src/native_resolution.h`:
  `FullScreen3DContract` computes the separate host pixel scale of
  a **true 2560×1600** 3D target, with original guest output
  `1280×720`: x scale `2.0`, y scale `20/9`, 3D camera horizontal
  focal compensation `10/9`. 4K 16:9 requires no compensation;
  ultrawide needs a multiplier smaller than 1.0. Full-screen and safe
  16:9 letterbox are separate explicit contracts. Invalid sub-native
  sizes are rejected. Corresponding C++ synthetic tests were added.
- **Important:** the full-screen raster/projection math is deliberately
  not enabled in Hulk Vulkan: no independently verified USA 3D camera
  matrix ownership or pause menu call ABI has been obtained.
  Activating it without hooking every relevant 3D view and isolating
  UI/HD shadows would stretch or clip actual geometry.
- Original GPU metadata remains 640×360/4× MSAA and 1280×720/1×
  for their respective aliases. D140 already renders native Vulkan
  host pixels; rewriting these guest dimensions has been explicitly
  rejected as a way to fix original 16:10 camera FOV.
- Under the user's release gate, **no new Action build, ZIP, gameplay
  render or real 60Hz physics acceptance test was run**. Original
  `UIPage_Pause_Settings` functionality, full 16:10 host frame,
  correct all-draw shader microcode and low-detail sign textures
  remain unverified and unresolved.

## 2026-11-15 — BLACK V3 / Toy Story 3 comparison and actual menu integration status

The reference BLACK-XBERecomp **V3**, tagged `72830d4`, has TWO complementary
interfaces: F10 host settings (`src/common/menu.cpp`) and **real in-game
options/pause-page integration** (`src/game/options_menu.cpp`). The latter
identifies its game Flash UI root, holder and button instances; it creates
and populates rows, tracks the page/input/focus lifecycle, and returns to
the actual game option page. Its quit control (`src/game/exit_menu.cpp`)
also preserves the original game's title-menu navigation. V2 specifically
made all added options pages available from the paused game. **Do not
misrepresent Hulk's existing independent F10 ImGui overlay as equivalent.**

References (source):
- https://github.com/TGP482/BLACK-XBERecomp/blob/V3/src/game/options_menu.cpp
- https://github.com/TGP482/BLACK-XBERecomp/blob/V3/src/game/exit_menu.cpp
- https://github.com/TGP482/BLACK-XBERecomp/releases/tag/V3

The official **Toy Story 3 Complete Edition** (Digital Eclipse, October 2026)
was rebuilt from the game's **original source repository**. Its modern
native port is NOT evidence that Xbox 360 proprietary game code can be
recreated merely from assets or replaced by an arbitrary D3D wrapper.
The independent 2026 Toy Story BTB Patch illustrates per-object LOD,
streaming memory budgets and shadow cascade adjustments, including reported
120/unlimited-FPS experiments; Hulk must verify its own EoR equivalents
and physical simulation timing before applying any multipliers.
- https://reverbgame.com/toy-story-3-complete-edition-interview-on-performance-restored-content-and-platform-details
- https://www.nexusmods.com/toystory3completeedition/mods/10?tab=description

**Concrete source changes in Hulk main, not a finished native EoR menu:**
1. `8147130e9c7fd97d106507f6629dac9e3fbc966e` implements independent
   X/Y mouse sensitivity and inversion with independent fallback/direct-camera
   paths, actual F10 Controls sliders, single native TOML persistence, and
   C++/Python regression coverage. The direct camera remains inoperative
   without a verified original USA XEX guest-camera memory layout.
2. `ac20381a93b9923a68485ca5774054e6905ad8e2` extends the existing
   Ghidra EoR UI identifier scanner with both BE/LE references to 13
   **independently index-verified original Fonts/Textures CRC32 resource
   hashes**. In contrast to ungrounded PPC guesses, every hit is explicitly
   a candidate requiring original event/lifetime verification. The added
   source-only hash-correlation test is in `ci/test_ghidra_hulk_ui_hash_xrefs.py`.
   `563de24e6b8d01eb8541c1899aa5a98149ff705a` wires that test into
   the one pre-existing manual-only Windows Release workflow.

**Still required to complete the user-requested original menu:** resolve
`UI_ActivateOptions` / `UI_OptionScreen` / `UIPage_Pause_Settings` event
owners, PPC ABI, font/texture decoding, menu input focus and scene lifecycle;
then integrate Display, Graphics, Controls, Audio and Exit with the original
EoR pause/title UI. No F10 dependency should remain for normal operation.
The current source-only Ghidra candidate scanner is not such an integration.

**Other unpassed release gates:** Windows compile after #114 compiler failure;
27-dword IM_LOAD VS provenance; full RT/depth/Resolve and GPU flash fixes;
correctly sampled full-size signs and original material/mip behavior;
verified 16:10 USA camera projection + separately proportioned HUD;
60+ Hz simulation without car-door/water/animation bugs; native mouse yaw/pitch
without XInput simulation; audio, Alt+Tab, saves and final standalone ZIP.
The user’s 2560×1600 display / 175%-DPI and 16:9 guest framebuffer dimensions
are deliberately kept separate. No new Windows game CI, executable or ZIP
was built for these changes. Do NOT mark them as validated runtime fixes.
