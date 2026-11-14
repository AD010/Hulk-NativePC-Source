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
