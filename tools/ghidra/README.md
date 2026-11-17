# Hulk USA (2008) — local Ghidra/XEX engine callsite audit

This is an optional **offline** reverse-engineering workflow to discover real
original Edge of Reality PowerPC callsites behind XDK Draw/Resolve/Clear/Present.
It is **not** a ChatGPT plugin, not a game renderer, and not an auto-patch.
The only file that may be committed is this source-only tooling; **never commit
the XEX, Ghidra project/decompiled game code, shader binaries or guest memory**.

## Recommended local tools

- [Ghidra](https://github.com/NationalSecurityAgency/ghidra):
  original open-source decompiler/PowerPC analysis framework.
- [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV):
  import Xbox 360 XEX and `.pdata` into Ghidra. Install a binary matching
  your Ghidra version. Maintained builds also exist through
  [REPlugins](https://github.com/DohmBoy64Bit/REPlugins). The latter
  also includes an optional *third-party* GhidraMCP local bridge; it is **not**
  an automatically connected ChatGPT plugin. Do not expose private
  game data or locally running Ghidra MCP over public network interfaces.
- [Ghidra headless manual](https://github.com/NationalSecurityAgency/ghidra/blob/master/Ghidra/RuntimeScripts/support/analyzeHeadlessREADME.md)

No ChatGPT Plugin Directory integration directly exposing Ghidra was
discovered at the time of this review. The GitHub connector and the existing
self-contained Hulk scripts can still update source files and run source-only
logic without installing a local MCP server.

## Offline USA XEX workflow

You need the exact **USA** `default.xex`, SHA-256
`346a73690303f6633bfdaf9850e78793fe07cdbfb8f849b89fbee320630b11f0`.
Never substitute the PAL `default.xex`, an EU Title Update, the original
2008 PC `HULK.EXE`, or a rebuilt ReXGlue Windows executable.

On Windows, install Ghidra and its matching XEXLoaderWV extension, restart
Ghidra, and use PowerShell:

```powershell
pwsh -NoProfile -File tools/ghidra/Run-HulkUSA-XdkAudit.ps1 `
  -GhidraRoot "C:\Tools\ghidra_12.1.2_PUBLIC" `
  -UsaXex "E:\Games\IncredibleHulk\games\incredible_hulk\game\default.xex" `
  -OutputJson "E:\HulkAudit\hulk_usa_ghidra_xdk_callsites.json"
```

This first checks the source SHA and refuses an unknown XEX. The script also
checks Ghidra's imported-program MD5 against that exact SHA-pinned file, so
an unrelated already-open PowerPC database cannot masquerade as Hulk USA.
The headless XEXLoaderWV importer builds an isolated temporary project, the
`HulkUSA_XDKCallsiteAudit.java` script records verified direct branch
instruction address, function entry, and return-address `LR=site+4` for
the ten source-proven XDK wrappers. The script outputs **only original guest
addresses and method names** in `hulk_ghidra_usa_xdk_direct_v1` JSON,
with `engine_draw_abi_verified=false`. It attempts to delete its private
temporary project when finished; check its warnings if Windows holds a lock.

## Correlating Ghidra, ReXGlue generated source and LIVE runtime

If the current game build has source-only diagnostics enabled
(`hulk_eor_xdk_capture`), it can log original PPC `r3..r10` snapshots
and cumulative `[HULK/EOR-XDK-CALLSITE]` lines.
The existing `ci/hulk_eor_xdk_callsite_crosswalk.py` verifies the USA XEX
and matches `LR-4` with the exact original `bl` instruction extracted
from genuine ReXGlue-generated guest C++.

Without rebuilding, use the two address-only JSONs:

```powershell
python ci/hulk_ghidra_xdk_crosscheck.py `
  --ghidra "E:\HulkAudit\hulk_usa_ghidra_xdk_callsites.json" `
  --runtime "E:\HulkAudit\hulk_eor_xdk_verified_callsites.json" `
  --output "E:\HulkAudit\hulk_eor_triple_verified_callsites.json"
```

The validator insists on three independent pieces of evidence:
1. Original USA input SHA and PowerPC import from Ghidra;
2. Exact caller `bl` instruction and function provenance from both Ghidra
   and ReXGlue codegen;
3. The same original guest `LR` actually seen during Hulk rendering.

The main `native_port/BUILD_WINDOWS.ps1` optionally performs the
additional comparison when BOTH
`HULK_EOR_XDK_RUNTIME_LOG` and `HULK_EOR_GHIDRA_EDGES_JSON` are
provided. Nothing is uploaded to a new repository; the Release workflow is
still a single manual-only workflow.

**Important limitations:** A matching callsite alone does NOT reveal the
original EoR vertex layout, shader/material ownership, texture fetch mapping,
World/ViewProj matrices, or native render target. For that, the next analysis
must trace concrete original guest object pointers and their producers and
prove those reads using original model/shader archives and live snapshots.
Neither Ghidra, XEXLoaderWV nor GhidraMCP is evidence that the independent
Vulkan renderer already renders actual Hulk game frames.
