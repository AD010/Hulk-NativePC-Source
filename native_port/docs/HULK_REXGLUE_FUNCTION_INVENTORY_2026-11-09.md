# Hulk ReXGlue / PPC function-start audit — 2026-11-09

Compared original TekRantGaming/incredible-hulk-recompiled and AD010/Hulk-NativePC-Source.
Function start sets are identical. The two ReXGlue lists do NOT skip functions; they force codegen to include otherwise missed addresses.

- missing_funcs.toml: 495 candidate direct branch/unregistered guest starts.
- indirect_funcs.toml: 13,912 pointer-derived candidates, each with a guessed code span.
- 0 starts duplicated between lists; no explicit indirect intervals overlap; no missing start lies inside an indirect interval.
- Save/restore fall-through helpers near 0x827377B0 are absent from both lists as the original author intended.
- Typical indirect span: median 16 bytes, p95 64 bytes, p99 96 bytes.

Three large spans require validation against PowerPC big-endian disassembly and original unwind information:
- 0x82D19A8C–0x82D4031C: 157,840 bytes, data pointer at 0x83484A28.
- 0x82D1085C–0x82D17C6C: 29,712 bytes, data pointer at 0x8348DFB4.
- 0x82A0FA9C–0x82A116CC: 7,216 bytes, data pointer at 0x83493834.

These are NOT proved erroneous. The original find_missing_funcs.py uses terminator/preceding padding heuristics and explicitly avoids some within-function blr splits detected by the debug codegen output. A successful ReXGlue build does not certify all virtual call targets and real GPU state.

Live log incredible_hulk_153.log: 210000 PM4-GUARD skips; 100000 ambiguous surface matches and 100000 fallback recoveries at last thresholds; 168 too-wide 1D texture warnings, 162 unsupported primitive messages, 38 incorrect index counts. Native Present resolution is correct: 2560x1440 into 2560x1600. High-motion flicker and green missing regions likely occur before Present; multi-frame attachment/depth/pixel-history verification is still needed.

Non-native rooftop screenshot: low-LOD silhouettes and thick fog are original engine/streaming defects. Prior EoR metadata scan found CameraFarPlane, FogNear/FogFar, ZoneStreamManager, HiResDataset, ZoneLowLod as schema STRINGS only; modifying fog alone cannot stream high-LOD Manhattan geometry.

New source-only audit tool: python ci/audit_hulk_rexglue_function_inventory.py native_port/missing_funcs.toml native_port/indirect_funcs.toml. It does not modify codegen, runtime, user files, or CI workflows.

Next checks: compare source and PPC basic blocks at three long spans; verify real bctrl/vtable callers and eight known XDK hook ABIs; trace GPU packet-time render targets and shader/fetch constants through consecutive frames. D3D11 FL11_0 remains target architecture, not a completed Vulkan repair or drop-in Darkness renderer.
