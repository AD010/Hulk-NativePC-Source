# The Incredible Hulk (2008) PC — Resource/Shader ASI Bridge v0.1

This is a **real compiled 32-bit Windows modding framework for the official
PC version**, not a finished Xbox 360 graphics remaster. The alternative
full Xbox EoR Vulkan recomp project is kept separate and intact.

## Verified original 2008 PC executable

Owner's original `HULK.EXE` SHA-256:
`6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233`.
PE32 x86, 2008-06-11; imports `dinput8.dll!DirectInput8Create`,
`kernel32!CreateFileA` (IAT RVA `0x003CC118`), and
`d3d9!Direct3DCreate9` (IAT RVA `0x003CC310`).

## Components and safe defaults

- `dinput8.dll`: own x86 system DirectInput8 forwarding shim; loads
  `HulkBridge.asi` and `HulkShader.asi`. When they are present it will **not**
  also load previous experimental `HulkPC.asi`, to avoid repeating the 4:3
  HUD/16:10 distortion visible in the user's screenshots.
- `HulkBridge.asi`: hooks the game's own original `CreateFileA` IAT import,
  *read-only and opt-in*. `index.ind` and selected `SHADEREF.ARC` or
  `MODELS.ARC` will be resolved from `HulkBridge/` **only** after an
  externally built PC-format source package with `HulkBridge.map` is present.
  If a redirected file cannot be opened, the original PC filename is used.
  Original game resources and DXVK `d3d9.dll` are never changed.
- `HulkShader.asi`: separately hooks original `Direct3DCreate9` ->
  `IDirect3D9::CreateDevice` -> `IDirect3DDevice9::CreateVertexShader` /
  `CreatePixelShader`. Obtains original D3D9 bytecode through `GetFunction`,
  computes FNV-1a32 + CRC32 + byte length, looks for an **explicit** matching
  `HulkBridge/shaders/{VS,PS}_FNV_CRC_LENGTH.hlsl` file, compiles via the
  Windows `d3dcompiler_47.dll` `D3DCompile` API for **vs_3_0/ps_3_0**,
  validates the compiled shader stage, and replaces the original shader
  object **only if** the new one successfully creates. If there is no
  match, HLSL has errors, the DXVK D3D9 device refuses it, or D3DCompiler
  is missing: the original PC shader stays in use. **Raw Xenos shader
  bytecode or unported Sonic/XenosRecomp HLSL is NOT compatible** with
  D3D9 SM3, and cannot be loaded by this mechanism.
- `HulkBridge.ini`: both resource and shader bridges start OFF.

## Original ARC format recovered and handled

The original PC `indexPC.ind` (241,488 B) contains 26 EoR categories with
sorted resource hashes and endian-native `(offset,size,reserved)` records.
A PC ARC also has a SECOND internal directory starting at the little-endian
`uint32` stored at file offset zero. For example, original `MODELS.ARC`
contains 1,823 directory records, `SHADEREF.ARC` contains 36. Each internal
record holds `(hash,offset,size,zero-terminated-name,8-byte FILETIME)` and
is sorted by resource hash, ending with zero padding to a 32-byte file size.

`tools/build_hulk_pc_bridge.py` now truly repacks **both** the external
`index.ind` and the internal ARC name/timestamp directory; an endian-only
index converter would be insufficient and could crash the game. It also
keeps original PC data unchanged and adds explicit PC-format overrides as
separate new records, never modifying the game's installed PC data.

Strict first supported payload types: PC Models (PC endianness, safe model
name and resource hash, original resource record header) and PC
ShaderEffects with genuine paired D3D9 CTAB shader comment tokens.
Xbox 360 Models and Xenos shaders are rejected without genuine translation.
**Textures, TexStreams, new graphics techniques and other Xbox-only types
are not yet supported.** File redirection alone does not teach the game
how to use features absent in the 2008 PC engine.

## Actual completed checks

- All three DLL/ASI images built locally with `clang-cl` + `lld-link` and
  are PE32/i386 without VC++ runtime requirements. `dinput8.dll` has the
  correct `DirectInput8Create` export. Imports use real Windows kernel32.
- PC original `indexPC.ind`: read and repacked **byte-for-byte identical**.
- Original PC `MODELS.ARC` internal directory: 1,823 records read and
  reproduced byte-for-byte, preserving all names/timestamps.
- Original PC `SHADEREF.ARC` internal directory: all 36 records read and
  reproduced byte-for-byte.
- Actual original PC model record used privately to create a consistent
  virtual ARC and index with **1,824 model resources**, and then the new
  internal ARC catalog was decoded and checked. No game data is packaged.
- Original 35/36 PC ShaderEffects pass the strict nonempty PC D3D9 shader
  pair validator; the remaining resource has no embedded shader programs.
- 196 source PC shaders extracted into a non-proprietary identity map:
  98 VS + 98 PS; hashes and original IDs only, not shader bytecode.
- Four Python tests pass: index roundtrip, ARC pack and extra-ID insertion,
  PC-vs-Xenos payload rejection, and shader identity hashes.

## Not yet verified / unresolved

Live Windows gameplay, early loader hook timing, 16:10 HUD layout, dynamic
conversion of the 5,194 original Xbox shaders, 2,977 Xbox-only models,
5,940 Xbox-only textures, 5,836 TexStreams, and additional EoR rendering
passes are NOT implemented by these DLLs. SM3 runtime HLSL compilation is
not an Xbox-to-PC shader translator. The validated Xenos Vulkan shader
bank belongs to the separate recomp effort, not the PC D3D9 API.

Do not replace original `index.ind`, install raw Xbox `.ARC` files, disable
DXVK, or enable empty resource mappings. No game build of the ongoing x64
EoR recomp was launched for this new PC mod code.
