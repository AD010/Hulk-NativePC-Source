#pragma once
#include "eor_fsb3_bank.h"
#include <algorithm>
namespace hulk::eor::audio {
struct FevWaveformRef {
  size_t offset=0;
  uint32_t bank=0,weight=0,sample_ordinal=0,duration_ms=0;
  std::string_view path;
};
inline bool FevAsciiPath(std::string_view s) noexcept {
  return s.size()>=5 && s.size()<=1024 &&
      std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c<127;}) &&
      (s.ends_with(".wav")||s.ends_with(".WAV"));
}
// Bounded typed waveform records; not a complete event or layer parser.
inline std::optional<std::vector<FevWaveformRef>> InspectFevWaveforms(AudioBytes b,std::string_view project) {
  const auto refs=InspectOriginalFevBanks(b,project);
  if(!refs||b.size()>(2u<<20))return {};
  size_t p=16+project.size()+1;
  for(const auto& r:*refs)p+=12+r.stem.size()+1;
  if(p>b.size())return {};
  const auto txt=AudioText(b);
  std::vector<FevWaveformRef> out;
  for(size_t at=p;at+24<=b.size();++at) {
    if(AudioU32(b,at)!=0)continue;
    const auto w=AudioU32(b,at+4),n=AudioU32(b,at+8);
    if(!w||w>100||n<6||n>1025||n>b.size()-at-12)continue;
    const auto q=at+12+n;
    if(txt[q-1]!=0||b.size()-q<12)continue;
    const auto path=txt.substr(at+12,n-1);
    if(!FevAsciiPath(path))continue;
    const auto bn=AudioU32(b,q);
    if(bn<2||bn>129||bn>b.size()-q-12||txt[q+4+bn-1]!=0)continue;
    const auto bank=txt.substr(q+4,bn-1);
    for(uint32_t i=0;i<refs->size();++i)if((*refs)[i].stem==bank) {
      out.push_back({at,i,w,AudioU32(b,q+4+bn),AudioU32(b,q+8+bn),path});
      at=q+bn+11;break;
    }
  }
  return out;
}
inline bool FevEqualAscii(std::string_view a,std::string_view b) noexcept {
  const auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};
  return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[&](auto x,auto y){return fold(x)==fold(y);});
}
inline bool MatchesFevSampleName(std::string_view s,std::string_view path) noexcept {
  if(!FevAsciiPath(path))return false;
  const auto slash=path.find_last_of("/\\");
  const auto name=slash==std::string_view::npos?path:path.substr(slash+1);
  const auto stem=name.substr(0,name.size()-4);
  return FevEqualAscii(s,name)||FevEqualAscii(s,stem)||
      (s.size()==30&&(FevEqualAscii(s,name.substr(0,30))||FevEqualAscii(s,stem.substr(0,30))));
}
inline std::optional<uint32_t> ResolveFevWaveformSample(const OriginalWaveBank& bank,std::string_view path) noexcept {
  if(!FevAsciiPath(path))return {};
  const auto slash=path.find_last_of("/\\");
  const auto name=slash==std::string_view::npos?path:path.substr(slash+1);
  const auto stem=name.substr(0,name.size()-4);
  std::optional<uint32_t> found;
  for(uint32_t i=0;i<bank.samples.size();++i) {
    const auto s=bank.samples[i].name;
    if(!FevEqualAscii(s,name)&&!FevEqualAscii(s,stem)&&
       !(s.size()==30&&(FevEqualAscii(s,name.substr(0,30))||FevEqualAscii(s,stem.substr(0,30)))))continue;
    if(found)return {}; // never select the first duplicate/truncated name
    found=i;
  }
  return found;
}
// FEV1 v0x002c0000 waveform word after bank name is the sample ordinal.
inline std::optional<uint32_t> ResolveFevWaveformSample(const OriginalWaveBank& bank,const FevWaveformRef& ref) noexcept {
  if(ref.sample_ordinal>=bank.samples.size() ||
     !MatchesFevSampleName(bank.samples[ref.sample_ordinal].name,ref.path))return {};
  return ref.sample_ordinal;
}
} // namespace hulk::eor::audio
