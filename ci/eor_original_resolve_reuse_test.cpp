#include "../native_port/src/eor_original_resolve_reuse.h"
#include <cstdlib>
#include <map>
using hulk::eor::gpu::OriginalResolveReuse;
static void Check(bool v){if(!v)std::abort();}
int main(){
  OriginalResolveReuse cache;
  Check(!cache.CanReuse(1,2,true));
  cache.ColorWritten(1);cache.Resolved(1,2,true);Check(cache.CanReuse(1,2,true));
  Check(!cache.CanReuse(1,2,false));
  cache.ColorWritten(3);Check(cache.CanReuse(1,2,true));
  cache.Resolved(3,2,true);Check(!cache.CanReuse(1,2,true));Check(cache.CanReuse(3,2,true));
  cache.Resolved(3,2,false);Check(!cache.CanReuse(3,2,true));
  cache.Resolved(1,2,true);cache.ColorWritten(1);Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);cache.DrawColorWritten(1,0);Check(cache.CanReuse(1,2,true));
  cache.DrawColorWritten(1,{});Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);cache.ColorWritten(2);Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);cache.Resolved(2,3,true);Check(cache.CanReuse(2,3,true));
  cache.ColorWritten(2);Check(!cache.CanReuse(1,2,true));Check(!cache.CanReuse(2,3,true));
  cache.Reset();Check(!cache.CanReuse(1,2,true));
  // A 17th texture no longer drops unrelated verified resolves.
  cache.ColorWritten(1);cache.Resolved(1,2,true);
  for(uint32_t id=3;id<=64;++id)cache.ColorWritten(id);
  Check(cache.CanReuse(1,2,true));
  // Evict the oldest write, then reintroduce it: no stale generation reuse.
  cache.ColorWritten(65);Check(!cache.CanReuse(1,2,true));
  cache.ColorWritten(1);Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);Check(cache.CanReuse(1,2,true));
  cache.Resolved(1,2,false);Check(!cache.CanReuse(1,2,true));
  cache.Reset();
  // Compare against an unbounded reference model. With <=64 active image
  // identities and <=64 resolve destinations all valid hits must survive.
  // Above capacity false negatives are allowed, false positives never are.
  for(uint32_t width:{20u,48u}){
    cache.Reset();uint32_t rng=0x12345678u;
    auto next=[&](){rng=rng*1664525u+1013904223u;return rng>>8;};
    std::map<uint32_t,uint64_t> generation;
    struct ResolveState {uint32_t source;uint64_t source_gen,dest_gen;
      bool operator==(const ResolveState&) const=default;};
    std::map<uint32_t,ResolveState> result;
    for(unsigned i=0;i<100000;++i){
      const auto source=1u+next()%width,dest=100u+next()%width,op=next()%6u;
      if(op==0){cache.ColorWritten(source);++generation[source];}
      else if(op==1){cache.Resolved(source,dest,true);++generation[dest];result[dest]={source,generation[source],generation[dest]};}
      else if(op==2){cache.Resolved(source,dest,false);++generation[dest];result.erase(dest);}
      else if(op==3){cache.ColorWritten(dest);++generation[dest];}
      else if(op==4){cache.Resolved(dest,source,true);++generation[source];result[source]={dest,generation[dest],generation[source]};}
      else {
        const bool expected=generation[source]!=0 && result.contains(dest) &&
            result.at(dest)==ResolveState{source,generation[source],generation[dest]};
        const bool actual=cache.CanReuse(source,dest,true);
        Check(!actual||expected);if(width==20)Check(actual==expected);
        Check(!cache.CanReuse(source,dest,false));
      }
    }
  }
}
