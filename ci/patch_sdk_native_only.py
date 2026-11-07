#!/usr/bin/env python3
"""Configure the cloned ReXGlue SDL3 for Vulkan windowing and no physical joystick.

This patches the *vendored SDK source*: SDL_RENDER and SDL_VULKAN are forced
OFF by ReXGlue thirdparty/CMakeLists.txt, so passing -D flags is insufficient.
The guest title's XInput ABI stays present: Hulk maps Win32 keyboard/mouse to
guest XINPUT_STATE without requiring a physical SDL joystick.
"""
from pathlib import Path
import sys

def patch_sdk(path: Path) -> None:
    source = path.read_text(encoding="utf-8-sig")
    changes = (
        ('set(SDL_RENDER OFF CACHE BOOL "" FORCE)',
         'set(SDL_RENDER ON CACHE BOOL "" FORCE)\n'
         'set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)\n'
         'set(SDL_GAMEPAD OFF CACHE BOOL "" FORCE)'),
        ('    # Linux picks X11 or Wayland at runtime, so the surface has to come from\n'
         '    # whichever driver SDL chose. Windows presenters go straight from the HWND.\n'
         '    if(UNIX)\n'
         '        set(SDL_VULKAN ON CACHE BOOL "" FORCE)\n'
         '    else()\n'
         '        set(SDL_VULKAN OFF CACHE BOOL "" FORCE)\n'
         '    endif()',
         '    # Native Vulkan on Windows and Linux; no SDL physical joystick backend.\n'
         '    set(SDL_VULKAN ON CACHE BOOL "" FORCE)'),
        ('# Video provides the windowing/event layer; presenters create their own\n'
         '# D3D12/Vulkan surfaces from native handles, so SDL_RENDER stays off.',
         '# SDL video and 2D renderer are enabled; the actual Hulk 3D path is\n'
         '# NativeXRenderer/DiligentCore Vulkan, not SDL_Renderer.'),
    )
    for before, after in changes:
        if source.count(before) == 1:
            source = source.replace(before, after)
        elif source.count(after) == 1:
            # Already updated in an SDK checkout or build-cache restoration.
            continue
        else:
            raise ValueError(f"unexpected upstream SDL config (missing/duplicated {before[:75]})")
    path.write_text(source, encoding="utf-8", newline="\n")

def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: patch_sdk_native_only.py PATH_TO_REXGLUE_SDK", file=sys.stderr)
        return 2
    sdk = Path(sys.argv[1])
    path = sdk / "thirdparty" / "CMakeLists.txt"
    if not path.is_file():
        print(f"Missing vendored SDL configuration: {path}", file=sys.stderr)
        return 1
    try:
        patch_sdk(path)
    except ValueError as exc:
        print(f"Refusing to silently ignore SDL configuration: {exc}", file=sys.stderr)
        return 1
    print("ReXGlue SDK: SDL_RENDER ON; SDL_JOYSTICK OFF; SDL_GAMEPAD OFF; SDL_VULKAN ON")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
