#pragma once
// Bounded, byte-exact cache of immutable GPU allocation owners. Cache hits
// share physical storage, never source Draw IDs or mutable buffer handles.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <span>
#include <tuple>
#include <vector>

namespace hulk::eor::gpu {
template<class Value> class ImmutableBufferCache {
 public:
  ImmutableBufferCache(size_t bytes,size_t entries,size_t max_payload)
      : budget_(bytes),max_entries_(entries),max_payload_(max_payload) {}
  const Value* Find(uint32_t kind,std::span<const std::byte> bytes) {
    if(!Eligible(bytes))return nullptr;
    const auto found=index_.find(MakeKey(kind,bytes));
    if(found==index_.end())return nullptr;
    auto item=found->second;
    if(!std::equal(bytes.begin(),bytes.end(),item->bytes.begin()))return nullptr;
    entries_.splice(entries_.begin(),entries_,item);
    return &item->value;
  }
  void Remember(uint32_t kind,std::span<const std::byte> bytes,const Value& value) {
    if(!Eligible(bytes))return;
    const auto key=MakeKey(kind,bytes);
    if(index_.contains(key))return; // collision: leave the verified entry intact
    Entry entry{key,{bytes.begin(),bytes.end()},value};
    entries_.push_front(std::move(entry));
    try {index_.emplace(key,entries_.begin());}
    catch (...) {entries_.pop_front();throw;}
    used_+=bytes.size();
    while(used_>budget_ || entries_.size()>max_entries_) {
      auto& old=entries_.back();
      used_-=old.bytes.size();index_.erase(old.key);entries_.pop_back();
    }
  }
  size_t RetainedBytes() const noexcept {return used_;}
  size_t Size() const noexcept {return entries_.size();}
  void Clear() noexcept {index_.clear();entries_.clear();used_=0;}
 private:
  using Key=std::tuple<uint32_t,size_t,uint64_t>;
  struct Entry {Key key;std::vector<std::byte> bytes;Value value;};
  bool Eligible(std::span<const std::byte> bytes) const noexcept {
    return !bytes.empty() && max_entries_ && bytes.size()<=max_payload_ &&
           bytes.size()<=budget_ && budget_<=SIZE_MAX/2;
  }
  static Key MakeKey(uint32_t kind,std::span<const std::byte> bytes) noexcept {
    // Fast candidate selection only. Find always compares every byte.
    uint64_t hash=0xcbf29ce484222325ull;
    size_t i=0;
    for(;bytes.size()-i>=8;i+=8) {
      uint64_t word;std::memcpy(&word,bytes.data()+i,8);
      hash=(hash^word)*0x100000001b3ull;
    }
    for(;i<bytes.size();++i)
      hash=(hash^std::to_integer<uint8_t>(bytes[i]))*0x100000001b3ull;
    return {kind,bytes.size(),hash};
  }
  size_t budget_,max_entries_,max_payload_,used_=0;
  std::list<Entry> entries_;
  std::map<Key,typename std::list<Entry>::iterator> index_;
};
} // namespace hulk::eor::gpu
