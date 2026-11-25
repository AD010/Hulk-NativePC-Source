#include "../native_port/src/eor_usa_material_textures.h"
#include <cassert>
#include <cstring>
using namespace hulk::eor::xdk;
struct Memory {std::array<std::byte,1024> bytes{};};
bool Read(void* p,uint32_t at,std::byte* out,size_t n) noexcept {
  auto& b=static_cast<Memory*>(p)->bytes;
  if(at>b.size()||n>b.size()-at)return false;
  std::memcpy(out,b.data()+at,n);return true;
}
void Write(Memory& m,uint32_t at,uint32_t v) {
  for(unsigned i=0;i<4;++i)m.bytes[at+i]=std::byte(v>>(24u-8u*i));
}
int main() {
  Memory m;
  auto make=[&](uint32_t asset,uint32_t wrapper,uint32_t hash,uint32_t bound) {
    Write(m,asset,0x82070CB8u);Write(m,asset+12,hash);
    Write(m,asset+20,wrapper);Write(m,wrapper+24,bound);
  };
  make(32,128,0x12345678,512);
  auto a=ReadOriginalTextureOwner(32,512,Read,&m);
  assert(a && a->hash==0x12345678u);
  OriginalTextureOwnerCache c;
  c.Remember(*a,Read,&m);assert(c.Find(512,Read,&m)==a);
  Write(m,152,516);assert(!c.Find(512,Read,&m));
  make(256,384,0x87654321,512);
  auto b=ReadOriginalTextureOwner(256,512,Read,&m);
  assert(b);c.Remember(*b,Read,&m);assert(c.Find(512,Read,&m)==b);
  Write(m,152,512);c.Remember(*a,Read,&m);assert(!c.Find(512,Read,&m));
  Write(m,152,516);assert(c.Find(512,Read,&m)==b);
  c.Remember(*b,Read,&m);
  Write(m,152,512);assert(!c.Find(512,Read,&m));
  Write(m,408,0);assert(c.Find(512,Read,&m)==a);
  Write(m,152,516);assert(!c.Find(512,Read,&m));
  Write(m,152,512);assert(c.Find(512,Read,&m)==a);
  c.Reset();c.Remember(*a,Read,&m);assert(c.Find(512,Read,&m)==a);
  Write(m,32,0);assert(!c.Find(512,Read,&m));
}
