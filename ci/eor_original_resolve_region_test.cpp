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
  // 16:10 output uses a fractional vertical scale. Same-position and
  // integer-host translations preserve the source pixel lattice.
  OriginalResolveRegion aligned{{8,8,32,48},{8,8}};
  auto wide=MapOriginalResolveRegion(aligned,1280,720,1280,720,2560,1600,2560,1600);
  assert(wide && (wide->source==std::array<int32_t,4>{16,18,64,107}) &&
         (wide->destination==std::array<int32_t,2>{16,18}));
  aligned.destination={16,80}; // +72 guest Y = +160 host pixels
  wide=MapOriginalResolveRegion(aligned,1280,720,1280,720,2560,1600,2560,1600);
  assert(wide && (wide->destination==std::array<int32_t,2>{32,178}));
  aligned.destination={16,16}; // fractional host translation cannot CopyTexture
  assert(!MapOriginalResolveRegion(aligned,1280,720,1280,720,2560,1600,2560,1600));
  // Tile coverage: consecutive original scanlines partition the actual
  // host target at fractional scale, with no dropped or duplicate row.
  for(const uint32_t host_h : {1080u,1440u,1600u,2160u}) {
    int32_t previous=0;
    for(int32_t y=0;y<720;++y) {
      OriginalResolveRegion tile{{0,y,1280,y+1},{0,y}};
      const auto m=MapOriginalResolveRegion(tile,1280,720,1280,720,
                                            2560,host_h,2560,host_h);
      assert(m && m->source[1]==previous && m->source[3]>previous);
      // At a half-pixel tie, the boundary pixel belongs to the next tile.
      const int32_t expected=static_cast<int32_t>(
          (2ull*uint32_t(y+1)*host_h+719u)/1440u);
      assert(m->source[3]==expected && m->destination[1]==previous);
      previous=m->source[3];
    }
    assert(previous==static_cast<int32_t>(host_h));
  }
  partial.destination={48,8};assert(!ValidOriginalResolveRegion(partial,64,64,64,64));
  partial.destination={-1,0};assert(!ValidOriginalResolveRegion(partial,64,64,64,64));
  partial={{0,0,16,16},{0,0}};assert(ValidOriginalResolveRegion(partial,16,16,16,16));
  std::cout<<"Resolve region mapping/bounds tests passed\n";
}
