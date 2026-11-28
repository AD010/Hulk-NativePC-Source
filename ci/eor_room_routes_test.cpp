#include "../native_port/src/eor_room_routes.h"
#include <cassert>
#include <limits>
using namespace hulk::eor::audio;
struct Room {int id=0;};
struct Sends {
  struct Send {uint32_t owner=0;float gain=0;std::optional<Room> room;};
  std::array<Send,16> values{};size_t count=0;
};
int main(){
  auto valid=[](Room r){return r.id>0;};
  Sends s;
  auto plan=[&]{return PlanNativeRoomRoutes<Room>(s,valid);};
  assert(plan()->count==0);
  s.count=3;s.values[0]={30,.8f,Room{3}};s.values[1]={10,.7f,Room{1}};s.values[2]={20,0,{}};
  auto a=plan();assert(a&&a->Valid(valid)&&a->count==2&&a->routes[0].owner==10&&a->routes[0].gain==.7f&&a->routes[1].gain==.8f&&a->routes[1].room.id==3);
  std::swap(s.values[0],s.values[1]);s.values[0].gain=.4f;auto b=plan();assert(b&&a->SameTopology(*b));
  s.values[2].gain=.1f;assert(!plan());s.values[2].room=Room{2};b=plan();assert(b&&b->count==3&&!a->SameTopology(*b));
  s.values[2].owner=10;assert(!plan());s.values[2].owner=20;s.values[2].gain=std::numeric_limits<float>::quiet_NaN();assert(!plan());
  s.count=9;for(size_t i=0;i<s.count;++i)s.values[i]={uint32_t(i+1),.1f,Room{1}};assert(!plan());
  s.count=17;assert(!plan());
  auto malformed=*a;malformed.count=99;assert(!malformed.Valid(valid)&&!malformed.SameTopology(malformed));
}
