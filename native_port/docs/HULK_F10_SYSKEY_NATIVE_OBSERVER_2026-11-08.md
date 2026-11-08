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
