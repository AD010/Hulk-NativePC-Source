#pragma once
#include <charconv>
#include <string_view>
#include <system_error>

namespace hulk::options {
inline bool IsHostAudioOption(std::string_view key) noexcept {
  return key=="audio_mute" || key=="audio_thread_priority" ||
         key=="audio_wasapi_exclusive" || key=="audio_maxqframes" ||
         key=="audio_callback_min_interval_us";
}
inline bool IsHostAudioValue(std::string_view key,std::string_view value) noexcept {
  if(key=="audio_mute" || key=="audio_thread_priority" ||
     key=="audio_wasapi_exclusive")return value=="true" || value=="false";
  if(value.empty() || value.size()>4 || value.front()<'0' || value.front()>'9' ||
     (value.size()>1 && value.front()=='0'))return false;
  int number=0;
  const auto result=std::from_chars(value.data(),value.data()+value.size(),number);
  if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size())return false;
  if(key=="audio_maxqframes")return number>=4 && number<=64;
  if(key=="audio_callback_min_interval_us")return number>=0 && number<=5000;
  return false;
}
inline bool IsLiveHostAudioOption(std::string_view key) noexcept {
  // The SDK reads mute and callback pacing while playing. Device creation,
  // semaphore queue capacity and thread scheduling are startup operations.
  return key=="audio_mute" || key=="audio_callback_min_interval_us";
}
} // namespace hulk::options
