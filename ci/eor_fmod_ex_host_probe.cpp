// Opt-in Windows x64 compatibility executable. Use the legally obtained,
// matching legacy FMOD Ex SDK headers; never link the Xbox ABI or Studio API.
#include <windows.h>
#include <fmod.h>
#include <fmod_event.h>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

static_assert(sizeof(void*)==8);
static_assert((FMOD_VERSION>>16)==4,"This probe requires FMOD Ex, not FMOD Studio");

namespace {
class Module {
 public:
  explicit Module(const std::filesystem::path& p)
      : handle_(LoadLibraryExW(p.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
  ~Module(){if(handle_)FreeLibrary(handle_);}
  Module(const Module&)=delete;
  Module& operator=(const Module&)=delete;
  explicit operator bool() const noexcept{return handle_!=nullptr;}
  template<class F> F Get(const char* symbol) const noexcept {
    static_assert(sizeof(F)==sizeof(FARPROC));
    return handle_?std::bit_cast<F>(GetProcAddress(handle_,symbol)):nullptr;
  }
 private:
  HMODULE handle_{};
};
// Legacy API takes narrow paths. Refuse lossy conversion instead of
// loading a different file. A UTF-8 system ACP needs different flags.
std::optional<std::string> Narrow(const std::wstring& w){
  if(w.empty()||w.size()>32767)return {};
  const bool utf8=GetACP()==CP_UTF8;
  const DWORD flags=utf8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
  BOOL substituted=FALSE;
  auto* used=utf8?nullptr:&substituted;
  const int size=WideCharToMultiByte(CP_ACP,flags,w.c_str(),-1,nullptr,0,nullptr,used);
  if(size<=0||substituted)return {};
  std::string s(static_cast<size_t>(size),'\0');
  if(!WideCharToMultiByte(CP_ACP,flags,w.c_str(),-1,s.data(),size,nullptr,used)||substituted)return {};
  s.pop_back();return s;
}
bool Check(const char* stage,FMOD_RESULT result){
  std::cout<<stage<<": "<<static_cast<int>(result)<<"\n";
  return result==FMOD_OK;
}
struct EventSession {
  FMOD_EVENTSYSTEM* system=nullptr;
  decltype(&FMOD_EventSystem_Release) release=nullptr;
  ~EventSession(){if(system&&release)(void)release(system);}
};
int Run(int argc,wchar_t** argv){
  if(argc!=2&&argc!=5){
    std::cerr<<"Usage: hulk_fmod_ex_probe <trusted SDK DLL directory> [<original PC .fev> <FSB media directory> <full event name>]\n";
    return 2;
  }
  namespace fs=std::filesystem;
  const auto dir=fs::canonical(argv[1]);
  if(!fs::is_directory(dir))return 2;
  const auto core_path=dir/L"fmodex64.dll",event_path=dir/L"fmodevent64.dll";
  if(!fs::is_regular_file(core_path)||!fs::is_regular_file(event_path)){
    std::cerr<<"Missing matching x64 DLL pair.\n";return 3;
  }
  Module core(core_path);
  if(!core){std::cerr<<"Core DLL load failed: "<<GetLastError()<<"\n";return 3;}
  Module events(event_path);
  if(!events){std::cerr<<"Event DLL load failed: "<<GetLastError()<<"\n";return 3;}
#define BIND(module, name) const auto name##_fn=(module).Get<decltype(&name)>(#name); if(!name##_fn){std::cerr<<"Missing export " #name "\n";return 4;}
  BIND(events,FMOD_EventSystem_Create)
  BIND(events,FMOD_EventSystem_Release)
  BIND(events,FMOD_EventSystem_GetVersion)
  BIND(events,FMOD_EventSystem_GetSystemObject)
  BIND(events,FMOD_EventSystem_Init)
  BIND(events,FMOD_EventSystem_Update)
  BIND(events,FMOD_EventSystem_SetMediaPath)
  BIND(events,FMOD_EventSystem_Load)
  BIND(events,FMOD_EventSystem_GetEvent)
  BIND(events,FMOD_Event_Start)
  BIND(events,FMOD_Event_Stop)
  BIND(events,FMOD_Event_GetState)
  BIND(core,FMOD_System_GetVersion)
#undef BIND
  EventSession session;
  session.release=FMOD_EventSystem_Release_fn;
  if(!Check("EventSystem_Create",FMOD_EventSystem_Create_fn(&session.system))||!session.system)return 5;
  unsigned int event_version=0,core_version=0;
  FMOD_SYSTEM* native_core=nullptr;
  if(!Check("EventSystem_GetVersion",FMOD_EventSystem_GetVersion_fn(session.system,&event_version))||
     !Check("EventSystem_GetSystemObject",FMOD_EventSystem_GetSystemObject_fn(session.system,&native_core))||!native_core||
     !Check("System_GetVersion",FMOD_System_GetVersion_fn(native_core,&core_version)))return 5;
  std::cout<<std::hex<<"core actual=0x"<<core_version<<" SDK=0x"<<FMOD_VERSION
           <<" event actual=0x"<<event_version<<" SDK=0x"<<FMOD_EVENT_VERSION<<std::dec<<"\n";
  if(core_version!=FMOD_VERSION||event_version!=FMOD_EVENT_VERSION){
    std::cerr<<"DLL/SDK version mismatch; no event initialization attempted.\n";return 6;
  }
  if(!Check("EventSystem_Init",FMOD_EventSystem_Init_fn(session.system,64,FMOD_INIT_NORMAL,nullptr,FMOD_EVENT_INIT_NORMAL)))return 7;
  if(argc==2){
    return Check("EventSystem_Update",FMOD_EventSystem_Update_fn(session.system))?0:7;
  }
  const auto fev=fs::canonical(argv[2]),media=fs::canonical(argv[3]);
  if(!fs::is_regular_file(fev)||!fs::is_directory(media))return 2;
  const auto file=Narrow(fev.wstring()),media_path=Narrow(media.wstring()+L"\\"),event_name=Narrow(argv[4]);
  if(!file||!media_path||!event_name){std::cerr<<"Legacy narrow path conversion failed.\n";return 2;}
  FMOD_EVENTPROJECT* project=nullptr;
  if(!Check("EventSystem_SetMediaPath",FMOD_EventSystem_SetMediaPath_fn(session.system,media_path->c_str()))||
     !Check("EventSystem_Load",FMOD_EventSystem_Load_fn(session.system,file->c_str(),nullptr,&project))||!project)return 8;
  FMOD_EVENT* event=nullptr;
  if(!Check("EventSystem_GetEvent",FMOD_EventSystem_GetEvent_fn(session.system,event_name->c_str(),FMOD_EVENT_DEFAULT,&event))||!event||
     !Check("Event_Start",FMOD_Event_Start_fn(event)))return 9;
  bool playing=false;
  for(unsigned i=0;i<150;++i){
    FMOD_EVENT_STATE state{};
    if(!Check("EventSystem_Update",FMOD_EventSystem_Update_fn(session.system))||
       !Check("Event_GetState",FMOD_Event_GetState_fn(event,&state)))return 9;
    if(state&FMOD_EVENT_STATE_ERROR)return 9;
    if(state&FMOD_EVENT_STATE_PLAYING)playing=true;
    else if(playing)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if(!Check("Event_Stop",FMOD_Event_Stop_fn(event,1)))return 9;
  std::cout<<"Event playing state observed: "<<playing<<"\n";
  return playing?0:10;
}
}
int wmain(int argc,wchar_t** argv){
  try{return Run(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
}
