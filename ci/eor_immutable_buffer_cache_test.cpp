#include "../native_port/src/eor_immutable_buffer_cache.h"
#include <array>
#include <cassert>
#include <memory>
using hulk::eor::gpu::ImmutableBufferCache;
int main() {
  using Owner=std::shared_ptr<int>;
  ImmutableBufferCache<Owner> cache(24,3,16);
  std::array<std::byte,8> a{},b{},c{},d{};
  b[2]=std::byte{1};c[2]=std::byte{2};d[2]=std::byte{3};
  auto first=std::make_shared<int>(17);
  cache.Remember(3,a,first);
  assert(cache.Find(3,a) && *cache.Find(3,a)==first);
  assert(!cache.Find(2,a) && !cache.Find(3,b));
  cache.Remember(3,b,std::make_shared<int>(18));
  cache.Remember(3,c,std::make_shared<int>(19));
  auto retained=*cache.Find(3,b);
  cache.Remember(3,d,std::make_shared<int>(20));
  assert(!cache.Find(3,a));
  assert(cache.Size()==3 && cache.RetainedBytes()==24);
  assert(cache.Find(3,b) && *cache.Find(3,b)==retained);
  cache.Remember(4,b,std::make_shared<int>(21));
  assert(cache.Find(4,b) && **cache.Find(4,b)==21);
  assert(cache.Find(3,b) && **cache.Find(3,b)==18);
  std::array<std::byte,32> large{};
  cache.Remember(3,large,first);assert(!cache.Find(3,large));
  assert(cache.RetainedBytes()==24);
  // Caller/Draw still holds its original immutable owner after eviction.
  cache.Clear();assert(!cache.Find(3,b) && cache.RetainedBytes()==0);
  assert(*retained==18 && *first==17);
  ImmutableBufferCache<Owner> one(24,1,16);
  one.Remember(3,a,first);one.Remember(3,b,retained);
  assert(one.Size()==1 && !one.Find(3,a) && *one.Find(3,b)==retained);
}
