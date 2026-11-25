#include "../native_port/src/native_audio_options.h"
#include <cstdlib>
#include <string>
using namespace hulk::options;
static void Check(bool ok){if(!ok)std::abort();}
int main(){
  for(int i=-5;i<=70;++i)
    Check(IsHostAudioValue("audio_maxqframes",std::to_string(i))==(i>=4 && i<=64));
  for(int i=-5;i<=5010;++i)
    Check(IsHostAudioValue("audio_callback_min_interval_us",std::to_string(i))==(i>=0 && i<=5000));
  for(auto key:{"audio_mute","audio_thread_priority","audio_wasapi_exclusive"}){
    Check(IsHostAudioOption(key));
    Check(IsHostAudioValue(key,"true") && IsHostAudioValue(key,"false"));
    Check(!IsHostAudioValue(key,"1"));
  }
  for(auto value:{"","-0","00"," 8","8 ","8.0","8e0","8#","9999999999"})
    Check(!IsHostAudioValue("audio_maxqframes",value));
  Check(!IsHostAudioValue("audio_callback_min_interval_us","-0"));
  Check(!IsHostAudioValue("not_a_real_option","8"));
  Check(IsLiveHostAudioOption("audio_mute"));
  Check(IsLiveHostAudioOption("audio_callback_min_interval_us"));
  Check(!IsLiveHostAudioOption("audio_maxqframes"));
  Check(!IsLiveHostAudioOption("audio_wasapi_exclusive"));
  Check(!IsLiveHostAudioOption("audio_thread_priority"));
}
