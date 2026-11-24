#include "../native_port/src/eor_original_resolve_region.h"
#include <cassert>
#include <iostream>
using namespace hulk::eor::xdk;
int main(){
  OriginalResolveRegion full{{0,0,1280,720},{0,0}};
  auto f=MapOriginalResolveRegion(full,1280,720,1280,720,2560,1600,2560,1600);
  assert(f && f->IsFull(2560,1600));
  OriginalResolveRegion partial{{8,16,32,48},{40,8}};
  auto p=MapOriginalResolveRegion(partial,64,64,64,64,128,128,128,128);
  assert(p && (p->source==std::array<int32_t,4>{16,32,64,96}) && (p->destination==std::array<int32_t,2>{80,16}));
  assert(!MapOriginalResolveRegion(partial,64,64,64,64,128,128,64,64));
  assert(!MapOriginalResolveRegion(partial,64,64,64,64,129,129,129,129));
  partial.destination={48,8};assert(!ValidOriginalResolveRegion(partial,64,64,64,64));
  partial.destination={-1,0};assert(!ValidOriginalResolveRegion(partial,64,64,64,64));
  partial={{0,0,16,16},{0,0}};assert(ValidOriginalResolveRegion(partial,16,16,16,16));
  std::cout<<"7 Resolve region mapping/bounds tests passed\n";
}
