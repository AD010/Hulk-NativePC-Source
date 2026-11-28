#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
namespace hulk::eor::audio {
// Admission holds every voice until the entire explicit event selection is
// ready. Failure cancels all admitted voices; no guest handle is fabricated.
// Callbacks must return a real nonzero voice ID, and stop must not throw.
template<class Cue,class Admit,class Resume,class Stop>
std::optional<std::vector<uint64_t>> AdmitEventVoiceGroup(
    std::span<const Cue> cues,Admit&& admit,Resume&& resume,Stop&& stop) {
  if(cues.empty()||cues.size()>64)return {};
  std::vector<uint64_t> ids;
  const auto rollback=[&]() noexcept {for(auto id:ids)stop(id);};
  try {
    ids.reserve(cues.size());
    for(const auto& cue:cues) {
      const auto id=admit(cue);
      if(!id){rollback();return {};}
      ids.push_back(id);
    }
    for(std::size_t i=0;i<ids.size();++i)
      if(!resume(cues[i],ids[i])){rollback();return {};}
    return ids;
  } catch (...) {rollback();return {};}
}
} // namespace hulk::eor::audio
