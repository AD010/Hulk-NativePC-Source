#include "../native_port/src/eor_fsb3_ima.h"
#include "../native_port/src/eor_fsb3_mpeg.h"
#include <cassert>
using namespace hulk::eor::audio;
int main() {
  std::vector<std::byte> b(80);
  b[0]=std::byte{1};b[4]=std::byte{2};
  b[8]=std::byte{0x11};b[12]=std::byte{0x99};
  Fsb3Sample s{"test",64,0x20400040,4000,2,b};
  auto pcm=DecodeOriginalIma(s);
  assert(pcm && pcm->size()==128 && (*pcm)[0]==1 && (*pcm)[1]==2 &&
         (*pcm)[2]==2 && (*pcm)[3]==1 && (*pcm)[4]==3 && (*pcm)[5]==0);
  b[79]=std::byte{1};assert(!DecodeOriginalIma(s));b[79]=std::byte{};
  b[2]=std::byte{89};assert(!DecodeOriginalIma(s));b[2]=std::byte{};
  s.payload=s.payload.first(71);assert(!DecodeOriginalIma(s));
  // MPEG-1 Layer II 128kbps, 48kHz mono: 384-byte frame + bank padding.
  std::vector<std::byte> mp(400);
  mp[0]=std::byte{0xff};mp[1]=std::byte{0xfd};mp[2]=std::byte{0x84};mp[3]=std::byte{0xc0};
  s={"mpeg",1152,0x2220,48000,1,mp};
  auto f=OriginalMpegLayer2Frames(s);assert(f&&f->size()==1&&(*f)[0].size()==384);
  s.frames=2304;assert(!OriginalMpegLayer2Frames(s));s.frames=1152;
  mp[399]=std::byte{1};assert(!OriginalMpegLayer2Frames(s));mp[399]=std::byte{};
  mp[1]=std::byte{0xfb};assert(!OriginalMpegLayer2Frames(s));
}
