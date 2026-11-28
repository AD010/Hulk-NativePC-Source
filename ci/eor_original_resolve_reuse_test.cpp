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
  // A real Draw or Clear into the resolve destination invalidates reuse,
  // even when its source image has not changed.
  cache.ColorWritten(2);
  Check(!cache.CanReuse(1,2,true));
  cache.Resolved(1,2,true);
  Check(cache.CanReuse(1,2,true));
  // The destination can become another pass's source. A later write to
  // that destination must invalidate both the direct and chained resolve.
  cache.Resolved(2,3,true);
  Check(cache.CanReuse(2,3,true));
  cache.ColorWritten(2);
  Check(!cache.CanReuse(1,2,true));
  Check(!cache.CanReuse(2,3,true));
  cache.Reset();
  Check(!cache.CanReuse(1,2,true));
  // A seventeenth target resets the generation table during Resolved.
  // A source subsequently assigned generation 1 must not resurrect it.
  for(uint32_t id=1;id<=16;++id)cache.ColorWritten(id);
  cache.Resolved(1,17,true);
  Check(!cache.CanReuse(1,17,true));
  cache.ColorWritten(1);
  Check(!cache.CanReuse(1,17,true));
  cache.Resolved(1,17,true);
  Check(cache.CanReuse(1,17,true));
  cache.Resolved(1,17,false);
  Check(!cache.CanReuse(1,17,true));
  cache.Reset();
  // Compare against a reference model under many source/destination changes.
  uint32_t rng=0x12345678u;
  auto next=[&](){rng=rng*1664525u+1013904223u;return rng>>8;};
  std::map<uint32_t,uint64_t> generation;
  struct ResolveState {uint32_t source;uint64_t source_gen,dest_gen;
    bool operator==(const ResolveState&) const=default;};
  std::map<uint32_t,ResolveState> result;
  // Eight sources and eight destinations fit the fixed 16-target cache.
  // Keep a reference model of the two independent image generations.
  for(unsigned i=0;i<100000;++i){
    const auto source=1u+next()%8u,dest=100u+next()%8u,op=next()%6u;
    if(op==0){cache.ColorWritten(source);++generation[source];}
    else if(op==1){
      cache.Resolved(source,dest,true);
      ++generation[dest];
      result[dest]={source,generation[source],generation[dest]};
    } else if(op==2){
      cache.Resolved(source,dest,false);
      ++generation[dest];
      result.erase(dest);
    } else if(op==3){cache.ColorWritten(dest);++generation[dest];}
    else if(op==4){
      cache.Resolved(dest,source,true);
      ++generation[source];
      result[source]={dest,generation[dest],generation[source]};
    } else {
      const bool expected=generation[source]!=0 && result.contains(dest) &&
          result.at(dest)==ResolveState{source,generation[source],generation[dest]};
      Check(cache.CanReuse(source,dest,true)==expected);
      Check(!cache.CanReuse(source,dest,false));
    }
  }
}
