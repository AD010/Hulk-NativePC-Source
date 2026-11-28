#pragma once
#include "eor_fev_voice_schedule.h"
#include <cstddef>
namespace hulk::eor::audio {
// Resolve an explicit finite event timeline into source segments before DSP.
// Each changed offset is an absolute wrapped seek; this is a host policy,
// not the original FMOD runtime's elapsed-time correction. End-of-stream
// terminates playback, even when subsequent schedule entries exist.
inline std::optional<std::vector<int16_t>> RenderFevSeekPcm(
    std::span<const int16_t> source,uint32_t rate,uint16_t channels,
    std::span<const FevVoiceBlock> blocks,size_t max_samples) {
  if(!rate||(channels!=1&&channels!=2)||source.empty()||source.size()%channels||
     source.size()/channels>UINT32_MAX||blocks.empty()||blocks.size()>16384||
     blocks.front().first_frame!=0||!blocks.front().controls.offset_ms||
     !HasMovingFevOffset(blocks))return {};
  const auto frames=static_cast<uint32_t>(source.size()/channels);
  for(size_t i=0;i<blocks.size();++i){
    if(!blocks[i].controls.offset_ms||blocks[i].first_frame>=frames||
       (i&&blocks[i].first_frame<=blocks[i-1].first_frame))return {};
  }
  struct Segment {size_t begin,count;};
  std::vector<Segment> segments;segments.reserve(blocks.size());
  size_t total=0;uint32_t cursor=0;
  for(size_t i=0;i<blocks.size();++i){
    if(!i||blocks[i].controls.offset_ms!=blocks[i-1].controls.offset_ms)
      cursor=FevOffsetFrame(*blocks[i].controls.offset_ms,frames,rate);
    const auto available=frames-cursor;
    const auto requested=i+1<blocks.size()?blocks[i+1].first_frame-blocks[i].first_frame:available;
    const auto take=(std::min)(available,requested);
    const size_t count=size_t(take)*channels;
    if(count>max_samples-total)return {};
    if(count)segments.push_back({size_t(cursor)*channels,count});
    total+=count;cursor+=take;
    if(take>=available)break;
  }
  if(!total)return {};
  std::vector<int16_t> output;output.reserve(total);
  for(const auto& s:segments){const auto part=source.subspan(s.begin,s.count);output.insert(output.end(),part.begin(),part.end());}
  return output;
}
} // namespace hulk::eor::audio
