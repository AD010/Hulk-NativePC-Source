#pragma once
#include "eor_dynamic_dsp.h"
#include <atomic>
#include <bit>
namespace hulk::eor::audio {
// Single writer (game audio service), single reader (voice callback).
// All words atomic; a busy publication leaves the prior complete chain active.
class LiveDspMailbox {
 public:
  bool Publish(const StaticDspChain& chain,uint32_t rate) noexcept {
    if(!ValidStaticDsp(chain,rate))return false;
    sequence_.fetch_add(1);
    words_[0].store(chain.count);
    for(unsigned i=0;i<chain.count;++i){
      words_[1+i*3].store(static_cast<uint32_t>(chain.stages[i].kind));
      words_[2+i*3].store(std::bit_cast<uint32_t>(chain.stages[i].value));
      words_[3+i*3].store(std::bit_cast<uint32_t>(chain.stages[i].q));
    }
    sequence_.fetch_add(1);return true;
  }
  bool Read(StaticDspChain& out) const noexcept {
    const auto before=sequence_.load();if(before&1u)return false;
    StaticDspChain next;const auto count=words_[0].load();if(count>next.stages.size())return false;
    next.count=static_cast<uint8_t>(count);
    for(unsigned i=0;i<count;++i)next.stages[i]={static_cast<DspKind>(words_[1+i*3].load()),
        std::bit_cast<float>(words_[2+i*3].load()),std::bit_cast<float>(words_[3+i*3].load())};
    if(sequence_.load()!=before)return false;
    out=next;return true;
  }
 private:
  static_assert(std::atomic<uint32_t>::is_always_lock_free);
  std::atomic<uint32_t> sequence_{0};
  std::array<std::atomic<uint32_t>,13> words_{};
};
// PCM is immutable and owned by the caller until playback is destroyed.
// No allocation, locks or whole-buffer rewrites on the consumer path.
class LiveDspStream {
 public:
  bool Initialize(std::span<const int16_t> pcm,uint32_t rate,uint16_t channels,
      uint32_t first,const StaticDspChain& initial) noexcept {
    if((channels!=1&&channels!=2)||pcm.empty()||pcm.size()%channels||first>=pcm.size()/channels||
       !ValidStaticDsp(initial,rate))return false;
    if(!processor_.Configure(rate,channels,initial)||!mailbox_.Publish(initial,rate))return false;
    pcm_=pcm;rate_=rate;channels_=channels;cursor_=size_t(first)*channels;return true;
  }
  bool Publish(const StaticDspChain& chain) noexcept {return rate_&&mailbox_.Publish(chain,rate_);}
  // Returns zero at EOF or on invalid output. The caller distinguishes using AtEnd().
  size_t Fill(std::span<int16_t> output) noexcept {
    if(!rate_||!channels_||output.empty()||output.size()%channels_)return 0;
    StaticDspChain chain;
    if(mailbox_.Read(chain)&&!processor_.Configure(rate_,channels_,chain))return 0;
    const auto count=(std::min)(output.size(),pcm_.size()-cursor_);
    std::array<float,256> scratch{};
    for(size_t pos=0;pos<count;){
      const auto n=(std::min)(scratch.size(),count-pos);
      for(size_t i=0;i<n;++i)scratch[i]=pcm_[cursor_+pos+i]/32768.f;
      if(!processor_.Process(std::span<float>(scratch).first(n)))return 0;
      for(size_t i=0;i<n;++i){
        if(!std::isfinite(scratch[i]))return 0;
        output[pos+i]=static_cast<int16_t>(std::lround(std::clamp(scratch[i]*32768.f,-32768.f,32767.f)));
      }
      pos+=n;
    }
    cursor_+=count;return count;
  }
  bool AtEnd() const noexcept {return cursor_==pcm_.size();}
 private:
  std::span<const int16_t> pcm_{};
  uint32_t rate_=0;uint16_t channels_=0;size_t cursor_=0;
  LiveDspMailbox mailbox_;
  DynamicDspProcessor processor_;
};
} // namespace hulk::eor::audio
