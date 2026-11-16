#ifndef HULK_WIN32_MIN_H
#define HULK_WIN32_MIN_H
// Tiny standalone Win32 declarations: only for building the independent x86
// smoke proxy with clang-cl on build hosts without Windows SDK headers.
// The full HulkPC.asi enhancement uses Microsoft's real Windows/D3D9 SDK.
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* FARPROC;
typedef void* LPVOID;
typedef unsigned long DWORD;
typedef long HRESULT;
typedef int BOOL;
typedef const char* LPCSTR;
typedef const unsigned short* LPCWSTR;
#define WINAPI __stdcall
#define DLLIMPORT __declspec(dllimport)
#define MAX_PATH 260
extern DLLIMPORT HMODULE WINAPI GetModuleHandleW(LPCWSTR);
extern DLLIMPORT HMODULE WINAPI GetModuleHandleA(LPCSTR);
extern DLLIMPORT DWORD WINAPI GetModuleFileNameA(HMODULE,char*,DWORD);
extern DLLIMPORT DWORD WINAPI GetSystemDirectoryA(char*,DWORD);
extern DLLIMPORT HMODULE WINAPI LoadLibraryA(LPCSTR);
extern DLLIMPORT FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR);
extern DLLIMPORT HANDLE WINAPI CreateThread(LPVOID,DWORD,DWORD (WINAPI*)(LPVOID),LPVOID,DWORD,DWORD*);
extern DLLIMPORT BOOL WINAPI CloseHandle(HANDLE);
extern DLLIMPORT BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
extern DLLIMPORT DWORD WINAPI Sleep(DWORD);
extern DLLIMPORT void WINAPI OutputDebugStringA(LPCSTR);
extern DLLIMPORT HANDLE WINAPI CreateFileA(LPCSTR,DWORD,DWORD,LPVOID,DWORD,DWORD,HANDLE);
extern DLLIMPORT BOOL WINAPI WriteFile(HANDLE,const void*,DWORD,DWORD*,LPVOID);
extern DLLIMPORT BOOL WINAPI VirtualProtect(LPVOID,DWORD,DWORD,DWORD*);
extern DLLIMPORT long WINAPI InterlockedExchange(volatile long*,long);
static LPVOID HulkSwap(LPVOID volatile* slot,LPVOID value) {
 return (LPVOID)InterlockedExchange((volatile long*)slot,(long)value);
}
extern DLLIMPORT DWORD WINAPI GetTickCount(void);
extern DLLIMPORT HANDLE WINAPI GetProcessHeap(void);
extern DLLIMPORT LPVOID WINAPI HeapAlloc(HANDLE,DWORD,DWORD);
extern DLLIMPORT BOOL WINAPI HeapFree(HANDLE,DWORD,LPVOID);
extern DLLIMPORT unsigned WINAPI GetPrivateProfileIntA(LPCSTR,LPCSTR,int,LPCSTR);
#define FILE_APPEND_DATA (4u)
#define FILE_SHARE_READ (1u)
#define OPEN_ALWAYS (4u)
#define FILE_ATTRIBUTE_NORMAL (0x80u)
#define PAGE_READWRITE (4u)
#define PAGE_EXECUTE_READWRITE (0x40u)
#define DLL_PROCESS_ATTACH (1u)
#define TRUE (1)
static unsigned HulkLen(const char* s) {unsigned n=0;while(s && s[n] && n<2048u)++n;return n;}
static int HulkEndsAt(char* p,unsigned cap,unsigned* pos) {
 DWORD n=GetModuleFileNameA((HMODULE)0,p,cap);
 if(!n || n>=cap)return 0;
 while(n && p[n-1]!='\\' && p[n-1]!='/')--n;
 if(!n)return 0;*pos=(unsigned)n;return 1;
}
static int HulkAppend(char* p,unsigned cap,unsigned pos,const char* suf) {
 unsigned i=0;while(suf[i] && pos+i+1<cap){p[pos+i]=suf[i];++i;}
 if(suf[i])return 0;p[pos+i]=0;return 1;
}
extern DLLIMPORT DWORD WINAPI GetFileAttributesA(LPCSTR);
extern DLLIMPORT BOOL WINAPI ReadFile(HANDLE,LPVOID,DWORD,DWORD*,LPVOID);
extern DLLIMPORT DWORD WINAPI GetFileSize(HANDLE,DWORD*);
#endif
