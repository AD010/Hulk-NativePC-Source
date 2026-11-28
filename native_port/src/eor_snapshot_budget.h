#pragma once
#include <cstddef>
namespace hulk::eor::xdk {
// Drop only cache-owned snapshots. The protected key is the current
// read's prior version: its iterator and replacement credit stay valid.
// A Draw holding a shared owner keeps the original bytes immutable.
template<class Map,class Iterator>
bool ReclaimUnusedSnapshotEntries(Map& latest,Iterator protected_entry,
    size_t& retained,size_t budget,size_t requested,size_t replacement_credit=0) noexcept {
  if(retained>budget||replacement_credit>retained||requested>budget)return false;
  const auto fits=[&]{return requested<=budget-(retained-replacement_credit);};
  if(fits())return true;
  for(auto it=latest.begin();it!=latest.end();){
    if(it==protected_entry||it->second.use_count()!=1){++it;continue;}
    const auto bytes=it->second->bytes.size();
    if(bytes>retained-replacement_credit)return false;
    retained-=bytes;
    it=latest.erase(it);
    if(fits())return true;
  }
  return false;
}
} // namespace hulk::eor::xdk
