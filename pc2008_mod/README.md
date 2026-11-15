# The Incredible Hulk (2008) — official PC DX9 / ASI alternative

This is a **separate optional host-game enhancement route** within the owner's
existing NativePC source repository, NOT a replacement game ZIP or proof that
the Xbox 360 renderer has been recompiled. It intentionally does not add a
new repository or Windows release workflow.

## Verified target, using the owner's original PC executable

- Original private HULK.EXE: 4,709,888 bytes, SHA-256
  \`6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233\`.
- PE32/i386, TimeDateStamp 0x48503EC6 (2008-06-11), entry RVA
  0x24E129, SizeOfImage 0x81B000.
- Direct import **dinput8.dll!DirectInput8Create**.
- Direct imports **d3d9.dll!Direct3DCreate9** and D3DPERF event functions.
  Original import thunk RVA **0x003CC310** for Direct3DCreate9, only for
  this exact verified executable (not a universal game signature).
- D3DX is \`d3dx9_35.dll\`, so the game renders using **native Direct3D 9**
  without any Xenia, ReXGlue, Xbox Xenos or guest PM4 at runtime.

The freely available **Ultimate ASI Loader v10.0.2 x86**, by ThirteenAG,
supports the \`dinput8.dll\` proxy:
https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2

The independently built \`smoke\` sources also provide a game-specific 32-bit
\`dinput8.dll\` forwarder and \`HulkPCProbe.asi\` diagnostic. The probe
reports the loader and actual Direct3DCreate9 / CreateDevice / Present
hook chain in \`HulkPC_ASI.log\`; no shaders, camera, save data or game
executable are modified. Source is available in \`pc2008_mod/smoke/\`.
The x86 PE and export/import tables were statically verified on 2026-11-15.
The real game was **not** executed on Windows here.

## Real original resource comparison

Source: the owner's real PC and Xbox 360 IND/ARC files, audited read-only by
\`tools/audit_pc_x360_effects.py\`. Group counts below are original index
records, NOT binary-compatible objects:

| Group | PC 2008 | Xbox 360 | Identical resource hashes |
| --- | ---: | ---: | ---: |
| ShaderEffects | 36 | 233 | 7 |
| Models | 1,823 | 4,174 | 1,197 |
| Textures | 1,365 | 6,669 | 729 |
| Objects | 2,444 | 3,938 | 1,866 |
| PclEffects | 310 | 365 | 305 |

The PC ShaderEffects archive is **143,712 bytes**, has **196 CTAB tables**
from **98 vertex + 98 pixel D3D9 shaders** (SM2), and carries no Xenos
shader containers. The Xbox ShaderEffects archive is **5,913,920 bytes**
with **5,194 original Xenos ShaderContainers** (2,597 VS + 2,597 PS).
PC has only 7 ShaderEffect hashes in common with Xbox.
The PC shader compiler metadata contains **91 VS programs with
ViewProj c11-c14** and **81 VS programs with World c7-c10**.
These are original DX9 CTAB bindings, NOT validated camera offsets or
guaranteed 16:10 fixes.

**Never replace PC SHADEREF.ARC with the Xbox version.** Xbox shaders use
different bytecode, material/pass programs, texture formats and render-state
semantics. The correct PC route is to import source data one material at
a time, translate Xbox shader logic to the PC D3D9 shader model and
rebind compatible PC objects, or replace selected PC D3D9 rendering stages
through a proper D3D9 host hook. There is no drop-in console shader ZIP.

## Building an optional native PC enhancement plugin

The separate \`CMakeLists.txt\` builds **Win32** \`HulkPC.asi\` using
Visual Studio Windows SDK and static MSVC CRT (\`/MT\`). Original EXE is
NOT linked or included. The advanced plugin source is in \`src/\`.
It can measure created VS/PS and Present FPS, optionally force supported
full-screen resolution, optional anisotropic filtering, and has a disabled
experimental ViewProj adjustment guarded by actual shader reflection.
All graphics-changing options default **off**; none implies a verified
Xbox graphics port.

Until a Windows game test proves shader RT parity, camera state, Havok
call ABI, and PC save integration, *do not publish a complete remaster*
or change the project's x64 ReXGlue Vulkan release workflow to this mode.


## November 15, 2026: actual freestanding Win32 ASI plugin artifacts

A private **x86** cross-compiled package was produced independently from
the ReXGlue/NativeX Windows x64 CI and made available in the conversation:

- `bin/dinput8.dll` SHA-256
  `4553d4e4d3778792273ba2e5f60f2e146ae0836b72743058ff11f0028df82fc8`
  (exports `DirectInput8Create`, forwards it to system32).
- `bin/HulkPCProbe.asi` SHA-256
  `c849cda689f15f260518d305ae0c708fbbce6c6f58d56418442e960cb1d451ec`
  (read-only D3D9 CreateDevice/Present diagnostic).
- `bin/HulkPC.asi` SHA-256
  `9abec84d4940549441ea930f15c8991473fb76c6e3919e25e853a29aaac3ccd7`
  (optional native D3D9 shader CTAB/constant sampler interceptor).
- ZIP `Hulk_2008_PC_ASI_Probe_x86.zip` SHA-256
  `0431cb6354f2c8bc00bf209a0851f46afdc4c260f8b636d7289a278997a5c831`.

The ZIP contains the extra freestanding `source/HulkPC.c`,
`source/build_x86_smoke.py`, `source/win32_min.h` and installer
scripts. The default `Install.cmd` installs ONLY the read-only probe.
The separate `Install-Enhanced.cmd` installs the optional D3D9
extension with all graphics-changing options OFF; the
`Install-Enhanced-2560x1600.cmd` variant requests physical
2560x1600 fullscreen output and 16x anisotropic filtering. The ASI
loader prioritizes the diagnostic probe over the optional extension,
and the enhanced installer refuses to run while a probe is present.

All executables are PE32/i386, statically linked and depend only on
real Windows kernel32, not on additional VC++ redistributables. Both
the PE metadata and import/export names were statically audited, but
**no live original Hulk PC gameplay was run**. The 16:10
ViewProj rewrite is separately opt-in and deliberately disabled
because shader matrix storage/camera ownership remain unverified.
No original game EXE, saves or textures are bundled.

Do not claim the optional Win32 program has imported Xbox geometry,
textures, Havok or shaders, or that it implements DLSS/FSR Frame
Generation. Those are independent and substantially incomplete tasks.

The advanced plugin now uses native Win32 process-heap allocation for its
8-KiB reflection buffers and transient VS constants instead of disabling
stack probing on an unbounded stack array. The diagnostic loader takes
precedence when both plugins are present. The output ZIP is updated to
24,912 bytes with 18 tested entries. The freestanding C source for the
advanced x86 build is bundled in the package's source/HulkPC.c, separately
from the Windows SDK C++ implementation in src/HulkPC.cpp.
