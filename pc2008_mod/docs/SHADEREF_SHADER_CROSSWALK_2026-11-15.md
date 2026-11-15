# Hulk original PC2008 vs Xbox360 ShaderEffects bytecode crosswalk — 2026-11-15

The owner's real private indices and original archives were compared WITHOUT
uploading proprietary bytes. PC 2008 uses D3D9 VS/PS shader bytecode;
Xbox 360 uses Xenos ShaderContainer. They are NOT interchangeable.

Exactly **7** effect hashes are shared. In six cases both platform effect
records contain the same *number* of shader signatures, for a total of
**16 PC CTAB / 16 Xenos container signatures**. The remaining shared
effect has no embedded shader code in either platform.

| Original EoR Effect Hash | PC record bytes | PC D3D9 CTAB | Xbox record bytes | Xbox Xenos containers |
| --- | ---: | ---: | ---: | ---: |
| 01E1C422 | 4,610 | 6 | 5,310 | 6 |
| 1FA58AE4 | 1,143 | 2 | 1,435 | 2 |
| 37AA3DD0 | 1,137 | 2 | 1,485 | 2 |
| 4FD7B844 | 1,619 | 2 | 1,883 | 2 |
| 9DA7A534 | 1,665 | 2 | 1,909 | 2 |
| E374333D | 501 | 0 | 541 | 0 |
| E601E566 | 1,305 | 2 | 1,581 | 2 |

**This is a real bridge, not the final shader port.** Each exact common
effect ID can be audited to map the original PC SM2 D3D9 shader to the
Xbox Xenos equivalent in offline source, then preserve the PC engine
material/pass call semantics while replacing the compiled PC shader
only after the bytecode and texture bindings are verified. Directly
replacing PC SHADEREF.ARC or Xbox effect bytes corrupts the PC resource
format and will not add 5,194 native shaders automatically.

The other 226 Xbox-only effects lack a common PC effect identifier and
need genuine material/resource/pass discovery. The native PC D3D9 ASI
is the appropriate route for bounded graphics enhancements; official
PC menu/Havok/saves remain native without guest runtime dependencies.

Reproduce: run tools/hulk_pc_xbox_shader_crosswalk.py against the
owner's original PC/Xbox IND and matching SHADEREF archives. It emits
only metadata; no copyrighted game bytecode is written to GitHub.
No actual runtime shader replacement has been performed. The compiled
Win32 ASI is not validated in a real Windows game process yet.
