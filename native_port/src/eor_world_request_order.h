#pragma once
#include <algorithm>
namespace hulk::eor::world {
// Promote only across speculative requests. Existing current-frame
// requests keep FIFO order even when requested on every Present.
template<class Queue,class Iterator,class IsUrgent>
void PromoteWorldRequest(Queue& requests,Iterator item,IsUrgent&& urgent) {
  if(item==requests.end())return;
  const auto boundary=std::find_if(requests.begin(),item,[&](const auto& hash){return !urgent(hash);});
  if(boundary!=item)std::rotate(boundary,item,item+1);
}
} // namespace hulk::eor::world
