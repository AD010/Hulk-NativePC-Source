# Hulk NativePC crash collection (Windows)

`native_port/src/incredible_hulk_app.h` already installs `hulk::InstallCrashReport()` in `OnPostSetup`, after the ReXGlue runtime's own exception handlers. The renderer is **not** guaranteed stable yet.

The unhandled exception filter now keeps its existing `crash-report.txt` and attempts to write a compact `crash-report.dmp` in the same directory. It uses `MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules`: includes the failed native thread, module list and CPU context but not a full-process memory dump. If the EXE directory is read-only, both files fall back to `%LOCALAPPDATA%\\HulkNativePC`. Each new crash overwrites the prior same-name report: copy it elsewhere before reproducing another.

**Private:** DMPs may include program data and should **never** be committed to the public source repository, sent to public GitHub Actions, uploaded into a public release, or attached to a public issue. Do not send full-process-memory dumps. Inspect privately in WinDbg if one exists.

Historical evidence: the previous `crash-report.txt` showed `0xC0000005` reading `0x10` in `nvgpucomp64.dll` / `nvoglv64.dll` during shader compilation; disabling persistent pipeline storage and clearing the cache changed this behavior. This does not prove other crashes are in the driver. `incredible_hulk_153.log` and `incredible_hulk_160.log` both end with normal window shutdown and `Execution complete`, so neither identifies the user's **remaining** crash. Capturing an up-to-date report is necessary for a root-cause patch.

The filter deliberately returns `EXCEPTION_CONTINUE_SEARCH`, allowing normal Windows Error Reporting and debugger handling. It cannot catch GPU DEVICE_LOST without an exception, TerminateProcess, process kills, power loss, all unhandled C++ aborts or a deadlock/hang. The minidump writer is best-effort under severe heap/stack corruption. This is crash **diagnostics**, not a claimed crash fix.
