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

**Not individually present in the Library inventory, though referenced
by the indexes:** `Fonts.arc` from PC and Xbox (both indexes each
declare one Font resource); `Textures.arc` for PC and Xbox
(index entries: 1,365 vs 6,669); PC `Objects.arc` (the uploaded
`OBJECTS.ARC` length corresponds to the Xbox index, not the PC
index); full Xbox `Datasets.arc` beyond the three extracted
resources; possible Xbox `TexStreams.arc` for texture payloads.
The original user-owned installed games/discs may already contain
all these. DO NOT request duplicate archives before checking existing
project inputs. Lack of the files in Library is **not** proof they are
missing from the owner's own game installations.

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
