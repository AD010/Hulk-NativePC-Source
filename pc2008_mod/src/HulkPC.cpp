// Optional official 2008 PC host enhancement, independent of Xenos/ReXGlue.
// This library is NOT a finished Xbox graphics port. All game-data-changing
// paths are explicitly opt-in. Requires the exact original HULK.EXE x86 build.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>

static_assert(sizeof(void*) == 4, "The Incredible Hulk PC 2008 is PE32/x86");
namespace {
using Create9 = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using CreateVSFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**);
using CreatePSFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
using SetVSFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DVertexShader9*);
using SetVSConstFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,const float*,UINT);
using SamplerFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
using SetTextureFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
Create9 realCreate9{};
CreateDeviceFn realCreateDevice{};
PresentFn realPresent{};
ResetFn realReset{};
CreateVSFn realCreateVS{};
CreatePSFn realCreatePS{};
SetVSFn realSetVS{};
SetVSConstFn realVSConst{};
SamplerFn realSampler{};
SetTextureFn realSetTexture{};
// Sampler requests must be reapplied when a texture of another type is bound.
std::mutex mipMutex;
bool mipPointRequested[16]{};
bool minLinearRequested[16]{};
// Restore the game's sampler maximum when an AF override leaves its texture.
bool afMaximumOverridden[16]{};
DWORD afOriginalMaximum[16]{};
struct ShaderInfo {
  UINT view = 0, world = 0;
  WORD matrixClass = 0;
  bool hasView = false, hasWorld = false;
};
std::mutex shaderMutex, logMutex;
std::unordered_map<void*,ShaderInfo> shaderMap;
IDirect3DVertexShader9* activeVS{};
std::atomic<uint32_t> vsCount{0}, psCount{0}, viewProjCount{0};
std::once_flag configOnce;
wchar_t ini[MAX_PATH]{}, logFile[MAX_PATH]{};
bool fixAspect = false;
// Optional D3D9 presentation interval override; independent of HUD geometry.
bool forceImmediatePresent = false;
// Optional experimental support for full-size offscreen 3D scene passes.
bool allowOffscreenHorPlus = false;
// Optional texture-quality enhancement; disabled unless explicitly configured.
bool forceTrilinearMip = false;
DWORD mipLinearCaps2D = 0, mipLinearCapsCube = 0, mipLinearCapsVolume = 0;
DWORD anisotropicCaps2D = 0, anisotropicCapsCube = 0, anisotropicCapsVolume = 0;
uint32_t width = 0, height = 0, anisotropy = 0, maxAnisotropy = 1;
double sourceAspect = 4.0 / 3.0;
// D3D9's hFocusWindow can differ from the actual device presentation window.
// Track the effective hDeviceWindow for accurate client/backbuffer diagnostics.
std::atomic<HWND> presentationWindow{nullptr};
// Optional native-PC engine camera far-clip experiment. The game's separate
// LOD distances and shadow cascades are NOT changed by this setting.
volatile float cameraFarPlaneScale = 1.0f;
std::mutex cameraFarPlaneHookMutex;
bool cameraFarPlaneHookInstalled = false;

bool OriginalHulk() noexcept {
  auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
  if(!dos || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew!=0x40) return false;
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const BYTE*>(dos)+dos->e_lfanew);
  return nt->Signature==IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 &&
         nt->FileHeader.TimeDateStamp==0x48503ec6u &&
         nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
         nt->OptionalHeader.AddressOfEntryPoint==0x24e129u &&
         nt->OptionalHeader.SizeOfImage==0x81b000u;
}
void LoadConfig() {
  std::call_once(configOnce, [] {
    wchar_t path[MAX_PATH]{};
    const DWORD pathLength=GetModuleFileNameW(nullptr,path,MAX_PATH);
    // A truncated executable path must never be treated as an install directory.
    // GetModuleFileNameW reports nSize on truncation; a suffix could otherwise
    // resolve to an unrelated HulkPC.ini or log path.
    if(!pathLength || pathLength>=MAX_PATH)return;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash)return;
    *(slash+1)=0;
    // GetModuleFileNameW can return a path almost MAX_PATH characters long.
    // Both destination buffers must fit the longer log filename; otherwise
    // wcscat_s invokes the MSVC invalid-parameter handler inside the game.
    constexpr size_t kLogNameChars = sizeof(L"HulkPC_ASI.log") / sizeof(wchar_t);
    if(wcslen(path) > MAX_PATH - kLogNameChars) return;
    wcscpy_s(ini,path);wcscat_s(ini,L"HulkPC.ini");
    wcscpy_s(logFile,path);wcscat_s(logFile,L"HulkPC_ASI.log");
    fixAspect=GetPrivateProfileIntW(L"Graphics",L"EnableExperimentalViewProj16x10",0,ini)==1;
    forceImmediatePresent=GetPrivateProfileIntW(
        L"Graphics",L"ForceVSyncOff",0,ini)==1;
    allowOffscreenHorPlus=GetPrivateProfileIntW(
        L"Graphics",L"ExperimentalHorPlusSceneTargets",0,ini)==1;
    anisotropy=GetPrivateProfileIntW(L"Graphics",L"ForceAnisotropy",0,ini);
    forceTrilinearMip=GetPrivateProfileIntW(L"Graphics",L"ForceTrilinearMipFilter",0,ini)==1;
    if(anisotropy!=2&&anisotropy!=4&&anisotropy!=8&&anisotropy!=16)anisotropy=0;
    width=GetPrivateProfileIntW(L"Graphics",L"ForceFullscreenWidth",0,ini);
    height=GetPrivateProfileIntW(L"Graphics",L"ForceFullscreenHeight",0,ini);
    if(!width||!height||width>16384||height>16384)width=height=0;
    // DXVK HUD-SAFE: original 2008 PC HUD uses a 4:3 layout even when
    // forced backbuffer/projection display a 16:10 3D scene. Never change
    // the engine's UI by accident, including when an old INI is present.
    // Opt-in ProtectOriginalHUD=0 only AFTER a real native UI mapper exists.
    if(GetPrivateProfileIntW(L"Graphics",L"ProtectOriginalHUD",1,ini)!=0) {
      fixAspect=false;
      allowOffscreenHorPlus=false;
      width=height=0;
    }
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Graphics",L"SourceAspect",
                             L"1.333333333333333",value,64,ini);
    wchar_t* end=nullptr;
    const double aspect=wcstod(value,&end);
    if(end!=value&&std::isfinite(aspect)&&aspect>=1.0&&aspect<=3.0)
      sourceAspect=aspect;
    // Keep Visual Studio Win32 ASI consistent with the freestanding x86
    // HulkPC.asi distributed in the DXVK 16:10 source package.
    // The legacy integer aspect pair is optional. Do not silently override
    // a user-supplied SourceAspect with implicit default 4:3 values.
    wchar_t numeratorText[32]{}, denominatorText[32]{};
    const DWORD numeratorLength=GetPrivateProfileStringW(
        L"Graphics",L"BaseAspectNumerator",L"",numeratorText,32,ini);
    const DWORD denominatorLength=GetPrivateProfileStringW(
        L"Graphics",L"BaseAspectDenominator",L"",denominatorText,32,ini);
    if(numeratorLength && denominatorLength) {
      wchar_t* numeratorEnd=nullptr;
      wchar_t* denominatorEnd=nullptr;
      const unsigned long numerator=wcstoul(numeratorText,&numeratorEnd,10);
      const unsigned long denominator=wcstoul(denominatorText,&denominatorEnd,10);
      if(numeratorEnd!=numeratorText && *numeratorEnd==L'\0' &&
         denominatorEnd!=denominatorText && *denominatorEnd==L'\0' &&
         numerator>=3 && numerator<=64 &&
         denominator>=3 && denominator<=64)
        sourceAspect=double(numerator)/double(denominator);
    }
    wchar_t farScaleText[48]{};
    GetPrivateProfileStringW(L"Graphics",L"ExperimentalCameraFarPlaneScale",
                             L"1.0",farScaleText,48,ini);
    wchar_t* farScaleEnd=nullptr;
    const double farScale=wcstod(farScaleText,&farScaleEnd);
    // Fail closed on malformed text, infinity, or extreme depth ranges.
    if(farScaleEnd!=farScaleText && *farScaleEnd==L'\0' &&
       std::isfinite(farScale) && farScale>=1.0 && farScale<=2.0)
      cameraFarPlaneScale=static_cast<float>(farScale);
  });
}
void Log(const char* fmt,...) {
  LoadConfig();
  if(!logFile[0])return;
  char buffer[1024]{};
  va_list args;va_start(args,fmt);
  vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,fmt,args);va_end(args);
  std::lock_guard lock(logMutex);
  HANDLE file=CreateFileW(logFile,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,
                          OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return;
  DWORD written=0;
  WriteFile(file,buffer,(DWORD)strlen(buffer),&written,nullptr);
  CloseHandle(file);
}

#if defined(_MSC_VER) && defined(_M_IX86)
// Exact HULK.EXE SHA-256 verified off-line:
// 6d71e479de73a5e3ee5f3a924ed2dc6f79aea9f0aac43b5984b1f77193d3b233
// 004AC728: movss dword ptr [esi+40h], xmm0  (CameraFarPlane parser)
// Replace only this 5-byte store with CALL. RET continues at 004AC72D.
// The original XMM0, all GPRs, EFLAGS and the stack are preserved.
__declspec(naked) void CameraFarPlaneStoreHook() {
  __asm {
    pushfd
    sub esp, 4
    movss dword ptr [esp], xmm0
    mulss xmm0, dword ptr [cameraFarPlaneScale]
    movss dword ptr [esi+0x40], xmm0
    movss xmm0, dword ptr [esp]
    add esp, 4
    popfd
    ret
  }
}
bool InstallCameraFarPlaneHook() {
  if(!OriginalHulk() || cameraFarPlaneScale<=1.0001f)return false;
  auto* host=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  if(!host)return false;
  auto* site=host+0x000AC728u;
  // Verify both the store AND the following call to the resource parser.
  // Do not overwrite an altered EXE, a conflicting plugin, or an unknown TU.
  static constexpr BYTE expected[10] = {
    0xF3,0x0F,0x11,0x46,0x40,0xE8,0x1C,0x53,0x0D,0x00
  };
  if(memcmp(site,expected,sizeof(expected))!=0) {
    Log("[Camera] far-plane hook refused: unexpected original instruction bytes\r\n");
    return false;
  }
  // This hook runs later from executable code. Pin the ASI to prevent a
  // dinput8 loader unloading the destination while the game is running.
  HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_PIN,
      reinterpret_cast<LPCWSTR>(&CameraFarPlaneStoreHook),&pinned))
    return false;
  DWORD previousProtect=0;
  if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previousProtect))
    return false;
  BYTE patch[5]{0xE8,0,0,0,0};
  const DWORD target=static_cast<DWORD>(
      reinterpret_cast<uintptr_t>(&CameraFarPlaneStoreHook));
  const DWORD next=static_cast<DWORD>(reinterpret_cast<uintptr_t>(site+5));
  const DWORD displacement=target-next; // E8 relative32, modulo 2^32 on x86.
  memcpy(patch+1,&displacement,sizeof(displacement));
  memcpy(site,patch,sizeof(patch));
  FlushInstructionCache(GetCurrentProcess(),site,sizeof(patch));
  DWORD unused=0;
  const BOOL restored=VirtualProtect(site,5,previousProtect,&unused);
  Log("[Camera] native PC CameraFarPlane store hooked at 004AC728, scale=%.3f protection_restored=%u\r\n",
      static_cast<double>(cameraFarPlaneScale),static_cast<unsigned>(restored!=0));
  return true;
}
#else
bool InstallCameraFarPlaneHook() { return false; }
#endif

template<typename Fn> bool ReplaceVtable(void* object,size_t index,Fn hook,Fn& original) {
  if(!object)return false;
  auto** table=*reinterpret_cast<void***>(object);
  if(!table)return false;
  auto* slot=&table[index];
  DWORD protect=0;
  if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&protect))return false;
  const auto current=*slot;
  bool success=true;
  if(current!=reinterpret_cast<void*>(hook)) {
    if(!original) original=reinterpret_cast<Fn>(current);
    if(current!=reinterpret_cast<void*>(original))success=false;
    else InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot),
                                    reinterpret_cast<PVOID>(hook));
  }
  DWORD unused=0;VirtualProtect(slot,sizeof(void*),protect,&unused);
  return success;
}
uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
uint16_t U16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1]<<8);}
ShaderInfo ReadCtab(IDirect3DVertexShader9* shader) {
  ShaderInfo info{};
  UINT bytes=0;
  if(!shader || FAILED(shader->GetFunction(nullptr,&bytes)) ||
     bytes<44||bytes>(1u<<20))return info;
  std::vector<uint8_t> code(bytes);
  const UINT allocated=bytes;
  if(FAILED(shader->GetFunction(code.data(),&bytes)) ||
     bytes>allocated || bytes<44 || (bytes&3u))return info;
  for(size_t i=0;i+36<bytes;i+=4) {
    if(memcmp(code.data()+i,"CTAB",4))continue;
    const size_t base=i+4;
    if(U32(code.data()+base)!=28)continue;
    const size_t count=U32(code.data()+base+12);
    const size_t offset=U32(code.data()+base+16);
    if(count>128 || offset>bytes-base || 20*count>bytes-base-offset)continue;
    for(size_t j=0;j<count;++j) {
      const size_t at=base+offset+j*20;
      const auto nameOff=U32(code.data()+at);
      const auto set=U16(code.data()+at+4);
      const auto reg=U16(code.data()+at+6);
      const auto size=U16(code.data()+at+8);
      const auto type=U32(code.data()+at+12);
      if(nameOff>=bytes-base || type>bytes-base-16 || set!=2 ||
         size!=4 || reg>252)continue;
      const char* name=reinterpret_cast<const char*>(code.data()+base+nameOff);
      if(!memchr(name,'\0',bytes-base-nameOff))continue;
      if(!strcmp(name,"ViewProj")) {
        info.view=reg;info.matrixClass=U16(code.data()+base+type);
        info.hasView=true;
      }
      if(!strcmp(name,"World")) {info.world=reg;info.hasWorld=true;}
    }
    break;
  }
  return info;
}
HRESULT STDMETHODCALLTYPE OnCreateDevice(IDirect3D9*,UINT,D3DDEVTYPE,HWND,
                                         DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE OnCreateVS(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**);
HRESULT STDMETHODCALLTYPE OnCreatePS(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
HRESULT STDMETHODCALLTYPE OnSetVS(IDirect3DDevice9*,IDirect3DVertexShader9*);
HRESULT STDMETHODCALLTYPE OnVSConstants(IDirect3DDevice9*,UINT,const float*,UINT);
HRESULT STDMETHODCALLTYPE OnSampler(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
HRESULT STDMETHODCALLTYPE OnSetTexture(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);

// A failed early attempt must not permanently consume the only install chance:
// the loader may not yet have finished preparing the original camera parser.
// Serialize attempts from the loader worker and the first D3D9 initialization.
void TryInstallCameraFarPlaneHook() {
  std::lock_guard<std::mutex> lock(cameraFarPlaneHookMutex);
  if(cameraFarPlaneHookInstalled || cameraFarPlaneScale<=1.0001f)return;
  cameraFarPlaneHookInstalled=InstallCameraFarPlaneHook();
  if(!cameraFarPlaneHookInstalled)
    Log("[Camera] far-plane hook not installed; a later D3D9 attempt may retry\r\n");
}
IDirect3D9* WINAPI OnCreate9(UINT sdk) {
  if(!realCreate9)return nullptr;
  LoadConfig();
  if(cameraFarPlaneScale>1.0001f) TryInstallCameraFarPlaneHook();
  auto* api=realCreate9(sdk);
  if(api && ReplaceVtable(api,16,&OnCreateDevice,realCreateDevice))
    Log("[ASI] HULK.EXE x86; Direct3DCreate9 / CreateDevice hook installed\r\n");
  return api;
}
bool HookIat() {
  if(!OriginalHulk())return false;
  auto* exe=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  HMODULE dll=GetModuleHandleW(L"d3d9.dll");
  if(!dll)return false;
  void* target=reinterpret_cast<void*>(GetProcAddress(dll,"Direct3DCreate9"));
  auto** thunk=reinterpret_cast<void**>(exe+0x003cc310u);
  if(!target || *thunk!=target)return false;
  DWORD old=0;
  if(!VirtualProtect(thunk,4,PAGE_READWRITE,&old))return false;
  realCreate9=reinterpret_cast<Create9>(*thunk);
  InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(thunk),
                             reinterpret_cast<PVOID>(&OnCreate9));
  DWORD unused=0;VirtualProtect(thunk,4,old,&unused);
  return true;
}
DWORD WINAPI TryLateHook(void* context) {
  // The game can parse its camera before its first Direct3DCreate9 call.
  // Install the independent camera hook as soon as the loader lock is gone,
  // rather than waiting for graphics-device initialization.
  LoadConfig();
  if(cameraFarPlaneScale>1.0001f) TryInstallCameraFarPlaneHook();
  // Only retry the D3D9 IAT when the immediate attach-time attempt failed.
  // Never rewrite an import slot already owned by this ASI.
  if(context) {
    for(unsigned n=0;n<200;++n) {
      if(HookIat())return 0;
      Sleep(10);
    }
    OutputDebugStringA("HulkPC: original D3D9 import not found, leaving game untouched\n");
  }
  return 0;
}
HRESULT STDMETHODCALLTYPE OnCreateDevice(IDirect3D9* api,UINT adapter,
  D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* pp,
  IDirect3DDevice9** result) {
  if(!realCreateDevice)return D3DERR_INVALIDCALL;
  if(!pp||!result)return realCreateDevice(api,adapter,type,window,flags,pp,result);
  const D3DPRESENT_PARAMETERS original=*pp;
  // Capture our own change before calling the driver: a failed CreateDevice
  // is allowed to modify the caller's presentation parameters.
  const bool sizeChanged=width && height && !pp->Windowed &&
    (pp->BackBufferWidth!=width || pp->BackBufferHeight!=height);
  const bool intervalChanged=forceImmediatePresent &&
    pp->PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE;
  const bool changed=sizeChanged || intervalChanged;
  if(sizeChanged) {
    pp->BackBufferWidth=width;
    pp->BackBufferHeight=height;
  }
  if(intervalChanged)
    pp->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  HRESULT hr=realCreateDevice(api,adapter,type,window,flags,pp,result);
  if(FAILED(hr)) {
    // A failed driver call may mutate presentation parameters even when no
    // override was requested. Never return driver-written garbage to the game.
    *pp=original;
    *result=nullptr;
    if(changed) {
      hr=realCreateDevice(api,adapter,type,window,flags,pp,result);
      if(FAILED(hr)) {
        *pp=original;
        *result=nullptr;
      }
    }
  }
  if(SUCCEEDED(hr) && *result) {
    // A successful fallback may have changed hDeviceWindow. Record the
    // effective presentation HWND, not the application's focus HWND.
    presentationWindow.store(pp->hDeviceWindow ? pp->hDeviceWindow : window,
                             std::memory_order_release);
    // A new device starts with fresh sampler state. Never replay requests
    // recorded for a previous device after a CreateDevice/recreate cycle.
    {std::lock_guard lock(mipMutex);
     std::fill_n(mipPointRequested,16,false);
     std::fill_n(minLinearRequested,16,false);
     std::fill_n(afMaximumOverridden,16,false);}
    // Do not erase shader reflections belonging to other live D3D9 devices.
    // CreateVertexShader replaces a reflection entry when an address is reused,
    // and Reset does not destroy managed shader objects. Clearing the global
    // map here could silently disable Hor+ on an unrelated live device.
    // Keep existing entries until shader/device lifetime tracking is added.
    {std::lock_guard lock(shaderMutex);
     activeVS=nullptr;}
    D3DCAPS9 caps{};
    // D3D9 has separate mip-filter capabilities for 2D/cube/volume textures.
    // Keep the exact device caps: DXVK or older native drivers may differ.
    mipLinearCaps2D=mipLinearCapsCube=mipLinearCapsVolume=0;
    anisotropicCaps2D=anisotropicCapsCube=anisotropicCapsVolume=0;
    maxAnisotropy=1;
    if(SUCCEEDED((*result)->GetDeviceCaps(&caps))) {
      maxAnisotropy=std::max(1u,caps.MaxAnisotropy);
      anisotropicCaps2D=caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC;
      anisotropicCapsCube=caps.CubeTextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC;
      anisotropicCapsVolume=caps.VolumeTextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC;
      mipLinearCaps2D=caps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR;
      mipLinearCapsCube=caps.CubeTextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR;
      mipLinearCapsVolume=caps.VolumeTextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR;
    }
    ReplaceVtable(*result,16,&OnReset,realReset);
    ReplaceVtable(*result,17,&OnPresent,realPresent);
    ReplaceVtable(*result,91,&OnCreateVS,realCreateVS);
    ReplaceVtable(*result,92,&OnSetVS,realSetVS);
    ReplaceVtable(*result,94,&OnVSConstants,realVSConst);
    ReplaceVtable(*result,106,&OnCreatePS,realCreatePS);
    ReplaceVtable(*result,69,&OnSampler,realSampler);
    ReplaceVtable(*result,65,&OnSetTexture,realSetTexture);
    Log("[D3D9] trilinear mip: requested=%u caps 2D=%u cube=%u volume=%u\r\n",
        (unsigned)forceTrilinearMip,(unsigned)!!mipLinearCaps2D,
        (unsigned)!!mipLinearCapsCube,(unsigned)!!mipLinearCapsVolume);
    Log("[HulkPC] DXVK-compatible D3D9 x86 Hor+ experimental; only ViewProj/World VS constants\r\n");
    Log("[D3D9] Device %ux%u fullscreen=%u, FOV experimental=%u, AF=%u\r\n",
        pp->BackBufferWidth,pp->BackBufferHeight,(unsigned)!pp->Windowed,
        (unsigned)fixAspect,anisotropy);
  }
  return hr;
}
HRESULT STDMETHODCALLTYPE OnReset(IDirect3DDevice9* dev,D3DPRESENT_PARAMETERS* pp) {
  if(!realReset)return D3DERR_INVALIDCALL;
  // Apply the same opt-in fullscreen dimensions on Reset as CreateDevice.
  // A lost device may be reset repeatedly (including when DXVK is in use).
  // If the requested mode fails, retry the game's untouched parameters.
  D3DPRESENT_PARAMETERS original{};
  bool changed=false;
  if(pp) {
    original=*pp;
    const bool sizeChanged=width && height && !pp->Windowed &&
      (pp->BackBufferWidth!=width || pp->BackBufferHeight!=height);
    const bool intervalChanged=forceImmediatePresent &&
      pp->PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE;
    changed=sizeChanged || intervalChanged;
    if(sizeChanged) {
      pp->BackBufferWidth=width;
      pp->BackBufferHeight=height;
    }
    if(intervalChanged)
      pp->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  }
  // A failed Reset leaves the old device state in place. Invalidate the
  // tracked shader only after a successful reset, not before the call.
  const HRESULT firstHr=realReset(dev,pp);
  HRESULT hr=firstHr;
  if(FAILED(hr) && pp) {
    // D3D9 is permitted to rewrite the caller's parameters on failure.
    // Restore the host request even without an active override; otherwise a
    // failed ordinary Reset can poison the game's next lost-device retry.
    *pp=original;
    if(changed) {
      hr=realReset(dev,pp);
      if(FAILED(hr)) *pp=original;
      Log("[D3D9] Reset forced mode failed hr=0x%08lX; original retry hr=0x%08lX\r\n",
          (unsigned long)(unsigned int)firstHr,(unsigned long)(unsigned int)hr);
    }
  }
  if(SUCCEEDED(hr)) {
    // Reset can change the presentation window, or clear hDeviceWindow to
    // request the device's focus window. Never retain a stale HWND from a
    // previous fullscreen/windowed mode after a successful Reset.
    HWND effectiveWindow=pp ? pp->hDeviceWindow : nullptr;
    if(!effectiveWindow) {
      D3DDEVICE_CREATION_PARAMETERS creation{};
      if(SUCCEEDED(dev->GetCreationParameters(&creation)))
        effectiveWindow=creation.hFocusWindow;
    }
    presentationWindow.store(effectiveWindow,std::memory_order_release);
    {std::lock_guard lock(shaderMutex);activeVS=nullptr;}
    {std::lock_guard lock(mipMutex);
     std::fill_n(mipPointRequested,16,false);
     std::fill_n(minLinearRequested,16,false);
     std::fill_n(afMaximumOverridden,16,false);}
  }
  if(pp)Log("[D3D9] Reset %ux%u windowed=%u hr=0x%08lX\r\n",
            pp->BackBufferWidth,pp->BackBufferHeight,
            (unsigned)pp->Windowed,(unsigned long)(unsigned int)hr);
  return hr;
}
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9* dev,const RECT* a,
                                    const RECT* b,HWND c,const RGNDATA* d) {
  // Present may be called from more than one thread. Keep the diagnostic
  // frame clock local to the calling thread, and restart it on device changes.
  // This avoids races in the old shared counters without changing game timing.
  thread_local LARGE_INTEGER frequency{},then{};
  thread_local unsigned frames=0;
  thread_local IDirect3DDevice9* countedDevice=nullptr;
  LARGE_INTEGER now{};
  if(!QueryPerformanceCounter(&now))return realPresent(dev,a,b,c,d);
  if(countedDevice!=dev || !frequency.QuadPart) {
    countedDevice=dev;
    frames=0;
    then=now;
    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)
      return realPresent(dev,a,b,c,d);
  }
  if(++frames>=300) {
    const double seconds=double(now.QuadPart-then.QuadPart)/frequency.QuadPart;
    D3DVIEWPORT9 vp{};
    if(seconds>0.01 && SUCCEEDED(dev->GetViewport(&vp))) {
      // Report actual dimensions rather than assuming the requested fullscreen
      // mode was accepted. Read-only D3D9 queries also work through DXVK.
      UINT backWidth=0,backHeight=0,clientWidth=0,clientHeight=0;
      IDirect3DSurface9* back=nullptr;
      if(SUCCEEDED(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)) && back) {
        D3DSURFACE_DESC desc{};
        if(SUCCEEDED(back->GetDesc(&desc))) {
          backWidth=desc.Width;
          backHeight=desc.Height;
        }
        back->Release();
      }
      // Prefer the actual presentation HWND; the focus window can be a
      // separate hidden/owner window and report misleading 4:3 dimensions.
      // Present's hDestWindowOverride is the actual output window for this
      // call, even when it differs from both CreateDevice's hDeviceWindow and
      // the focus HWND (common in window wrappers and DXVK setups).
      // An invalid override must not replace the last known good window.
      HWND clientWindow=(c && IsWindow(c)) ? c :
                        presentationWindow.load(std::memory_order_acquire);
      if(!clientWindow || !IsWindow(clientWindow)) {
        D3DDEVICE_CREATION_PARAMETERS creation{};
        clientWindow=nullptr;
        if(SUCCEEDED(dev->GetCreationParameters(&creation)) &&
           creation.hFocusWindow && IsWindow(creation.hFocusWindow))
          clientWindow=creation.hFocusWindow;
      }
      if(clientWindow) {
        RECT client{};
        if(GetClientRect(clientWindow,&client)) {
          clientWidth=UINT(client.right-client.left);
          clientHeight=UINT(client.bottom-client.top);
        }
      }
      Log("[D3D9] fps=%.1f viewport=%ux%u backbuffer=%ux%u client=%ux%u VS=%u PS=%u ViewProjVS=%u\r\n",
          frames/seconds,vp.Width,vp.Height,backWidth,backHeight,
          clientWidth,clientHeight,vsCount.load(),psCount.load(),
          viewProjCount.load());
    }
    then=now;frames=0;
  }
  // Do not persist a one-frame destination override: D3D9 permits the
  // caller to switch back to the device's regular presentation window.
  return realPresent(dev,a,b,c,d);
}
HRESULT STDMETHODCALLTYPE OnCreateVS(IDirect3DDevice9* dev,const DWORD* data,
                                     IDirect3DVertexShader9** output) {
  if(!realCreateVS)return D3DERR_INVALIDCALL;
  const HRESULT hr=realCreateVS(dev,data,output);
  if(SUCCEEDED(hr) && output && *output) {
    ShaderInfo reflection{};
    try {reflection=ReadCtab(*output);}catch(...){}
    {std::lock_guard lock(shaderMutex);shaderMap[*output]=reflection;}
    ++vsCount;
    if(reflection.hasView)++viewProjCount;
  }
  return hr;
}
HRESULT STDMETHODCALLTYPE OnCreatePS(IDirect3DDevice9* dev,const DWORD* data,
                                     IDirect3DPixelShader9** output) {
  if(!realCreatePS)return D3DERR_INVALIDCALL;
  const HRESULT hr=realCreatePS(dev,data,output);
  if(SUCCEEDED(hr))++psCount;
  return hr;
}
HRESULT STDMETHODCALLTYPE OnSetVS(IDirect3DDevice9* dev,IDirect3DVertexShader9* shader) {
  if(!realSetVS)return D3DERR_INVALIDCALL;
  const HRESULT hr=realSetVS(dev,shader);
  if(SUCCEEDED(hr)){std::lock_guard lock(shaderMutex);activeVS=shader;}
  return hr;
}
HRESULT STDMETHODCALLTYPE OnVSConstants(IDirect3DDevice9* dev,UINT reg,
                                        const float* data,UINT count) {
  if(!realVSConst)return D3DERR_INVALIDCALL;
  if(!fixAspect || !data || !count || count>256)return realVSConst(dev,reg,data,count);
  // Query the shader bound to THIS device, not the last shader set on
  // any device. The latter is shared process-wide and can apply a camera
  // matrix correction to a different device's UI or postprocessing pass.
  // GetVertexShader returns an AddRef'd pointer; release it in all cases.
  IDirect3DVertexShader9* boundShader=nullptr;
  if(FAILED(dev->GetVertexShader(&boundShader)) || !boundShader)
    return realVSConst(dev,reg,data,count);
  ShaderInfo reflected{};
  {std::lock_guard lock(shaderMutex);
   const auto it=shaderMap.find(boundShader);
   if(it!=shaderMap.end())reflected=it->second;}
  boundShader->Release();
  // D3D9 permits partial constant-register updates. Correct the intersection
  // with the four ViewProj columns, not only a single four-register upload.
  // Use widened arithmetic: a caller can supply a high register index.
  const uint64_t writeBegin=reg, writeEnd=writeBegin+count;
  const uint64_t matrixBegin=reflected.view, matrixEnd=matrixBegin+4;
  if(!reflected.hasView || !reflected.hasWorld || reflected.matrixClass!=3 ||
     writeEnd<=matrixBegin || writeBegin>=matrixEnd)
    return realVSConst(dev,reg,data,count);
  D3DVIEWPORT9 vp{};
  if(FAILED(dev->GetViewport(&vp))||!vp.Width||!vp.Height)
    return realVSConst(dev,reg,data,count);
  // Never infer a camera aspect from a HUD/minimap or partial viewport.
  // In particular, applying the 16:10 scale to a 4:3 UI viewport can
  // distort menu geometry even when its shader happens to expose ViewProj.
  // Only modify a full-target viewport; unknown targets fail closed.
  IDirect3DSurface9* renderTarget=nullptr;
  if(FAILED(dev->GetRenderTarget(0,&renderTarget)) || !renderTarget)
    return realVSConst(dev,reg,data,count);
  D3DSURFACE_DESC targetDesc{};
  const HRESULT descHr=renderTarget->GetDesc(&targetDesc);
  // Prefer the swap-chain backbuffer. Some D3D9 engines instead render
  // their 3D scene to a full-size texture, then composite it on Present.
  // Such passes are excluded by default. With explicit opt-in, accept only a
  // full-size render target matching the backbuffer dimensions and having a
  // matching depth-stencil surface with Z testing active. Fullscreen UI and
  // postprocessing passes normally lack these camera-depth prerequisites.
  IDirect3DSurface9* backbuffer=nullptr;
  const HRESULT backHr=dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
  const bool isBackbuffer=SUCCEEDED(backHr) && backbuffer &&
                          renderTarget==backbuffer;
  D3DSURFACE_DESC backDesc{};
  const bool hasBackDesc=backbuffer &&
                        SUCCEEDED(backbuffer->GetDesc(&backDesc));
  bool eligible=isBackbuffer;
  // A matching offscreen target can still be a postprocessing/UI surface.
  // Require a texture-backed render target, not an arbitrary plain surface:
  // the intended scene-composite path renders to a texture and samples it
  // later. This is an additional safeguard, not proof of camera semantics.
  // D3D9 surfaces created as texture levels have an IDirect3DTexture9
  // container. Query that concrete interface first: some implementations
  // (including wrappers) do not expose the base-texture IID directly through
  // GetContainer even though the returned texture inherits that interface.
  // Never accept a cube face or a plain offscreen surface as a 2D scene RT.
  IDirect3DTexture9* sceneTexture=nullptr;
  bool textureBacked=false;
  if(allowOffscreenHorPlus && !isBackbuffer &&
     SUCCEEDED(renderTarget->GetContainer(
         IID_IDirect3DTexture9,
         reinterpret_cast<void**>(&sceneTexture))) && sceneTexture) {
    // A matching texture container is not enough: the active RT might be a
    // nonzero mip level. Only widen the full-resolution scene surface at mip 0.
    // COM identity is stable for repeated GetSurfaceLevel(0) queries.
    IDirect3DSurface9* topLevel=nullptr;
    if(SUCCEEDED(sceneTexture->GetSurfaceLevel(0,&topLevel)) && topLevel)
      textureBacked=(topLevel==renderTarget);
    if(topLevel)topLevel->Release();
  }
  if(sceneTexture)sceneTexture->Release();
  if(!eligible && textureBacked && SUCCEEDED(descHr) &&
     hasBackDesc && targetDesc.Width==backDesc.Width &&
     targetDesc.Height==backDesc.Height &&
     (targetDesc.Usage & D3DUSAGE_RENDERTARGET)!=0) {
    DWORD zEnable=D3DZB_FALSE;
    IDirect3DSurface9* depth=nullptr;
    if(SUCCEEDED(dev->GetRenderState(D3DRS_ZENABLE,&zEnable)) &&
       zEnable!=D3DZB_FALSE &&
       SUCCEEDED(dev->GetDepthStencilSurface(&depth)) && depth) {
      D3DSURFACE_DESC depthDesc{};
      eligible=SUCCEEDED(depth->GetDesc(&depthDesc)) &&
               depthDesc.Width==targetDesc.Width &&
               depthDesc.Height==targetDesc.Height;
    }
    if(depth)depth->Release();
  }
  if(backbuffer)backbuffer->Release();
  renderTarget->Release();
  if(FAILED(descHr) || !eligible || vp.X!=0 || vp.Y!=0 ||
     vp.Width!=targetDesc.Width || vp.Height!=targetDesc.Height)
    return realVSConst(dev,reg,data,count);
  // A full-backbuffer HUD can share the same shader constant names as the
  // world and bypass viewport/scissor guards. Do not widen projection uploads
  // during depth-disabled 2D compositing. Depth-tested translucent 3D remains
  // eligible: ZWRITEENABLE need not be set for a valid camera pass.
  DWORD depthTest=D3DZB_FALSE;
  if(FAILED(dev->GetRenderState(D3DRS_ZENABLE,&depthTest)) ||
     depthTest==D3DZB_FALSE)
    return realVSConst(dev,reg,data,count);
  // A full viewport alone does not mean the entire scene is being drawn.
  // The PC renderer may use a scissor rectangle for HUD panels, maps and
  // other partial passes while leaving the viewport at backbuffer size.
  // Do not change their ViewProj constants; fail closed on state-query errors.
  DWORD scissorEnabled=FALSE;
  if(FAILED(dev->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled)))
    return realVSConst(dev,reg,data,count);
  if(scissorEnabled) {
    RECT scissor{};
    if(FAILED(dev->GetScissorRect(&scissor)) ||
       scissor.left!=0 || scissor.top!=0 ||
       scissor.right!=static_cast<LONG>(targetDesc.Width) ||
       scissor.bottom!=static_cast<LONG>(targetDesc.Height))
      return realVSConst(dev,reg,data,count);
  }
  const double scale=sourceAspect/(double(vp.Width)/vp.Height);
  if(!std::isfinite(scale)||scale<0.6||scale>1.8||
     std::abs(scale-1.0)<0.0001)return realVSConst(dev,reg,data,count);
  // This is a per-draw hot path. The validated D3D9 upload is at most
  // 256 float4 registers (4096 bytes), so avoid heap allocation, exception
  // handling and allocator contention on every camera constant update.
  float changed[256*4];
  memcpy(changed,data,size_t(count)*4*sizeof(float));
  // CTAB class 3 is D3DXPC_MATRIX_COLUMNS: four consecutive constant
  // registers contain the four *columns* of ViewProj. Horizontal clip
  // scaling modifies row X (component 0) of EACH column, not all four
  // components of the first column. The latter corrupts Y/Z/W and can
  // distort depth, clipping, and the apparent camera perspective.
  const uint64_t overlapBegin=std::max(writeBegin,matrixBegin);
  const uint64_t overlapEnd=std::min(writeEnd,matrixEnd);
  for(uint64_t column=overlapBegin;column<overlapEnd;++column) {
    const size_t xComponent=size_t(column-writeBegin)*4;
    changed[xComponent]=float(changed[xComponent]*scale);
  }
  return realVSConst(dev,reg,changed,count);
}
HRESULT STDMETHODCALLTYPE OnSetTexture(IDirect3DDevice9* dev,DWORD stage,
                                       IDirect3DBaseTexture9* texture) {
  if(!realSetTexture)return D3DERR_INVALIDCALL;
  const HRESULT hr=realSetTexture(dev,stage,texture);
  if(SUCCEEDED(hr) && stage<16 && realSampler) {
    bool point=false,linear=false,restoreMaximum=false;
    DWORD originalMaximum=0;
    {std::lock_guard lock(mipMutex);
     point=mipPointRequested[stage];
     linear=minLinearRequested[stage];
     restoreMaximum=afMaximumOverridden[stage];
     originalMaximum=afOriginalMaximum[stage];
    }
    // Restore the game's MAXANISOTROPY before evaluating the new texture.
    // A failed restore must not silently discard our rollback record or
    // apply a second AF override over the still-modified sampler.
    if(restoreMaximum) {
      const HRESULT restoreHr=realSampler(dev,stage,D3DSAMP_MAXANISOTROPY,originalMaximum);
      if(FAILED(restoreHr)) {
        Log("[D3D9] AF maximum restore failed stage=%lu hr=0x%08lX\r\n",
            (unsigned long)stage,(unsigned long)(unsigned int)restoreHr);
        // The new texture is already bound: avoid leaking the previous
        // texture's anisotropic MIN state onto an incompatible texture.
        // Preserve the MAX rollback record for the next restoration attempt.
        if(linear) {
          const HRESULT minHr=realSampler(dev,stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
          if(FAILED(minHr))
            Log("[D3D9] AF MIN repair failed stage=%lu hr=0x%08lX\r\n",
                (unsigned long)stage,(unsigned long)(unsigned int)minHr);
        }
        return hr;
      }
      std::lock_guard lock(mipMutex);
      afMaximumOverridden[stage]=false;
    }
    // Reapply after binding: capability checks depend on the bound texture.
    // This also restores the game's LINEAR min filter when the next texture
    // cannot use the anisotropic override from a previous texture.
    if(forceTrilinearMip && point) {
      // If the optional override is rejected after a texture-type switch,
      // restore the game's original POINT request rather than leaving the
      // previous texture's LINEAR mip state behind.
      if(FAILED(OnSampler(dev,stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT))) {
        const HRESULT fallbackHr=realSampler(dev,stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT);
        if(FAILED(fallbackHr))
          Log("[D3D9] MIP POINT fallback failed stage=%lu hr=0x%08lX\r\n",
              (unsigned long)stage,(unsigned long)(unsigned int)fallbackHr);
      }
    }
    if(anisotropy && linear) {
      // Likewise, a failed anisotropic reapply must not leak the previous
      // texture's anisotropic MIN state onto an unsupported new binding.
      if(FAILED(OnSampler(dev,stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))) {
        const HRESULT fallbackHr=realSampler(dev,stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        if(FAILED(fallbackHr))
          Log("[D3D9] MIN LINEAR fallback failed stage=%lu hr=0x%08lX\r\n",
              (unsigned long)stage,(unsigned long)(unsigned int)fallbackHr);
      }
    }
  }
  return hr;
}
HRESULT STDMETHODCALLTYPE OnSampler(IDirect3DDevice9* dev,DWORD sampler,
                                    D3DSAMPLERSTATETYPE type,DWORD value) {
  if(!realSampler)return D3DERR_INVALIDCALL;
  // An explicit engine MAX request supersedes any saved AF restoration.
  if(type==D3DSAMP_MAXANISOTROPY && sampler<16) {
    const HRESULT hr=realSampler(dev,sampler,type,value);
    if(SUCCEEDED(hr)) {
      std::lock_guard lock(mipMutex);
      afMaximumOverridden[sampler]=false;
    }
    return hr;
  }
  // A rejected engine MIN request must not change the sampler's MAX state.
  // Apply the requested MIN first, then retire our AF maximum only after
  // D3D9 accepts that transition. Keep rollback ownership on restore failure.
  if(type==D3DSAMP_MINFILTER && sampler<16 && value!=D3DTEXF_LINEAR) {
    const HRESULT minHr=realSampler(dev,sampler,type,value);
    if(FAILED(minHr))return minHr;
    {
      std::lock_guard lock(mipMutex);
      minLinearRequested[sampler]=false;
    }
    bool overridden=false;
    DWORD originalMaximum=0;
    {std::lock_guard lock(mipMutex);
     overridden=afMaximumOverridden[sampler];
     originalMaximum=afOriginalMaximum[sampler];}
    if(overridden) {
      const HRESULT restoreHr=realSampler(dev,sampler,D3DSAMP_MAXANISOTROPY,
                                          originalMaximum);
      if(SUCCEEDED(restoreHr)) {
        std::lock_guard lock(mipMutex);
        afMaximumOverridden[sampler]=false;
      } else {
        Log("[D3D9] AF maximum restore on MIN change failed stage=%lu hr=0x%08lX\r\n",
            (unsigned long)sampler,(unsigned long)(unsigned int)restoreHr);
      }
    }
    return minHr;
  }
  // Track only successful engine requests. Failed SetSamplerState calls must
  // not poison the remembered state used by the next SetTexture bind.
  const bool trackMip=type==D3DSAMP_MIPFILTER && sampler<16;
  const bool trackMin=type==D3DSAMP_MINFILTER && sampler<16;
  const bool pointRequested=value==D3DTEXF_POINT;
  const bool linearRequested=value==D3DTEXF_LINEAR;
  // Query the calling device, not the last device created. Multiple D3D9
  // devices (including DXVK-backed devices) may advertise different caps.
  // Only query when an enhancement could actually use them.
  D3DCAPS9 currentCaps{};
  const bool haveCaps=(forceTrilinearMip && trackMip && pointRequested) ||
                      (anisotropy && trackMin && linearRequested);
  const bool capsValid=haveCaps && SUCCEEDED(dev->GetDeviceCaps(&currentCaps));
  // Preserve NONE (no mip sampling) and MIN/MAG filters. Use the capability
  // for the actual bound texture kind rather than requiring every texture
  // class to support linear mip filtering. GetTexture returns an AddRef'd
  // interface; release it on every path. Unknown/unbound samplers pass through.
  if(forceTrilinearMip && sampler<16 &&
     type==D3DSAMP_MIPFILTER && value==D3DTEXF_POINT) {
    IDirect3DBaseTexture9* texture=nullptr;
    if(SUCCEEDED(dev->GetTexture(sampler,&texture)) && texture) {
      DWORD supported=0;
      switch(texture->GetType()) {
        case D3DRTYPE_TEXTURE: supported=capsValid ? (currentCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) : 0; break;
        case D3DRTYPE_CUBETEXTURE: supported=capsValid ? (currentCaps.CubeTextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) : 0; break;
        case D3DRTYPE_VOLUMETEXTURE: supported=capsValid ? (currentCaps.VolumeTextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) : 0; break;
        default: break;
      }
      // Preserve the original filter on single-level textures (often HUD,
      // fonts or UI atlases). Trilinear filtering only has meaning when at
      // least two source mip levels exist; do not manufacture new mipmaps.
      const UINT levels=texture->GetLevelCount();
      texture->Release();
      if(supported && levels>1)value=D3DTEXF_LINEAR;
    }
  }
  if(anisotropy && capsValid && currentCaps.MaxAnisotropy>1 && sampler<16) {
    const unsigned maximum=std::min(anisotropy,currentCaps.MaxAnisotropy);
    // Do not rewrite the game's independent MAXANISOTROPY requests.
    // The optional AF path sets its own maximum only when a compatible
    // bound texture actually accepts an anisotropic MIN filter. Rewriting
    // MAX here could affect samplers whose texture is unsupported or absent.
    if(type==D3DSAMP_MINFILTER && value==D3DTEXF_LINEAR) {
      // D3D9 advertises anisotropic MIN filtering separately for 2D,
      // cube and volume textures. Never force an unsupported filter.
      IDirect3DBaseTexture9* bound=nullptr;
      DWORD supported=0;
      if(SUCCEEDED(dev->GetTexture(sampler,&bound)) && bound) {
        switch(bound->GetType()) {
          case D3DRTYPE_TEXTURE: supported=currentCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC; break;
          case D3DRTYPE_CUBETEXTURE: supported=currentCaps.CubeTextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC; break;
          case D3DRTYPE_VOLUMETEXTURE: supported=currentCaps.VolumeTextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC; break;
          default: break;
        }
        bound->Release();
      }
      if(supported) {
        // A failed anisotropic MIN filter must not leave MAXANISOTROPY
        // modified behind the game's back. Save and restore that state on
        // failure; if it cannot be queried, use the original MIN filter.
        DWORD previousMaximum=0;
        if(SUCCEEDED(dev->GetSamplerState(sampler,D3DSAMP_MAXANISOTROPY,
                                          &previousMaximum))) {
          // Repeated LINEAR requests can observe our own forced maximum.
          // Keep the original game's value as the rollback target.
          DWORD rollbackMaximum=previousMaximum;
          {
            std::lock_guard lock(mipMutex);
            if(afMaximumOverridden[sampler])
              rollbackMaximum=afOriginalMaximum[sampler];
          }
          if(SUCCEEDED(realSampler(dev,sampler,D3DSAMP_MAXANISOTROPY,maximum))) {
            if(SUCCEEDED(realSampler(dev,sampler,type,D3DTEXF_ANISOTROPIC))) {
              std::lock_guard lock(mipMutex);
              minLinearRequested[sampler]=true;
              if(!afMaximumOverridden[sampler]) {
                afOriginalMaximum[sampler]=rollbackMaximum;
                afMaximumOverridden[sampler]=true;
              }
              return D3D_OK;
            }
            const HRESULT rollbackHr=realSampler(
                dev,sampler,D3DSAMP_MAXANISOTROPY,rollbackMaximum);
            if(SUCCEEDED(rollbackHr)) {
              // The original MAX has been restored. A previous successful AF
              // override must no longer own a stale rollback record.
              std::lock_guard lock(mipMutex);
              afMaximumOverridden[sampler]=false;
            } else {
              // The driver's MAX state may still be our forced value.
              // Retain the original game maximum for a later texture/MIN
              // transition rather than silently losing rollback ownership.
              {
                std::lock_guard lock(mipMutex);
                if(!afMaximumOverridden[sampler]) {
                  afOriginalMaximum[sampler]=rollbackMaximum;
                  afMaximumOverridden[sampler]=true;
                }
              }
              Log("[D3D9] AF maximum rollback after rejected MIN failed stage=%lu hr=0x%08lX\r\n",
                  (unsigned long)sampler,(unsigned long)(unsigned int)rollbackHr);
            }
          }
        }
      }
    }
  }
  // A forced trilinear MIP state is optional. Some D3D9 drivers (including
  // translation layers) may advertise the capability but reject this exact
  // sampler/texture combination. Fall back to the game's POINT request so
  // an enhancement cannot turn a valid game call into a rendering failure.
  HRESULT hr=realSampler(dev,sampler,type,value);
  if(FAILED(hr) && trackMip && pointRequested && value==D3DTEXF_LINEAR) {
    const HRESULT forcedHr=hr;
    hr=realSampler(dev,sampler,type,D3DTEXF_POINT);
    if(SUCCEEDED(hr))
      Log("[D3D9] trilinear rejected stage=%lu hr=0x%08lX; restored POINT\r\n",
          (unsigned long)sampler,(unsigned long)(unsigned int)forcedHr);
  }
  if(SUCCEEDED(hr) && (trackMip || trackMin)) {
    std::lock_guard lock(mipMutex);
    if(trackMip)mipPointRequested[sampler]=pointRequested;
    if(trackMin)minLinearRequested[sampler]=linearRequested;
  }
  // An earlier texture may have used our anisotropic MIN override. When the
  // game next requests LINEAR but the new binding does not support AF, the
  // normal SetSamplerState succeeds without entering the AF path above.
  // Retire our MAX override here as well, not just on non-LINEAR requests.
  // Otherwise an old forced maximum leaks into the new texture's sampler.
  if(SUCCEEDED(hr) && trackMin && linearRequested) {
    bool overridden=false;
    DWORD originalMaximum=0;
    {
      std::lock_guard lock(mipMutex);
      overridden=afMaximumOverridden[sampler];
      originalMaximum=afOriginalMaximum[sampler];
    }
    if(overridden) {
      const HRESULT restoreHr=realSampler(
          dev,sampler,D3DSAMP_MAXANISOTROPY,originalMaximum);
      if(SUCCEEDED(restoreHr)) {
        std::lock_guard lock(mipMutex);
        afMaximumOverridden[sampler]=false;
      } else {
        Log("[D3D9] AF maximum restore on unsupported LINEAR failed stage=%lu hr=0x%08lX\r\n",
            (unsigned long)sampler,(unsigned long)(unsigned int)restoreHr);
      }
    }
  }
  return hr;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE dll,DWORD reason,LPVOID) {
  if(reason==DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(dll);
    if(OriginalHulk()) {
      const bool iatHooked=HookIat();
      // Run camera initialization even if D3D9 IAT was hooked immediately.
      // The worker does not load DLLs or perform file I/O under DllMain.
      HANDLE worker=CreateThread(nullptr,0,TryLateHook,
                                 iatHooked ? nullptr : reinterpret_cast<void*>(1),
                                 0,nullptr);
      if(worker)CloseHandle(worker);
    }
  }
  return TRUE;
}
