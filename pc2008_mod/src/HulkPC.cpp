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
// Optional texture-quality enhancement; disabled unless explicitly configured.
bool forceTrilinearMip = false;
DWORD mipLinearCaps2D = 0, mipLinearCapsCube = 0, mipLinearCapsVolume = 0;
DWORD anisotropicCaps2D = 0, anisotropicCapsCube = 0, anisotropicCapsVolume = 0;
uint32_t width = 0, height = 0, anisotropy = 0, maxAnisotropy = 1;
double sourceAspect = 4.0 / 3.0;

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
    const UINT baseNumerator=GetPrivateProfileIntW(
        L"Graphics",L"BaseAspectNumerator",4,ini);
    const UINT baseDenominator=GetPrivateProfileIntW(
        L"Graphics",L"BaseAspectDenominator",3,ini);
    if(baseNumerator>=3 && baseNumerator<=64 &&
       baseDenominator>=3 && baseDenominator<=64)
      sourceAspect=double(baseNumerator)/double(baseDenominator);
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

IDirect3D9* WINAPI OnCreate9(UINT sdk) {
  if(!realCreate9)return nullptr;
  LoadConfig();
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
DWORD WINAPI TryLateHook(void*) {
  for(unsigned n=0;n<200;++n) {
    if(HookIat())return 0;
    Sleep(10);
  }
  OutputDebugStringA("HulkPC: original D3D9 import not found, leaving game untouched\n");
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
  const bool changed=width && height && !pp->Windowed &&
    (pp->BackBufferWidth!=width || pp->BackBufferHeight!=height);
  if(changed) {
    pp->BackBufferWidth=width;
    pp->BackBufferHeight=height;
  }
  HRESULT hr=realCreateDevice(api,adapter,type,window,flags,pp,result);
  if(FAILED(hr) && changed) {
    *pp=original;
    if(result)*result=nullptr;
    hr=realCreateDevice(api,adapter,type,window,flags,pp,result);
  }
  if(SUCCEEDED(hr) && *result) {
    // A new device starts with fresh sampler state. Never replay requests
    // recorded for a previous device after a CreateDevice/recreate cycle.
    {std::lock_guard lock(mipMutex);
     std::fill_n(mipPointRequested,16,false);
     std::fill_n(minLinearRequested,16,false);}
    {std::lock_guard lock(shaderMutex);activeVS=nullptr;}
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
  if(pp && width && height && !pp->Windowed) {
    original=*pp;
    changed=pp->BackBufferWidth!=width || pp->BackBufferHeight!=height;
    if(changed) {
      pp->BackBufferWidth=width;
      pp->BackBufferHeight=height;
    }
  }
  // A failed Reset leaves the old device state in place. Invalidate the
  // tracked shader only after a successful reset, not before the call.
  const HRESULT firstHr=realReset(dev,pp);
  HRESULT hr=firstHr;
  if(FAILED(hr) && changed) {
    *pp=original;
    hr=realReset(dev,pp);
    Log("[D3D9] Reset forced mode failed hr=0x%08lX; original retry hr=0x%08lX\r\n",
        (unsigned long)(unsigned int)firstHr,(unsigned long)(unsigned int)hr);
  }
  if(SUCCEEDED(hr)) {
    {std::lock_guard lock(shaderMutex);activeVS=nullptr;}
    {std::lock_guard lock(mipMutex);
     std::fill_n(mipPointRequested,16,false);
     std::fill_n(minLinearRequested,16,false);}
  }
  if(pp)Log("[D3D9] Reset %ux%u windowed=%u hr=0x%08lX\r\n",
            pp->BackBufferWidth,pp->BackBufferHeight,
            (unsigned)pp->Windowed,(unsigned long)(unsigned int)hr);
  return hr;
}
HRESULT STDMETHODCALLTYPE OnPresent(IDirect3DDevice9* dev,const RECT* a,
                                    const RECT* b,HWND c,const RGNDATA* d) {
  static LARGE_INTEGER frequency{},then{};
  static unsigned frames=0;
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if(!frequency.QuadPart) {
    QueryPerformanceFrequency(&frequency);
    then=now;
  }
  if(++frames>=300 && frequency.QuadPart) {
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
      D3DDEVICE_CREATION_PARAMETERS creation{};
      if(SUCCEEDED(dev->GetCreationParameters(&creation)) && creation.hFocusWindow) {
        RECT client{};
        if(GetClientRect(creation.hFocusWindow,&client)) {
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
  ShaderInfo reflected{};
  {std::lock_guard lock(shaderMutex);
   const auto it=shaderMap.find(activeVS);
   if(it!=shaderMap.end())reflected=it->second;}
  if(!reflected.hasView || !reflected.hasWorld || reflected.matrixClass!=3 ||
     reg>reflected.view || uint64_t(reg)+count<uint64_t(reflected.view)+4)
    return realVSConst(dev,reg,data,count);
  D3DVIEWPORT9 vp{};
  if(FAILED(dev->GetViewport(&vp))||!vp.Width||!vp.Height)
    return realVSConst(dev,reg,data,count);
  const double scale=sourceAspect/(double(vp.Width)/vp.Height);
  if(!std::isfinite(scale)||scale<0.6||scale>1.8||
     std::abs(scale-1.0)<0.0001)return realVSConst(dev,reg,data,count);
  try {
    std::vector<float> changed(data,data+size_t(count)*4);
    const size_t column=size_t(reflected.view-reg)*4;
    for(size_t c=0;c<4;++c)changed[column+c]=float(changed[column+c]*scale);
    return realVSConst(dev,reg,changed.data(),count);
  } catch(...) {return realVSConst(dev,reg,data,count);}
}
HRESULT STDMETHODCALLTYPE OnSetTexture(IDirect3DDevice9* dev,DWORD stage,
                                       IDirect3DBaseTexture9* texture) {
  if(!realSetTexture)return D3DERR_INVALIDCALL;
  const HRESULT hr=realSetTexture(dev,stage,texture);
  if(SUCCEEDED(hr) && stage<16 && realSampler) {
    bool point=false,linear=false;
    {std::lock_guard lock(mipMutex);
     point=mipPointRequested[stage];
     linear=minLinearRequested[stage];}
    // Reapply after binding: capability checks depend on the bound texture.
    // This also restores the game's LINEAR min filter when the next texture
    // cannot use the anisotropic override from a previous texture.
    if(forceTrilinearMip && point) {
      // If the optional override is rejected after a texture-type switch,
      // restore the game's original POINT request rather than leaving the
      // previous texture's LINEAR mip state behind.
      if(FAILED(OnSampler(dev,stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT)))
        realSampler(dev,stage,D3DSAMP_MIPFILTER,D3DTEXF_POINT);
    }
    if(anisotropy && linear) {
      // Likewise, a failed anisotropic reapply must not leak the previous
      // texture's anisotropic MIN state onto an unsupported new binding.
      if(FAILED(OnSampler(dev,stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR)))
        realSampler(dev,stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    }
  }
  return hr;
}
HRESULT STDMETHODCALLTYPE OnSampler(IDirect3DDevice9* dev,DWORD sampler,
                                    D3DSAMPLERSTATETYPE type,DWORD value) {
  if(!realSampler)return D3DERR_INVALIDCALL;
  // Track only successful engine requests. Failed SetSamplerState calls must
  // not poison the remembered state used by the next SetTexture bind.
  const bool trackMip=type==D3DSAMP_MIPFILTER && sampler<16;
  const bool trackMin=type==D3DSAMP_MINFILTER && sampler<16;
  const bool pointRequested=value==D3DTEXF_POINT;
  const bool linearRequested=value==D3DTEXF_LINEAR;
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
        case D3DRTYPE_TEXTURE: supported=mipLinearCaps2D; break;
        case D3DRTYPE_CUBETEXTURE: supported=mipLinearCapsCube; break;
        case D3DRTYPE_VOLUMETEXTURE: supported=mipLinearCapsVolume; break;
        default: break;
      }
      texture->Release();
      if(supported)value=D3DTEXF_LINEAR;
    }
  }
  if(anisotropy && maxAnisotropy>1 && sampler<16) {
    const unsigned maximum=std::min(anisotropy,maxAnisotropy);
    if(type==D3DSAMP_MAXANISOTROPY)value=maximum;
    else if(type==D3DSAMP_MINFILTER && value==D3DTEXF_LINEAR) {
      // D3D9 advertises anisotropic MIN filtering separately for 2D,
      // cube and volume textures. Never force an unsupported filter.
      IDirect3DBaseTexture9* bound=nullptr;
      DWORD supported=0;
      if(SUCCEEDED(dev->GetTexture(sampler,&bound)) && bound) {
        switch(bound->GetType()) {
          case D3DRTYPE_TEXTURE: supported=anisotropicCaps2D; break;
          case D3DRTYPE_CUBETEXTURE: supported=anisotropicCapsCube; break;
          case D3DRTYPE_VOLUMETEXTURE: supported=anisotropicCapsVolume; break;
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
          if(SUCCEEDED(realSampler(dev,sampler,D3DSAMP_MAXANISOTROPY,maximum))) {
            if(SUCCEEDED(realSampler(dev,sampler,type,D3DTEXF_ANISOTROPIC))) {
              std::lock_guard lock(mipMutex);
              minLinearRequested[sampler]=true;
              return D3D_OK;
            }
            realSampler(dev,sampler,D3DSAMP_MAXANISOTROPY,previousMaximum);
          }
        }
      }
    }
  }
  const HRESULT hr=realSampler(dev,sampler,type,value);
  if(SUCCEEDED(hr) && (trackMip || trackMin)) {
    std::lock_guard lock(mipMutex);
    if(trackMip)mipPointRequested[sampler]=pointRequested;
    if(trackMin)minLinearRequested[sampler]=linearRequested;
  }
  return hr;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE dll,DWORD reason,LPVOID) {
  if(reason==DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(dll);
    if(!HookIat() && OriginalHulk()) {
      HANDLE worker=CreateThread(nullptr,0,TryLateHook,nullptr,0,nullptr);
      if(worker)CloseHandle(worker);
    }
  }
  return TRUE;
}
