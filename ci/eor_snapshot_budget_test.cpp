#include "../native_port/src/eor_snapshot_budget.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>
using namespace hulk::eor::xdk;
static void Check(bool b){if(!b)std::abort();}
struct Snapshot {std::vector<std::byte> bytes;};
int main(){
  std::map<int,std::shared_ptr<const Snapshot>> cache;
  size_t retained=0;
  const auto add=[&](int key,size_t size){cache.emplace(key,std::make_shared<Snapshot>(Snapshot{std::vector<std::byte>(size)}));retained+=size;};
  add(1,12);add(2,12);add(3,12);
  const auto live=cache.at(1);
  auto protected_entry=cache.find(2);
  Check(ReclaimUnusedSnapshotEntries(cache,protected_entry,retained,36,12));
  Check(retained==24&&cache.contains(1)&&cache.contains(2)&&!cache.contains(3));
  Check(ReclaimUnusedSnapshotEntries(cache,protected_entry,retained,24,12,12));
  Check(!ReclaimUnusedSnapshotEntries(cache,protected_entry,retained,24,12));
  Check(cache.at(1)==live&&protected_entry->second->bytes.size()==12);
  Check(!ReclaimUnusedSnapshotEntries(cache,cache.end(),retained,24,25));
  Check(!ReclaimUnusedSnapshotEntries(cache,cache.end(),retained,23,0));
  for(int i=4;i<10004;++i){
    Check(ReclaimUnusedSnapshotEntries(cache,cache.end(),retained,24,12));
    add(i,12);Check(retained==24&&cache.size()==2&&cache.at(1)==live);
  }
  Check(!ReclaimUnusedSnapshotEntries(cache,cache.find(10003),retained,24,13,12));
  Check(retained==24&&live->bytes.size()==12);
}
