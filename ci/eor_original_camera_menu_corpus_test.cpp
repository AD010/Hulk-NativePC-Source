#include "../native_port/src/eor_original_pause_vm.h"
#include "../native_port/src/eor_original_camera_speed.h"
#include "../native_port/src/eor_original_widget_api.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace hulk::eor::vm;
static void check(bool v){if(!v)std::abort();}
int main(int argc,char** argv){
  if(argc!=2)return 2;
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});
  if(image.empty())return 2;
  std::array<uint8_t,0x10000> heap{};
  constexpr uint32_t vm=0x1000,desc=0x4000,ctx=0x4100,page=0x4200,res=0x4300,def=0x4400,name=0x4500;
  constexpr uint32_t frame=0x5000,input=0x5100,nested=0x5200,arg=0x5300;
  uint8_t pitch=0,yaw=0;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)heap.at(a+i)=uint8_t(x>>(24-8*i));};
  auto read=[&](uint32_t a,uint8_t* dst,size_t n){
    if(n==1 && a==0x82e53b3cu){*dst=pitch;return true;}
    if(n==1 && a==0x82e53b3du){*dst=yaw;return true;}
    if(a>=0x82000000u && uint64_t(a)-0x82000000u+n<=image.size()){
      std::copy_n(image.data()+a-0x82000000u,n,dst);return true;
    }
    if(uint64_t(a)+n<=heap.size()){std::copy_n(heap.data()+a,n,dst);return true;}
    return false;
  };
  check(hulk::eor::ui::VerifyOriginalWidgetAbi(read));
  static_assert(hulk::eor::ui::WidgetSymbol("UIWidget_MenuItemTxt")==hulk::eor::ui::kMenuItemTemplate);
  {
    using namespace hulk::eor::ui;
    constexpr auto row_symbol=WidgetSymbol("NativePC_Graphics");
    auto make_widget=[&](uint32_t a,uint32_t parent,uint32_t symbol,uint32_t definition) {
      put(a+12,symbol);put(a+116,parent);put(a+124,a+0x100);
      put(a+0x108,a+0x200);put(a+0x200,0x8206A4D0u);put(a+0x20c,definition);
    };
    make_widget(0x6000,0,kSettingsDefinition,kSettingsDefinition);
    for(unsigned i=0;i<4;++i)put(0x9000+20*i,0x3f800000u);
    unsigned creates=0,writes=0;
    bool lookup_failed=false;
    auto invoke=[&](uint32_t address,std::span<const uint32_t> args)->std::optional<uint32_t> {
      if(address==0x825B8270u) {
        check(args.size()==2);
        if(lookup_failed)return {};
        if(args[0]==0x6000 && args[1]==row_symbol && creates)return 0x7000u;
        if(args[0]==0x7000 && args[1]==kTextChild && creates)return 0x8000u;
        return 0u;
      }
      if(address==0x825BDEE0u) {
        check(args.size()==6 && args[0]==0x6000 && args[1]==kMenuItemTemplate &&
              args[2]==0 && args[3]==0x9000 && args[4]==1 && args[5]==row_symbol);
        ++creates;
        make_widget(0x7000,0x6000,row_symbol,kMenuItemTemplate);
        make_widget(0x8000,0x7000,kTextChild,kTextTemplate);
        return 0x7000u;
      }
      if(address==0x8271D340u) {
        check(args.size()==2 && args[0]==0x8000 && args[1]==0xa000);
        ++writes;return 0u;
      }
      return {};
    };
    OriginalWidgetApi api(read,invoke);
    check(api.Verified());
    lookup_failed=true;
    check(!api.CreateMenuRow(0x6000,0x6000,row_symbol,0x9000) && creates==0);
    lookup_failed=false;
    const auto row=api.CreateMenuRow(0x6000,0x6000,row_symbol,0x9000);
    check(row && row->object==0x7000 && creates==1);
    check(api.CreateMenuRow(0x6000,0x6000,row_symbol,0x9000).has_value() && creates==1);
    put(0x720c,0x1234);
    check(!api.CreateMenuRow(0x6000,0x6000,row_symbol,0x9000) && creates==1);
    put(0x720c,kMenuItemTemplate);
    heap[0xa001]='P';heap[0xa003]='C';
    check(api.SetRowText(0x7000,0xa000,2) && writes==1);
    heap[0xa005]=1;
    check(!api.SetRowText(0x7000,0xa000,2) && writes==1);
  }
  put(vm,frame);put(frame,input);put(input+8,nested);put(nested,arg);
  put(ctx+20,page);put(vm+32,3);put(vm+4*(2093+3),page);
  put(page+124,res);put(res+8,def);put(def,0x8206a4d0);put(def+4,name|1);
  for(const auto& [key,label]:std::array<std::pair<uint32_t,std::string_view>,4>{{
      {0xd7a9e027u,"UIPage_Pause_Settings"},
      {0x30fe31f3u,"UIWidget_Frontend_Settings"},
      {0x04aa3a30u,"UIPage_Pause_GameSettings"},
      {0x141ed2a4u,"UIPage_Pause_CameraSettings"}}}){
    put(def+12,key);
    std::fill_n(heap.data()+name,32,0);std::copy(label.begin(),label.end(),heap.data()+name);
    for(const auto& [method,thunk]:std::array<std::pair<uint32_t,uint32_t>,2>{{
        {0x82338a00u,0x8218d3b0u},{0x82338a18u,0x8213a2b0u}}}) {
      put(desc,thunk);
      check(VerifyCameraGetter(method,0x825ea158,vm,vm,desc,ctx,read));
      check(!VerifyCameraGetter(method,0,vm,vm,desc,ctx,read));
      put(desc,0x82146a10u);
      check(!VerifyCameraGetter(method,0x825ea158,vm,vm,desc,ctx,read));
    }
    for(bool axis:{false,true}){
      const uint32_t method=axis?0x82339a08u:0x823399e8u;
      put(desc,axis?0x821204b0u:0x8217e250u);
      for(bool v:{false,true}){
        auto& previous=axis?yaw:pitch;previous=!v;heap[arg+12]=v;
        const auto change=CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read);
        check(change && change->yaw==axis && change->value==v);
        previous=v;check(!CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read));
        previous=!v;check(!CaptureCameraMenuChange(method,0,vm,vm,desc,ctx,read));
        heap[arg+12]=2;check(!CaptureCameraMenuChange(method,0x825ea158,vm,vm,desc,ctx,read));
      }
    }
  }
  for(uint32_t i=1;i<=300;++i)check(CameraSpeedSlider(CameraSpeedMultiplier(i))==i);
  check(CameraSpeedMultiplier(1)==0.25 && CameraSpeedMultiplier(100)==1.0 && CameraSpeedMultiplier(300)==4.0);
  put(def+12,0x30fe31f3u);
  const std::string_view frontend="UIWidget_Frontend_Settings";
  std::fill_n(heap.data()+name,32,0);std::copy(frontend.begin(),frontend.end(),heap.data()+name);
  for(const auto& m:kCameraSpeedMethods) {
    put(desc,m.thunk);put(arg+12,125);put(frame+4,0x5400);
    const auto e=CaptureCameraSpeed(m.method,0x825ea158,vm,vm,desc,ctx,read);
    check(e && e->method==&m && (m.setter?e->value==125:e->output==0x540c));
    check(!CaptureCameraSpeed(m.method,0,vm,vm,desc,ctx,read));
  }
  put(def+12,0);yaw=0;heap[arg+12]=1;
  check(!CaptureCameraMenuChange(0x82339a08,0x825ea158,vm,vm,desc,ctx,read));
  std::cout<<"PASS: original pause and title settings pages, both axes, true/false and rejected events\n";
}
