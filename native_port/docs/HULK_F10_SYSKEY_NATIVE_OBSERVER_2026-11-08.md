# Hulk F10 system-key and GPU observer

Log 100 at 2026-11-08 14:26:26–14:32:16 Kyiv used Xenos Vulkan
FIFO presentation (mode 2); the title shut down cleanly after 5m50s.
No NativeX initialization appears in that log, so it cannot diagnose
the separately reported native-black image.

Old Win32 subclass listened only for WM_KEYDOWN when pressing F10.
Windows commonly emits WM_SYSKEYDOWN for F10 because it is a system
menu key. Fix WM_SYSKEYDOWN plus release suppression; log F10 state.
Also log F11 receipt without consuming the SDK verification binding.
F11 GPU verification is not active in Xenos-only sessions unless the
optional verify-only mode is explicitly enabled.
Source-only validation; a real Windows hotkey test is still required.

NativeXRenderer pin bf965e9 contains guest-frame sync option
native_x_renderer_present_with_swaps (default false). When enabled,
its own renderer thread uses ReXGlue GetSwapCounts/SetSwapObserver to
present only after guest GPU SWAP execution, not after every called
XDK Present. Log079 emitted roughly 417 native frame events/sec while
the game itself was capped at 30; this mismatched cadence can amplify
resource pressure and GPU load. Setting it true can improve pacing;
it does not establish framebuffer correctness and requires SDK support.
