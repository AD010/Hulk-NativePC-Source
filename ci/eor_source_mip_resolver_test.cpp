#include "../native_port/src/eor_source_mip_resolver.h"
#include <array>
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::gpu;
template<class F> void Reject(F fn) {
  bool ok=false;try{fn();}catch(const std::invalid_argument&){ok=true;}
  assert(ok);
}
int main() {
  OriginalMipWitness bc3{20,512,512,0xBEEFu,0xABCDu,0xDECAu};
  OriginalMipIdentity main{bc3,0x12345678,0,true};
  VerifiedSourceMipResolver a(std::array{main});
  assert(a.ambiguous_witnesses()==0);
  const auto found=a.Find(bc3);
  assert(found && found->original_texture_hash==main.original_texture_hash &&
         found->archived_stream && found->original_mip_level==0);
  auto mismatch=bc3;mismatch.width=256;
  assert(!a.Find(mismatch));
  mismatch=bc3;mismatch.xenos_format=18;
  assert(!a.Find(mismatch));
  mismatch=bc3;mismatch.second_fnv^=1;
  assert(!a.Find(mismatch));
  mismatch=bc3;mismatch.full_xxh3^=1;
  assert(!a.Find(mismatch));
  auto different=main;different.original_texture_hash=0x456789AB;
  VerifiedSourceMipResolver ambiguous(std::array{main,different});
  assert(ambiguous.ambiguous_witnesses()==1 && !ambiguous.Find(bc3));
  auto different_level=main;different_level.original_mip_level=1;
  VerifiedSourceMipResolver mismatched_level(std::array{main,different_level});
  assert(!mismatched_level.Find(bc3));
  VerifiedSourceMipResolver dup(std::array{main,main});
  assert(dup.Find(bc3));
  Reject([&]{auto bad=main;bad.witness.width=0;
    (void)VerifiedSourceMipResolver(std::array{bad});});
  Reject([&]{auto bad=main;bad.original_texture_hash=0;
    (void)VerifiedSourceMipResolver(std::array{bad});});
  Reject([&]{auto bad=main;bad.witness.xenos_format=0xFF;
    (void)VerifiedSourceMipResolver(std::array{bad});});
}
