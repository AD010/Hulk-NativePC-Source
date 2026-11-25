#include "../native_port/src/eor_original_resolve_reuse.h"
#include <cstdlib>
#include <map>
using hulk::eor::gpu::OriginalResolveReuse;
static void Check(bool v){if(!v)std::abort();}
int main(){
  OriginalResolveReuse cache;
  Check(!cache.CanReuse(1,2,true));
  cache.ColorWritten(1);
  cache.Resolved(1,2,true);
  Check(cache.CanReuse(1,2,true));
  Check(!cache.CanReuse(1,2,false));
  cache.ColorWritten(3);
  Check(cache.CanReuse(1,2,true));
  cache.Resolved(3,2,true);
  Check(!cache.CanReuse(1,2,true));
  Check(cache.CanReuse(3,2,true));
  cache.Resolved(3,2,false);
  Check(!cache.CanReuse(3,2,true));
  cache.Resolved(1,2,true);
  cache.ColorWritten(1);
  Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);
  cache.Reset();
  Check(!cache.CanReuse(1,2,true));
  // Compare against a reference model under many source/destination changes.
  uint32_t rng=0x12345678u;
  auto next=[&](){rng=rng*1664525u+1013904223u;return rng>>8;};
  std::map<uint32_t,uint64_t> generation;
  std::map<uint32_t,std::pair<uint32_t,uint64_t>> result;
  for(unsigned i=0;i<100000;++i){
    const auto source=1u+next()%16u,dest=100u+next()%32u,op=next()%4u;
    if(op==0){cache.ColorWritten(source);++generation[source];}
    else if(op==1){cache.Resolved(source,dest,true);result[dest]={source,generation[source]};}
    else if(op==2){cache.Resolved(source,dest,false);result.erase(dest);}
    else {
      const bool expected=generation[source]!=0 && result.contains(dest) &&
         result.at(dest)==std::pair{source,generation[source]};
      Check(cache.CanReuse(source,dest,true)==expected);
      Check(!cache.CanReuse(source,dest,false));
    }
  }
}
