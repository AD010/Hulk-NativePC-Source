#include "win32_min.h"
// Runtime HLSL 3.0 bridge for original HULK.EXE's existing D3D9 VS/PS.
// Only exact bytecode identities may be replaced, never raw Xbox microcode.
// Errors ALWAYS fall back to the original PC shader. DXVK stays d3d9.dll.
#define HB_OPEN_EXISTING 3u
#define HB_READ 0x80000000u
#define HB_MAX_SOURCE 131072u
#define HB_MAX_BYTECODE 524288u
#define HB_FNV_INIT 2166136261u
static void* real_d3d9;
static void* real_device;
static void* real_vs;
static void* real_ps;
static char game_root[MAX_PATH];
static int hlsl_enabled;
static volatile long debug_written;

typedef void* (WINAPI* Create9Fn)(unsigned);
typedef HRESULT (WINAPI* DeviceFn)(void*,unsigned,unsigned,void*,unsigned,void*,void**);
typedef HRESULT (WINAPI* ShaderFn)(void*,const void*,void**);
typedef HRESULT (WINAPI* BlobFunction)(void*,void*,unsigned*);
typedef unsigned (WINAPI* BlobSize)(void*);
typedef void* (WINAPI* BlobBuffer)(void*);
typedef unsigned (WINAPI* ReleaseFn)(void*);
typedef HRESULT (WINAPI* CompileFn)(const void*,unsigned,const char*,void*,void*,
                                    const char*,const char*,unsigned,unsigned,
                                    void**,void**);
static int VerifiedHulk(void){
 unsigned char* exe=(unsigned char*)GetModuleHandleW((LPCWSTR)0);
 if(!exe||exe[0]!='M'||exe[1]!='Z')return 0;
 unsigned pe=*(unsigned*)(exe+0x3c);
 if(pe!=0x40||exe[pe]!='P'||exe[pe+1]!='E')return 0;
 return *(unsigned short*)(exe+pe+4)==0x14c &&
        *(unsigned*)(exe+pe+8)==0x48503ec6u &&
        *(unsigned*)(exe+pe+24+16)==0x24e129u &&
        *(unsigned*)(exe+pe+24+56)==0x81b000u;
}
static int HookSlot(void** slot,void* replacement,void** original){
 if(!slot||!replacement||!(*slot))return 0;
 DWORD access=0;
 if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&access))return 0;
 int success=1;
 if(*slot!=replacement){
  if(!*original)*original=*slot;
  if(*slot!=*original)success=0;
  else HulkSwap((LPVOID volatile*)slot,replacement);
 }
 {DWORD ignored=0;VirtualProtect(slot,4,access,&ignored);}
 return success;
}
static DWORD Fnv32(const unsigned char* bytes,unsigned n){
 DWORD result=HB_FNV_INIT;
 for(unsigned i=0;i<n;++i)result=(result^bytes[i])*16777619u;
 return result;
}
static DWORD Crc32(const unsigned char* bytes,unsigned n){
 DWORD result=0xffffffffu;
 for(unsigned i=0;i<n;++i){
  result^=bytes[i];
  for(unsigned bit=0;bit<8;++bit)
   result=(result>>1)^((result&1)?0xedb88320u:0);
 }
 return result^0xffffffffu;
}
static void Hex8(char* out,DWORD value){
 static const char digits[]="0123456789ABCDEF";
 for(unsigned i=0;i<8;++i)out[i]=digits[(value>>(28-4*i))&15];
 out[8]=0;
}
static int Filename(char* dest,unsigned cap,const char* stage,DWORD fnv,DWORD crc,DWORD bytes){
 char first[9],second[9],third[9];
 Hex8(first,fnv);Hex8(second,crc);Hex8(third,bytes);
 unsigned at=0;const char* parts[6]={game_root,"HulkBridge\\shaders\\",stage,first,second,third};
 for(unsigned j=0;j<6;++j){
  const char* p=parts[j];
  for(unsigned i=0;p[i];++i){if(at+1>=cap)return 0;dest[at++]=p[i];}
  if(j==3 || j==4){if(at+1>=cap)return 0;dest[at++]='_';}
 }
 const char* suffix=".hlsl";
 for(unsigned i=0;suffix[i];++i){if(at+1>=cap)return 0;dest[at++]=suffix[i];}
 dest[at]=0;return 1;
}
static unsigned SafeReadSource(const char* path,unsigned char** result){
 HANDLE f=CreateFileA(path,HB_READ,1,0,HB_OPEN_EXISTING,0,0);
 if(f==(HANDLE)-1)return 0;
 DWORD size=GetFileSize(f,0);
 if(size<40||size>HB_MAX_SOURCE){CloseHandle(f);return 0;}
 unsigned char* source=(unsigned char*)HeapAlloc(GetProcessHeap(),0,size+1);
 if(!source){CloseHandle(f);return 0;}
 DWORD bytes=0;BOOL read=ReadFile(f,source,size,&bytes,0);
 CloseHandle(f);
 if(!read||bytes!=size){HeapFree(GetProcessHeap(),0,source);return 0;}
 const char* marker="// HULKBRIDGE-HLSL1";
 unsigned i=0;
 for(;marker[i] && i<size;++i)if(source[i]!=(unsigned char)marker[i])break;
 if(marker[i] || (source[i]!='\r' && source[i]!='\n')){
  HeapFree(GetProcessHeap(),0,source);return 0;
 }
 source[size]=0;*result=source;return size;
}
static void MaybeReplace(void* device,void** output,int vertex,ShaderFn create){
 if(!hlsl_enabled||!output||!*output)return;
 void* original=*output;
 void** methods=*(void***)(original);
 if(!methods||!methods[4]||!methods[2])return;
 BlobFunction get=(BlobFunction)methods[4];
 unsigned length=0;
 if(get(original,0,&length)<0 || length<16 || length>HB_MAX_BYTECODE)return;
 const HANDLE heap=GetProcessHeap();
 unsigned char* code=(unsigned char*)HeapAlloc(heap,0,length);
 if(!code)return;
 if(get(original,code,&length)<0){HeapFree(heap,0,code);return;}
 if(length<16||length>HB_MAX_BYTECODE){HeapFree(heap,0,code);return;}
 const DWORD ver=*(const DWORD*)code;
 if(ver!=(vertex?0xfffe0200u:0xffff0200u) &&
    ver!=(vertex?0xfffe0300u:0xffff0300u)){
  HeapFree(heap,0,code);return;
 }
 DWORD fnv=Fnv32(code,length),crc=Crc32(code,length);
 HeapFree(heap,0,code);
 char path[MAX_PATH];
 if(!Filename(path,MAX_PATH,vertex?"VS_":"PS_",fnv,crc,length))return;
 unsigned char* hlsl=0;
 const unsigned len=SafeReadSource(path,&hlsl);
 if(!len)return;
 HMODULE compiler=LoadLibraryA("d3dcompiler_47.dll");
 CompileFn compile=compiler?(CompileFn)GetProcAddress(compiler,"D3DCompile"):(CompileFn)0;
 if(!compile){HeapFree(heap,0,hlsl);return;}
 void *binary=0,*errors=0;
 const HRESULT hr=compile(hlsl,len,path,0,0,"main",
                          vertex?"vs_3_0":"ps_3_0",0,0,&binary,&errors);
 HeapFree(heap,0,hlsl);
 if(errors){
  void** t=*(void***)(errors);
  if(t && t[2])((ReleaseFn)t[2])(errors);
 }
 if(hr<0||!binary)return;
 void** vt=*(void***)(binary);
 BlobBuffer buffer=(BlobBuffer)vt[3];BlobSize count=(BlobSize)vt[4];
 unsigned bytes=count(binary);
 void* data=buffer(binary);
 if(data && bytes>=16 && bytes<=HB_MAX_BYTECODE &&
    *(const DWORD*)data==(vertex?0xfffe0300u:0xffff0300u)){
  void* replacement=0;
  const HRESULT result=create(device,data,&replacement);
  if(result>=0 && replacement){
   // We created the original first, so its lifetime ends before replacing
   // the returned pointer with a valid D3D9 shader object.
   ((ReleaseFn)methods[2])(original);
   *output=replacement;
   if(InterlockedExchange(&debug_written,1)==0)
    OutputDebugStringA("[HulkShader] verified D3D9 shader HLSL override applied\n");
  }
 }
 if(vt[2])((ReleaseFn)vt[2])(binary);
}
static HRESULT WINAPI ShaderVS(void* device,const void* bytes,void** output){
 const HRESULT hr=((ShaderFn)real_vs)(device,bytes,output);
 if(hr>=0)MaybeReplace(device,output,1,(ShaderFn)real_vs);
 return hr;
}
static HRESULT WINAPI ShaderPS(void* device,const void* bytes,void** output){
 const HRESULT hr=((ShaderFn)real_ps)(device,bytes,output);
 if(hr>=0)MaybeReplace(device,output,0,(ShaderFn)real_ps);
 return hr;
}
static HRESULT WINAPI ShaderDevice(void* api,unsigned adapter,unsigned type,
       void* hwnd,unsigned flags,void* params,void** result){
 const HRESULT hr=((DeviceFn)real_device)(api,adapter,type,hwnd,flags,params,result);
 if(hr>=0 && result && *result){
  void** v=*(void***)(*result);
  if(v){HookSlot(&v[91],(void*)ShaderVS,&real_vs);
        HookSlot(&v[106],(void*)ShaderPS,&real_ps);}
 }
 return hr;
}
static void* WINAPI ShaderD3D9(unsigned version){
 if(!real_d3d9)return 0;
 void* api=((Create9Fn)real_d3d9)(version);
 if(api){void** v=*(void***)(api);
         if(v)HookSlot(&v[16],(void*)ShaderDevice,&real_device);}
 return api;
}
static DWORD WINAPI Worker(LPVOID ignored){
 (void)ignored;
 if(!VerifiedHulk())return 0;
 char path[MAX_PATH],ini[MAX_PATH];unsigned dir=0;
 if(!HulkEndsAt(path,MAX_PATH,&dir))return 0;
 path[dir]=0;
 unsigned i=0;
 for(;path[i]&&i<MAX_PATH-1;++i)game_root[i]=path[i];game_root[i]=0;
 if(!HulkAppend(path,MAX_PATH,dir,"HulkBridge.ini"))return 0;
 unsigned k=0;for(;path[k]&&k<MAX_PATH-1;++k)ini[k]=path[k];ini[k]=0;
 hlsl_enabled=GetPrivateProfileIntA("ShaderBridge","EnableRuntimeHlsl",0,ini)==1;
 if(!hlsl_enabled)return 0;
 unsigned char* exe=(unsigned char*)GetModuleHandleW((LPCWSTR)0);
 HMODULE d3d=0;
 for(unsigned attempt=0;attempt<250 && !(d3d=GetModuleHandleA("d3d9.dll"));++attempt)Sleep(10);
 if(!d3d)return 0;
 void* original=(void*)GetProcAddress(d3d,"Direct3DCreate9");
 void** slot=(void**)(exe+0x003cc310u);
 if(!original||*slot!=original){
  OutputDebugStringA("[HulkShader] D3D9 entry was modified; no hook to avoid conflicting ASIs\n");
  return 0;
 }
 DWORD access=0;
 if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&access))return 0;
 real_d3d9=original;
 HulkSwap((LPVOID volatile*)slot,(LPVOID)ShaderD3D9);
 {DWORD ignored=0;VirtualProtect(slot,4,access,&ignored);}
 OutputDebugStringA("[HulkShader] source-verified original PC shader replacement hook installed\n");
 return 0;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID ignored){
 (void)ignored;
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(module);
  HANDLE thread=CreateThread(0,0,Worker,0,0,0);
  if(thread)CloseHandle(thread);
 }
 return TRUE;
}