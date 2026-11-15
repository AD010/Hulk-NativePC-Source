#include "win32_min.h"
// The verified original 2008 HULK.EXE imports only DirectInput8Create.
// This game-local proxy ALWAYS forwards the real Windows system input DLL.
static HMODULE real_dinput8;
static HMODULE LoadSystemDInput(void) {
  char p[MAX_PATH];DWORD n=GetSystemDirectoryA(p,MAX_PATH);
  if(!n || n+14u>=MAX_PATH)return (HMODULE)0;
  p[n]=0;
  if(p[n-1]!='\\')p[n++]='\\';
  if(!HulkAppend(p,MAX_PATH,n,"dinput8.dll"))return (HMODULE)0;
  if(!real_dinput8)real_dinput8=LoadLibraryA(p);
  return real_dinput8;
}
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE instance,DWORD version,
                            const void* iid,void** output,void* outer) {
  HMODULE real=LoadSystemDInput();
  if(!real)return (HRESULT)0x80004005u;
  typedef HRESULT (WINAPI* Fn)(HINSTANCE,DWORD,const void*,void**,void*);
  Fn forward=(Fn)GetProcAddress(real,"DirectInput8Create");
  return forward?forward(instance,version,iid,output,outer):(HRESULT)0x80004005u;
}
static DWORD WINAPI LoadHulkAsi(LPVOID unused) {
  (void)unused;
  char path[MAX_PATH];unsigned end=0;
  if(!HulkEndsAt(path,MAX_PATH,&end))return 0;
  // Load only the owner's explicitly selected Hulk plugin, never arbitrary ASIs.
  // Safe diagnostic probe wins over an optional graphics extension.
  // Never load both, so D3D9 vtables are not hooked by competing ASIs.
  if(HulkAppend(path,MAX_PATH,end,"HulkPCProbe.asi")) {
    if(LoadLibraryA(path))return 0;
  }
  if(HulkAppend(path,MAX_PATH,end,"HulkPC.asi")) {
    if(LoadLibraryA(path))return 0;
  }
  OutputDebugStringA("Hulk PC dinput8 proxy loaded but no HulkPC.asi found\n");
  return 0;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID reserved) {
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    HANDLE t=CreateThread((LPVOID)0,0,LoadHulkAsi,(LPVOID)0,0,(DWORD*)0);
    if(t)CloseHandle(t);
  }
  return TRUE;
}
