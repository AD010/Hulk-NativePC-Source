#include "win32_min.h"
// For the verified 2008 HULK.EXE x86 only. Always forward original DInput8.
static HMODULE original_dinput;
static HMODULE SystemDInput8(void){
 char path[MAX_PATH];DWORD n=GetSystemDirectoryA(path,MAX_PATH);
 if(n<3||n>=MAX_PATH-15)return (HMODULE)0;
 if(path[n-1]!='\\')path[n++]='\\';
 if(!HulkAppend(path,MAX_PATH,n,"dinput8.dll"))return (HMODULE)0;
 if(!original_dinput)original_dinput=LoadLibraryA(path);
 return original_dinput;
}
__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst,DWORD version,
                  const void* iid,void** output,void* unknown){
 HMODULE real=SystemDInput8();
 if(!real)return (HRESULT)0x80004005u;
 typedef HRESULT (WINAPI* Factory)(HINSTANCE,DWORD,const void*,void**,void*);
 Factory f=(Factory)GetProcAddress(real,"DirectInput8Create");
 return f?f(inst,version,iid,output,unknown):(HRESULT)0x80004005u;
}
static DWORD WINAPI LoadModules(LPVOID unused){
 (void)unused;
 char folder[MAX_PATH];unsigned end=0;
 if(!HulkEndsAt(folder,MAX_PATH,&end))return 0;
 // Loading both is safe: the resource module touches only the original
 // CreateFileA import; shader module touches only the D3D9 Create9 import.
 // Do not ALSO load an old HulkPC.asi with incompatible D3D9/HUD hooks.
 const char* modules[2]={"HulkBridge.asi","HulkShader.asi"};
 unsigned found=0;
 for(unsigned i=0;i<2;++i){
  if(!HulkAppend(folder,MAX_PATH,end,modules[i]))return 0;
  if(GetFileAttributesA(folder)!=0xffffffffu){
   found=1;
   if(!LoadLibraryA(folder))OutputDebugStringA("[HulkBridge] ASI module failed loading\n");
  }
 }
 if(found)return 0;
 if(HulkAppend(folder,MAX_PATH,end,"HulkPCProbe.asi")){
  if(LoadLibraryA(folder))return 0;
 }
 if(HulkAppend(folder,MAX_PATH,end,"HulkPC.asi")){
  if(LoadLibraryA(folder))return 0;
 }
 return 0;
}
BOOL WINAPI DllMain(HMODULE dll,DWORD reason,LPVOID reserved){
 (void)reserved;
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(dll);
  HANDLE task=CreateThread(0,0,LoadModules,0,0,0);
  if(task)CloseHandle(task);
 }
 return TRUE;
}