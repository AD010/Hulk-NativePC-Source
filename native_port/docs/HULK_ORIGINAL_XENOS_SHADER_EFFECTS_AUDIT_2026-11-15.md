# Hulk original Xenos ShaderEffects: private-source audit (2026-11-15)

This is a **completed read-only audit of actual user Library bytes**, not
the full PM4-free EoR Vulkan renderer, not real SPIR-V compilation, and
not a proof that current colored flashes, low-res signs or 30FPS are fixed.

Source verified in the private workspace (NEVER copied to GitHub):
- Xbox 360 `indexXBOX360.ind` — SHA-256
  `46e6c6ec9f7428ba3a1737ab7ebad564c1b027b006c3e35156d2e036ab8b2820`.
- Xbox 360 `SHADEREF.ARC` — 5,913,920 bytes, SHA-256
  `c335e0ad39be455e34dcf6e02dfbcac74d17184c6a39ceb38411cf9be42e89dd`.
- Alternate owner Library archives `SHADEREF(1).ARC` and `SHADEREF(2).ARC`
  do NOT match the Xbox index and must not be treated as this source.

The genuine Xbox index contains **233 ShaderEffects records**. Every
record range fits the chosen archive. **231 records contain at least one
valid Xenos ShaderContainer**. All **5,194** embedded standalone containers
passed big-endian nine-dword header, per-record bounds, constant-table,
definition-table, offset and physical bytecode checks. There are
**2,597 VS and 2,597 PS**, with 5,194 distinct SHA-256 identities. All
shader physical code extents lie inside the original container. These
programs were written into a PRIVATE owned shader bank with
`manifest.json`, not this public tree.

An independent *bounded direct-EXEC* static scan of original bytecode
found 12,648 unique direct fetch instruction sites:
- vertex fetch, opcode 0: **7,867**
- texture fetch, opcode 1: **4,774**
- getWeights, opcode 19: **3**
- set-gradient-horizontal, opcode 25: **2**
- set-gradient-vertical, opcode 26: **2**

**24** shaders have static evidence of dynamic control-flow opcodes.
`getCompTexLOD` opcode 17 was **not observed** in the direct-EXEC
paths of these ShaderEffects: the runtime getCompTexLOD errors in
`incredible_hulk_212.log` and `213.log` must be matched to actual
game-time IM_LOAD origins before attributing them to this archive.
The scan does not prove absence of opcode 17 in dynamic branches,
effects outside this archive or translated runtime-generated shader code.

Use `ci/hulk_shader_effect_bank.py --extract-xenos` privately to create
the SHA-indexed manifest. Then run source-only
`python ci/audit_hulk_eor_shader_coverage.py --private-manifest <path>`.
The resulting JSON contains only counts; no actual shader code,
graphics constants, game executables or copyrighted game bytes.
For offline Vulkan shader conversion see `ci/hulk_xenos_to_spirv.py`
and its adapted XenosRecomp fetch/LOD patches. Shader conversion needs
complete Xbox instruction semantics, exact material/bindless resource
binding and native EoR Draw PSO ownership. The game is still linked to
ReXGlue/NativeX on the current runtime path.

**New game build intentionally NOT triggered.** Single Windows Release
workflow stays manual-only until all requested system integration work
has passed source tests and actual gameplay validation.

## Direct-EXEC offline converter blocker screen (2026-11-15)

A private scan of **all 5,194 original containers**, decoding only actual
Xenos direct EXEC fetch instructions (never interpreting ALU words as fetch),
found only **2 shaders** from **1 original effect** with directly used
unsupported SET_GRADIENT opcodes 25/26. **24 shaders** have dynamic flow.
Thus **5,168** pass the *static direct-fetch vocabulary* check. This is
NOT a count of compiled shaders or proof of parity: XenosRecomp may reject
other opcodes/control flow; layouts, texture formats, material semantics
and real runtime PM4-origin shader programs remain unverified. The script
reports `candidate_is_compiled=false` explicitly to avoid false positives.
