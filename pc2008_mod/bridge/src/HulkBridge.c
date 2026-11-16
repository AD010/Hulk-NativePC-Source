#include "win32_min.h"
// Original 2008 PC HULK.EXE, verified PE32 x86 only. Read-only resource
// redirection: original files, Direct3D9 (including DXVK), saves untouched.
// HulkBridge is opt-in through HulkBridge.ini and is never an Xbox ARC loader.
#define HB_OPEN_EXISTING 3u
#define HB_FILE_ATTRIBUTE_INVALID 0xffffffffu
#define HB_GENERIC_WRITE 0x40000000u
#define HB_FILE_WRITE_DATA 0x00000002u
#define HB_FILE_APPEND_DATA 0x00000004u
#define HB_MAX_LOG 40u

typedef HANDLE (WINAPI *OpenFn)(LPCSTR,DWORD,DWORD,LPVOID,DWORD,DWORD,HANDLE);
static OpenFn originalCreateFile;
static char game_folder[MAX_PATH];
static char virtual_index[MAX_PATH];
static char virtual_arc[MAX_PATH];
static char virtual_map[MAX_PATH];
static char selected_arc[40];
static volatile long intercepted_count;

static char Lower(char c){ return c>='A'&&c<='Z' ? (char)(c+32) : c; }
static int EqualInsensitive(const char* a,const char* b){
 unsigned i=0;
 while(a[i]&&b[i]&&i<MAX_PATH){if(Lower(a[i])!=Lower(b[i]))return 0;++i;}
 return a[i]==0 && b[i]==0;
}
static int Join(char* out,unsigned cap,const char* root,const char* tail){
 unsigned i=0,j=0;
 while(root[i] && i+1<cap){out[i]=root[i];++i;}
 if(root[i])return 0;
 while(tail[j] && i+1<cap){out[i++]=tail[j++];}
 if(tail[j])return 0;
 out[i]=0;return 1;
}
static const char* Base(const char* p){
 const char* result=p;
 if(!p)return 0;
 for(;*p;++p)if(*p=='/'||*p=='\\')result=p+1;
 return result;
}
static int IsGameResourcePath(const char* path,const char* expected){
 const char* base=Base(path);
 const char* scan=path;
 if(!base||!EqualInsensitive(base,expected))return 0;
 // A game may request data\\MODELS.ARC or ..\\Data\\MODELS.ARC.
 // Refuse a drive path to another game's matching filename.
 for(;*scan;++scan){
  if(*scan==':') {
   // Only the original game's file path is allowed. Caller can provide
   // relative paths; absolute-path guarding is provided by the game root.
   unsigned n=0,i=0;
   if(!game_folder[0])return 0;
   for(;game_folder[n] && path[n] && n<MAX_PATH;++n){
    if(Lower(path[n])!=Lower(game_folder[n]))break;
   }
   if(game_folder[n]!=0)return 0;
   i=n; (void)i;
   return 1;
  }
 }
 return 1;
}
static HANDLE WINAPI HookCreateFileA(LPCSTR path,DWORD access,DWORD sharing,
 LPVOID attrs,DWORD creation,DWORD flags,HANDLE template_file){
 if(!originalCreateFile)return (HANDLE)(-1);
 if(path && creation==HB_OPEN_EXISTING &&
    !(access & (HB_GENERIC_WRITE|HB_FILE_WRITE_DATA|HB_FILE_APPEND_DATA))) {
  const char* replacement=0;
  if(IsGameResourcePath(path,"index.ind"))replacement=virtual_index;
  else if(IsGameResourcePath(path,selected_arc))replacement=virtual_arc;
  if(replacement && replacement[0]) {
   HANDLE h=originalCreateFile(replacement,access,sharing,attrs,creation,flags,template_file);
   if(h!=(HANDLE)(-1)){
    if(InterlockedExchange(&intercepted_count,1)==0)
     OutputDebugStringA("[HulkBridge] original PC resource redirected to verified sidecar\n");
    return h;
   }
  }
 }
 return originalCreateFile(path,access,sharing,attrs,creation,flags,template_file);
}
static int PackageReady(void){
 // Check the sidecar layout before modifying a single guest IAT pointer.
 if(GetFileAttributesA(virtual_index)==HB_FILE_ATTRIBUTE_INVALID ||
    GetFileAttributesA(virtual_arc)==HB_FILE_ATTRIBUTE_INVALID ||
    GetFileAttributesA(virtual_map)==HB_FILE_ATTRIBUTE_INVALID)return 0;
 HANDLE f=CreateFileA(virtual_map,0x80000000u,1u,0,HB_OPEN_EXISTING,0,0);
 if(f==(HANDLE)(-1))return 0;
 char buf[128];DWORD got=0;
 BOOL ok=ReadFile(f,buf,sizeof(buf)-1,&got,0);CloseHandle(f);
 if(!ok || got<32)return 0;
 buf[got]=0;
 // Explicit supported-format signature; non-PC Xbox index has no such map.
 const char* mark="HULKBRIDGE-PC-v1\nindex.ind\n";
 unsigned i=0;
 for(;mark[i];++i)if(buf[i]!=mark[i])return 0;
 for(unsigned j=0;selected_arc[j];++j){
  if(i+j>=got || Lower(buf[i+j])!=Lower(selected_arc[j]))return 0;
 }
 return buf[i+HulkLen(selected_arc)]=='\n';
}
static int OriginalHulk(void){
 const unsigned char* exe=(const unsigned char*)GetModuleHandleW((const unsigned short*)0);
 if(!exe||exe[0]!='M'||exe[1]!='Z')return 0;
 unsigned pe=*(const unsigned*)(exe+0x3c);
 if(pe!=0x40||exe[pe]!='P'||exe[pe+1]!='E')return 0;
 return *(const unsigned short*)(exe+pe+4)==0x14c &&
        *(const unsigned*)(exe+pe+8)==0x48503ec6u &&
        *(const unsigned*)(exe+pe+24+16)==0x24e129u &&
        *(const unsigned*)(exe+pe+24+56)==0x81b000u;
}
static int InitPaths(void){
 char path[MAX_PATH];unsigned pos=0;
 if(!HulkEndsAt(path,MAX_PATH,&pos))return 0;
 path[pos]=0;
 if(!Join(game_folder,MAX_PATH,path,""))return 0;
 if(!Join(virtual_index,MAX_PATH,path,"HulkBridge\\index.ind"))return 0;
 if(!Join(virtual_map,MAX_PATH,path,"HulkBridge\\HulkBridge.map"))return 0;
 char ini[MAX_PATH];
 if(!Join(ini,MAX_PATH,path,"HulkBridge.ini"))return 0;
 if(GetPrivateProfileIntA("ResourceBridge","EnableResourceBridge",0,ini)!=1)return 0;
 const int category=GetPrivateProfileIntA("ResourceBridge","Category",0,ini);
 const char* filename=0;
 if(category==1)filename="SHADEREF.ARC";
 else if(category==2)filename="MODELS.ARC";
 else return 0;
 unsigned j=0;while(filename[j] && j<sizeof(selected_arc)-1){selected_arc[j]=filename[j];++j;}
 selected_arc[j]=0;
 char relative[80]="HulkBridge\\data\\";
 if(!HulkAppend(relative,sizeof(relative),HulkLen(relative),selected_arc))return 0;
 if(!Join(virtual_arc,MAX_PATH,path,relative))return 0;
 return PackageReady();
}
static DWORD WINAPI Worker(LPVOID unused){
 (void)unused;
 if(!OriginalHulk())return 0;
 if(!InitPaths()){
  OutputDebugStringA("[HulkBridge] disabled: no PC-compatible opt-in package\n");
  return 0;
 }
 unsigned char* exe=(unsigned char*)GetModuleHandleW((const unsigned short*)0);
 HMODULE kernel=GetModuleHandleA("kernel32.dll");
 if(!exe || !kernel)return 0;
 OpenFn genuine=(OpenFn)GetProcAddress(kernel,"CreateFileA");
 void** slot=(void**)(exe+0x003cc118u);
 // Hook ONLY when the original verified import thunk points at CreateFileA.
 if(!genuine || *slot!=(void*)genuine){
  OutputDebugStringA("[HulkBridge] CreateFileA import ABI drift; refusing hook\n");
  return 0;
 }
 DWORD old=0;
 if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&old))return 0;
 originalCreateFile=genuine;
 HulkSwap((LPVOID volatile*)slot,(LPVOID)HookCreateFileA);
 {DWORD ignored=0;VirtualProtect(slot,4,old,&ignored);}
 OutputDebugStringA("[HulkBridge] original 2008 PC CreateFileA virtual ARC/index bridge ACTIVE\n");
 return 0;
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID unused){
 (void)unused;
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(module);
  HANDLE thread=CreateThread(0,0,Worker,0,0,0);
  if(thread)CloseHandle(thread);
 }
 return TRUE;
}