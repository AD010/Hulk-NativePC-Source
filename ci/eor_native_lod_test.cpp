#include "../native_port/src/eor_native_lod.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::world;
namespace {
template<class F> void Reject(F f){bool hit=false;try{f();}catch(const std::invalid_argument&){hit=true;}assert(hit);}
std::vector<LodTier> Levels(){return {{101, 100.0}, {202, 300.0}, {303, std::numeric_limits<double>::infinity()}};}
}
int main(){
 ResidentLodSet world{Levels()};
 auto d=world.Update(50);
 assert(d.active_model_hash==0 && d.request_model_hash==101);
 world.SetResident(303,true);
 d=world.Update(50);
 assert(d.active_model_hash==303 && d.request_model_hash==101);
 world.SetResident(101,true);
 d=world.Update(50);
 assert(d.active_model_hash==101 && d.active_tier==0 && !d.pending_upgrade);
 d=world.Update(80);
 assert(d.prefetch_model_hash==202 && d.active_model_hash==101);
 // Jitter near 100 world units cannot make original LOD flicker.
 world.SetResident(202,true);
 for(int i=0;i<1200;++i){d=world.Update(i%2?102.0:99.0);assert(d.active_model_hash==101);}
 d=world.Update(113);
 assert(d.active_model_hash==202 && d.active_tier==1);
 for(int i=0;i<1200;++i){d=world.Update(i%2?99.0:102.0);assert(d.active_model_hash==202);}
 d=world.Update(87);
 assert(d.active_model_hash==101);
 d=world.Update(500);
 assert(d.active_model_hash==303);
 world.SetResident(101,false);
 world.SetResident(303,false);
 d=world.Update(20);
 assert(d.active_model_hash==202 && d.request_model_hash==101);
 world.SetResident(202,false);
 d=world.Update(20);
 assert(d.active_model_hash==0 && d.request_model_hash==101);
 world.SetResident(202,true);
 world.SetResident(101,true);
 d=world.Update(20);
 assert(d.active_model_hash==101);
 // More distant detail persists longer with user-selectable native policy.
 ResidentLodSet extended{Levels(), LodPolicy{3.0,0.1,0.25}};
 extended.SetResident(101,true);extended.SetResident(202,true);
 assert(extended.Update(250).active_model_hash==101);
 assert(extended.Update(335).active_model_hash==202);
 Reject([&]{(void)ResidentLodSet({{1,20},{2,10},{3,std::numeric_limits<double>::infinity()}});});
 Reject([&]{(void)ResidentLodSet({{1,20},{1,std::numeric_limits<double>::infinity()}});});
 Reject([&]{(void)ResidentLodSet({{1,100}});});
 Reject([&]{(void)ResidentLodSet({{1,std::numeric_limits<double>::infinity()}},LodPolicy{0.5,0.1,0.25});});
 Reject([&]{world.SetResident(999,true);});
 Reject([&]{world.Update(-1);});
 Reject([&]{world.Update(std::numeric_limits<double>::quiet_NaN());});
}
