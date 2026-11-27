#include "../native_port/src/eor_immutable_buffer_cache.h"
#include <array>
#undef NDEBUG
#include <cassert>
#include <memory>
#include <cstdlib>
#include <cstdint>
#include <cstring>
using hulk::eor::gpu::ImmutableBufferCache;
// Diligent's intrusive smart pointer exposes an overloaded operator&
 // that produces an out-parameter helper. A plain shared_ptr test cannot
 // reproduce that compilation error or catch destructive out-parameter use.
struct OutParameterOwner {
  std::shared_ptr<int> value;
  int** operator&() noexcept {std::abort();}
};
int main() {
  {
    ImmutableBufferCache<OutParameterOwner> gpu_cache(16,2,8);
    std::array<std::byte,8> bytes{};
    const OutParameterOwner first{std::make_shared<int>(174)};
    gpu_cache.Remember(3,bytes,first);
    const auto* hit=gpu_cache.Find(3,bytes);
    assert(hit && hit->value==first.value);
    auto retained=hit->value;
    gpu_cache.Clear();
    assert(*retained==174 && !gpu_cache.Find(3,bytes));
  }
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
  // A hash is only a candidate selector. Construct two distinct 16-byte
  // streams with exactly the same two-word hash, using unsigned arithmetic
  // and native byte order just like MakeKey. Never return the wrong GPU owner.
  {
    constexpr uint64_t basis=0xcbf29ce484222325ull, prime=0x100000001b3ull;
    const uint64_t word0=1, word1=(basis*prime)^((basis^word0)*prime);
    std::array<std::byte,16> original{}, collision{};
    std::memcpy(collision.data(),&word0,sizeof(word0));
    std::memcpy(collision.data()+8,&word1,sizeof(word1));
    assert(original!=collision);
    assert((basis*prime)*prime==(((basis^word0)*prime)^word1)*prime);
    ImmutableBufferCache<Owner> colliding(64,4,16);
    colliding.Remember(3,original,first);
    assert(!colliding.Find(3,collision));
    colliding.Remember(3,collision,retained);
    assert(!colliding.Find(3,collision));
    assert(colliding.Size()==1 && colliding.RetainedBytes()==16);
    assert(colliding.Find(3,original) && *colliding.Find(3,original)==first);
    // Kind participates in the key even for otherwise colliding streams.
    colliding.Remember(4,collision,retained);
    assert(colliding.Find(4,collision) && *colliding.Find(4,collision)==retained);
    assert(!colliding.Find(4,original));
  }
  for(auto limits: {std::array<size_t,3>{0,4,16}, {64,0,16}, {64,4,0}, {7,4,16}}) {
    ImmutableBufferCache<Owner> disabled(limits[0],limits[1],limits[2]);
    disabled.Remember(3,a,first);
    assert(disabled.Size()==0 && disabled.RetainedBytes()==0 && !disabled.Find(3,a));
  }
  ImmutableBufferCache<Owner> one(24,1,16);
  one.Remember(3,a,first);one.Remember(3,b,retained);
  assert(one.Size()==1 && !one.Find(3,a) && *one.Find(3,b)==retained);
}
