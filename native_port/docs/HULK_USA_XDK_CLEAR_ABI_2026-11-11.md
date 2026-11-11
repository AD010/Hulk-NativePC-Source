# The Incredible Hulk (2008) USA — Verified XDK 6534 Clear/ClearF hooks
**Date:** 2026-11-11. Source-only audit; no game executables, XEX image, proprietary shader words or archive contents are committed.

## Actual PPC AOT / SDK evidence
Compared the user's saved AOT `incredible_hulk.exe` against `Hulk_NativePC_XDK_Addresses_Stage1.zip`: the 99,269 guest→x64 function-address entries agree and the 141 recognized PM4 literals agree. This links the disassembly to the previously analysed USA AOT function map. The *private* USA `default.xex` is pinned separately by SHA-256 `346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`; the AOT mapping match is powerful static evidence, **not** a new byte-for-byte verification of the XEX in this session.

| XDK method | USA guest entry | Existing AOT host entry | Behaviour established |
| --- | --- | --- | --- |
| `Clear` | `0x82888D68` | `0x00000001410ECD70` | Converts packed `D3DCOLOR` to four floats and invokes `ClearF` for each supplied rectangle (or one full clear when absent) |
| `ClearF` | `0x82888C40` | `0x0000000141FEC5A0` | Uses the specified rectangle or constructs one from the bound render target; calls the low-level clear helper |
| Inner helper, **NOT a hook** | `0x828885B0` | `0x00000001424CEB80` | Emits the native XDK PM4 commands for the actual Xenos clear; called by `ClearF` |

The `Clear` AOT function has two direct calls into `ClearF` and reads `r7` (`ctx+0x38`) as packed color, `r5` (`ctx+0x28`) for the rect array, `r4` (`ctx+0x20`) for rect count, `r6` (`ctx+0x30`) for flags and `r9` (`ctx+0x48`) for stencil. `ClearF` reads `r5` (`ctx+0x28`) and for null rectangle reads surface dimensions, eventually entering `0x828885B0`. Independent AOT callers of `ClearF` include the known `BeginTiling` at `0x8288A900` and user engine functions `0x8263B688`, `0x8263BBD8`. The earlier tempting `0x8289A1F8` RECTLIST function is **not** promoted to Clear: it doesn't establish the expected Z argument or call graph.

## PPC ABI required by NativeXRenderer
NativeX `calls.inc` already supports both calls:
- `Clear(dev, count, rects, flags, packedColor, z, stencil)`: `r3` device, `r4` count, `r5` rect pointer, `r6` flags, `r7` packed color, `f1` depth, `r9` stencil.
- `ClearF(dev, flags, rect, float4Color, z, stencil)`: `r3` device, `r4` flags, `r5` rect, `r6` pointer to float4, `f1` depth, `r8` stencil.

NativeX `renderer::OnEnter` intentionally captures **ClearF on entry**, including nested `ClearF` calls from `Clear`. The `Clear` outer wrapper is *tracked but not separately captured*, preventing duplicate host clears. Its `CaptureClear` copies rectangle, color, target identities and Z/stencil while source bytes are valid. `ExecuteClear` then uses DiligentCore to actually clear Vulkan color/depth/stencil targets. The original XDK Clear PM4 work still runs through the guest game path for guest semantics; host clears replace the visual effects, not the game logic.

## Why this is a real fix but not final visual proof
Build #67 counted `g_stats.clears=0` on menu and gameplay despite visible frame rendering, and menu screens accumulated letterforms. Without the USA ClearF hook, NativeX never captured this operation; that lost **framebuffer clear** is a credible explanation for UI ghosts. With both hooks installed it can capture the authentic XDK ClearF commands rather than blindly clearing each frame, which would destroy persistence and postprocessing effects.

**Validation required:** On an explicitly user-authorized future Windows Release, observe nonzero `clears` during relevant menu/frame transitions, then verify a genuine menu replacement and stable sky/depth in screenshots. These addresses do not independently fix bad shader translation, invalid texture-fetch descriptors, or incorrectly bound geometry. No CI run or ZIP is launched by this source change.

## Safety and regression gates
`ci/hulk_engine_render_callgraph.py` now requires ten unique methods and the exact, paired USA Clear/ClearF entries; missing/swapped/duplicate entries fail closed. The graph groups them as `clear` instead of misc tiling. Existing `native_port/src/native_x_hulk_profile.cpp` already parses `Clear`/`ClearF` from upstream `calls.inc`, so no second hook registry or new workflow was needed.
