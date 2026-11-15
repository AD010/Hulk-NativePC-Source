#include "win32_min.h"
// Pure read-only diagnostic for exact 2008 original PC HULK.EXE.
// No shader, camera, save data, game executable or texture is modified.
static void* orig_d3d;
static void* orig_create_device;
static void* orig_present;
static unsigned frame_count;
static DWORD last_tick;
static char log_path[MAX_PATH];
static void Log(const char* message) {
  if(!log_path[0])return;
  HANDLE f=CreateFileA(log_path,FILE_APPEND_DATA,FILE_SHARE_READ,
                       (LPVOID)0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,(HANDLE)0);
  if(f==(HANDLE)(-1))return;
  DWORD written=0;
  WriteFile(f,message,HulkLen(message),&written,(LPVOID)0);
  CloseHandle(f);
}
static void UIntString(unsigned value,char* text,unsigned size) {
  char reverse[16];unsigned n=0,i=0;
  if(!size)return;
  do{reverse[n++]=(char)('0'+value%10u);value/=10u;}while(value && n<15);
  while(n && i+1<size)text[i++]=reverse[--n];text[i]=0;
}
static int VerifiedHulk(void) {
  unsigned char* p=(unsigned char*)GetModuleHandleW((LPCWSTR)0);
  if(!p || p[0]!='M'||p[1]!='Z')return 0;
  unsigned pe=*(unsigned*)(p+0x3c);
  if(pe!=0x40)return 0;
  if(p[pe]!='P'||p[pe+1]!='E')return 0;
  return *(unsigned short*)(p+pe+4)==0x14c &&
         *(unsigned*)(p+pe+8)==0x48503ec6u &&
         *(unsigned*)(p+pe+24+16)==0x24e129u &&
         *(unsigned*)(p+pe+24+56)==0x81b000u;
}
static int PatchSlot(void** slot,void* replacement,void** original) {
  if(!slot || !replacement)return 0;
  DWORD protect=0;
  if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&protect))return 0;
  if(*slot!=replacement) {
    if(!*original) {
      *original=*slot;
      HulkSwap((LPVOID volatile*)slot,replacement);
    } else if(*slot==*original) {
      HulkSwap((LPVOID volatile*)slot,replacement);
    } else {
      DWORD ignored=0;VirtualProtect(slot,4,protect,&ignored);return 0;
    }
  }
  {DWORD ignored=0;VirtualProtect(slot,4,protect,&ignored);}
  return 1;
}
typedef void* (WINAPI* D3D9CreateFn)(unsigned);
typedef HRESULT (WINAPI* DeviceCreateFn)(void*,unsigned,unsigned,void*,unsigned,void*,void**);
typedef HRESULT (WINAPI* DevicePresentFn)(void*,const void*,const void*,void*,const void*);
static HRESULT WINAPI ProbePresent(void* device,const void* source,
                                   const void* dest,void* window,const void* dirty) {
  if(frame_count++>=299) {
    DWORD now=GetTickCount();
    if(last_tick && now>last_tick) {
      const unsigned fps10=3000000u/(now-last_tick);
      char num[20],message[100];unsigned i=0,j=0;
      UIntString(fps10/10u,num,sizeof(num));
      const char* pre="[D3D9] 300 Present calls, FPS=";
      while(pre[i]&&i<sizeof(message)-1)message[j++]=pre[i++];
      for(i=0;num[i]&&j<sizeof(message)-4;i++)message[j++]=num[i];
      message[j++]='.';message[j++]=(char)('0'+(fps10%10));
      message[j++]='\r';message[j++]='\n';message[j]=0;
      Log(message);
    }
    last_tick=now;frame_count=0;
  }
  return ((DevicePresentFn)orig_present)(device,source,dest,window,dirty);
}
static HRESULT WINAPI ProbeCreateDevice(void* api,unsigned adapter,
      unsigned type,void* window,unsigned flags,void* params,void** device) {
  const HRESULT result=((DeviceCreateFn)orig_create_device)(
      api,adapter,type,window,flags,params,device);
  if(result>=0 && device && *device) {
    void** vtable=*(void***)(*device);
    if(PatchSlot(&vtable[17],(void*)ProbePresent,&orig_present))
      Log("[D3D9] IDirect3DDevice9::Present (index 17) hook installed\r\n");
  }
  return result;
}
static void* WINAPI ProbeD3D9Create(unsigned sdk_version) {
  void* api=((D3D9CreateFn)orig_d3d)(sdk_version);
  if(api) {
    void** vtable=*(void***)(api);
    if(PatchSlot(&vtable[16],(void*)ProbeCreateDevice,&orig_create_device))
      Log("[D3D9] IDirect3D9::CreateDevice (index 16) hook installed\r\n");
  }
  return api;
}
static int TryPatchIAT(void) {
  unsigned char* exe=(unsigned char*)GetModuleHandleW((LPCWSTR)0);
  HMODULE d3d9=GetModuleHandleA("d3d9.dll");
  if(!exe || !d3d9)return 0;
  void* expected=(void*)GetProcAddress(d3d9,"Direct3DCreate9");
  void** thunk=(void**)(exe+0x003cc310u); // verified ONLY for the exact HULK.EXE.
  if(!expected || *thunk!=expected)return 0;
  return PatchSlot(thunk,(void*)ProbeD3D9Create,&orig_d3d);
}
static DWORD WINAPI StartProbe(LPVOID ignored) {
  (void)ignored;
  char path[MAX_PATH];unsigned dir=0;
  if(!HulkEndsAt(path,MAX_PATH,&dir))return 0;
  if(!HulkAppend(path,MAX_PATH,dir,"HulkPC_ASI.log"))return 0;
  {unsigned i=0;for(;path[i]&&i<MAX_PATH-1;++i)log_path[i]=path[i];log_path[i]=0;}
  if(!VerifiedHulk()) {
    Log("[ASI] x86 DLL loaded; unrecognized HULK.EXE. No graphics hook installed.\r\n");
    return 0;
  }
  Log("[ASI] Original Hulk PC 2008 x86 identified; game EXE unchanged.\r\n");
  for(unsigned i=0;i<250;++i) {
    if(TryPatchIAT()) {Log("[D3D9] Direct3DCreate9 import hooked.\r\n");return 0;}
    Sleep(10);
  }
  Log("[D3D9] Could not hook Direct3DCreate9 import; no changes made.\r\n");
  return 0;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID reserved) {
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    HANDLE t=CreateThread((LPVOID)0,0,StartProbe,(LPVOID)0,0,(DWORD*)0);
    if(t)CloseHandle(t);
  }
  return TRUE;
}
