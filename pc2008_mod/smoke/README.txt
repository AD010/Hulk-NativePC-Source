The Incredible Hulk 2008 PC - original PE32 Win32 DirectInput ASI probe

The included source builds dinput8.dll + HulkPCProbe.asi.
This is a native, read-only Direct3D9 injection smoke test.
It does NOT implement the Xbox renderer, 16:10, Havok or DLSS.

Install.cmd verifies HULK.EXE SHA-256 before copying either file.
The installer refuses existing DLL conflicts; the original EXE and saves
are untouched. When the plugin starts, it records HulkPC_ASI.log in the
game folder. On verified import hook success it records D3D9 CreateDevice
and IDirect3DDevice9::Present frame rate observations. In the case of a
hook failure, it falls back to original unmodified graphics.

Build locally with Python build_x86_smoke.py plus clang-cl/lld-link.
When using real Win32 SDK, the advanced HulkPC.asi project is separate.
Game behavior is untested until actually run on the owner's Windows PC.
