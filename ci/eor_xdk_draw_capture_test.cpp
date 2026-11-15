#include "../native_port/src/eor_xdk_draw_capture.h"
#include <cassert>
#include <stdexcept>
using namespace hulk::eor::xdk;
int main() {
  FrameCapture capture;
  RawDraw a{Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,{3,4,5,6,7,8,9,10}};
  RawDraw b{Kind::DrawVertices,0x8288DF30u,0,{4,5,6,7,8,9,10,11}};
  capture.Record(a);
  capture.Record(b);
  Frame first=capture.Finish();
  assert(first.sequence==1 && first.dropped==0);
  assert(first.calls.size()==2 && first.calls[0].function==0x8288E318u);
  assert(first.calls[1].gpr[7]==11);
  assert(first.calls[0].lr==0x8263BF50u);
  assert(capture.Finish().sequence==2);
  assert(capture.Finish().calls.empty());
  for(size_t i=0;i<FrameCapture::kMaxDraws+3;++i)capture.Record(a);
  Frame full=capture.Finish();
  assert(full.calls.size()==FrameCapture::kMaxDraws && full.dropped==3);
  capture.Record(b);
  capture.Reset();
  assert(capture.Finish().calls.empty());
  bool caught=false;
  try { capture.Record(RawDraw{}); }
  catch(const std::invalid_argument&) { caught=true; }
  assert(caught);
}
