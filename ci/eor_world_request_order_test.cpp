#include "../native_port/src/eor_world_request_order.h"
#include <cstdlib>
#include <deque>
#include <set>
using namespace hulk::eor::world;
static void Check(bool v){if(!v)std::abort();}
int main(){
  std::deque<unsigned> q{1,2,3,4,5};
  std::set<unsigned> urgent;
  auto promote=[&](unsigned h){urgent.insert(h);PromoteWorldRequest(q,std::find(q.begin(),q.end(),h),[&](unsigned v){return urgent.contains(v);});};
  promote(4);Check(q==std::deque<unsigned>{4,1,2,3,5});
  promote(2);Check(q==std::deque<unsigned>{4,2,1,3,5});
  for(unsigned i=0;i<10000;++i){promote(2);promote(4);Check(q==std::deque<unsigned>{4,2,1,3,5});}
  q.push_back(6);promote(6);Check(q==std::deque<unsigned>{4,2,6,1,3,5});
  q.pop_front();promote(3);Check(q==std::deque<unsigned>{2,6,3,1,5});
  promote(99);Check(q==std::deque<unsigned>{2,6,3,1,5});
}
