#include "../native_port/src/eor_fmod_start_trace.h"
#include <cassert>
using namespace hulk::eor::audio;
int main(){
  std::map<uint32_t,std::byte> mem;
  auto put=[&](uint32_t a,uint32_t x){for(unsigned i=0;i<4;++i)mem[a+i]=std::byte(x>>(24-8*i));};
  auto read=[&](uint32_t a,std::byte* p,size_t n){for(size_t i=0;i<n;++i){auto it=mem.find(a+static_cast<uint32_t>(i));if(it==mem.end())return false;p[i]=it->second;}return true;};
  const uint32_t event=0x1000,layer=0x2000,parent=0x3000,child=0x4000,owner=0x5000,def=0x6000,other=0x7000;
  put(event,0x82089270);put(event+264,7);put(layer+12,event);put(parent+16,layer);put(parent+12,owner);put(parent+76,0);put(owner+4,def);put(def+76,2);
  for(auto node:{child,other}){put(node+16,layer);put(node+12,owner);put(node+76,parent);put(node+20,8);put(node+36,node+0x4000);}
  put(parent+48,0);auto first=CaptureFmodSelectionStore(child,parent,0,read);assert(first);
  assert(!CaptureFmodSelectionStore(child,parent,1,read));
  put(parent+48,1);auto second=CaptureFmodSelectionStore(other,parent,1,read);assert(second);
  assert(ConfirmFmodChannelStart(*first,child,read)==child+0x4000);
  assert(!ConfirmFmodChannelStart(*first,other,read));
  uint8_t base=0,wrong=0;
  FmodStartTraceScope::Record(&base,*first);assert(!FmodStartTraceScope::Consume(&base));
  {FmodStartTraceScope scope(&base);FmodStartTraceScope::Record(&base,*first);
    {FmodStartTraceScope nested(&base);assert(!FmodStartTraceScope::Consume(&base));FmodStartTraceScope::Record(&base,*second);assert(!FmodStartTraceScope::Consume(&wrong));assert(FmodStartTraceScope::Consume(&base)->key.child==other);}
    assert(FmodStartTraceScope::Consume(&base)->key.child==child);assert(!FmodStartTraceScope::Consume(&base));
    FmodStartTraceScope::Record(&base,*first);FmodStartTraceScope::Reset(&base);assert(!FmodStartTraceScope::Consume(&base));FmodStartTraceScope::Record(&base,*first);
  }
  {FmodStartTraceScope scope(&base);assert(!FmodStartTraceScope::Consume(&base));}
  FmodStartTraceRegistry registry;assert(registry.Observe(*first,child+0x4000));assert(registry.Observe(*second,other+0x4000));
  auto get=[&](uint32_t node){return registry.Resolve(event,7,node,parent,node+0x4000,def);};
  assert(get(child)->variant==0&&get(other)->variant==1);auto serial=get(child)->serial;
  assert(registry.Observe(*first,child+0x4000)&&get(child)->serial>serial);
  assert(!registry.Resolve(event,8,child,parent,child+0x4000,def));
  registry.Retain([&](const auto& key,uint32_t){return key.child==other;});assert(!get(child)&&get(other));
  registry.Forget(other);assert(!get(other));
  put(child+20,0);assert(!ConfirmFmodChannelStart(*first,child,read));put(child+20,8);
  put(event+264,8);assert(!ConfirmFmodChannelStart(*first,child,read));
}
