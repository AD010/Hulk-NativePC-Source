// A crash report: when the game crashes on Windows, crash-report.txt beside the exe gets the exception, its address
// and the call stack. With incredible_hulk.pdb beside the exe (tools/build_game.bat symbols) the frames have names,
// and a recompiled frame's name is the guest function it came from (sub_82XXXXXX).
// Installed as the unhandled exception filter, so it only runs after the runtime's own handlers have given up.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace hulk {
namespace {

LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS* info) {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (!n || n >= MAX_PATH) return EXCEPTION_CONTINUE_SEARCH;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return EXCEPTION_CONTINUE_SEARCH;
  wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"crash-report.txt");
  FILE* f = _wfopen(path, L"w");
  if (!f) return EXCEPTION_CONTINUE_SEARCH;

  const EXCEPTION_RECORD* rec = info->ExceptionRecord;
  std::fprintf(f, "exception 0x%08lX at %p\n", rec->ExceptionCode, rec->ExceptionAddress);
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
    std::fprintf(f, "%s of %p\n", rec->ExceptionInformation[0] ? "write" : "read",
                 reinterpret_cast<void*>(rec->ExceptionInformation[1]));

  HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(process, nullptr, TRUE);
  CONTEXT ctx = *info->ContextRecord;
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  for (int i = 0; i < 48; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                     SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset)
      break;
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 offset = 0;
    const DWORD64 module = SymGetModuleBase64(process, frame.AddrPC.Offset);
    char module_name[MAX_PATH] = "?";
    if (module) GetModuleFileNameA(reinterpret_cast<HMODULE>(module), module_name, MAX_PATH);
    const char* short_name = strrchr(module_name, '\\') ? strrchr(module_name, '\\') + 1 : module_name;
    if (SymFromAddr(process, frame.AddrPC.Offset, &offset, sym))
      std::fprintf(f, "%2d %s!%s+0x%llX\n", i, short_name, sym->Name, offset);
    else
      std::fprintf(f, "%2d %s+0x%llX\n", i, short_name, frame.AddrPC.Offset - module);
  }
  std::fclose(f);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashReport() { SetUnhandledExceptionFilter(&WriteCrashReport); }

}  // namespace hulk
#else
namespace hulk {
void InstallCrashReport() {}
}  // namespace hulk
#endif
