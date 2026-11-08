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

Additional bounded NativeX visual recovery:
The prior color fallback ran only if FindNewest returned a null pointer.
But an existing stale/black resolved front texture can satisfy that check.
Now a freshly resolved full-screen color image (exact guest 1280x720)
can supersede stale front data by last_resolve sequence. It cannot
supersede an equally new or newer direct fetch. The native log records
direct_found, direct_seq, fallback and selected address. Logging starts
during initial 12 frames and recurs every 60 native frames, rather than
one entry per 600; important if the guest swap rate is 30 FPS. This is
still a heuristic until the user confirms nonblack correctly composed
gameplay. No Xenos renderer snippets used in the native compositor.
