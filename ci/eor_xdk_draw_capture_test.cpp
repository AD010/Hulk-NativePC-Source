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
  // Exact original XDK PPC signature: r3=device, r4=primitive,
  // r5=signed base vertex, r6=start index, r7=index count.
  RawDraw live{Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,
               {0x40002880u,4u,0xFFFFFFFEu,12u,6u,0u,0u,0u}};
  const auto typed=DecodeOriginalIndexedXdkCall(live);
  assert(typed && typed->guest_device==0x40002880u &&
         typed->primitive==4u && typed->base_vertex==-2 &&
         typed->first_index==12 && typed->index_count==6);
  live.kind=Kind::DrawVertices;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.kind=Kind::DrawIndexedVertices;
  live.function=0x8288DF30u;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.function=0x8288E318u;
  live.gpr[3]=UINT32_MAX;
  assert(!DecodeOriginalIndexedXdkCall(live));
  live.gpr[3]=12u;
  live.gpr[4]=0;
  assert(!DecodeOriginalIndexedXdkCall(live));
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
  // Real EoR ABI discovery needs *repeatable callsite observations*, not
  // an assumption that PPC r3..r10 encode host Vulkan buffer handles.
  XdkCallsiteEvidence evidence;
  Frame a1{1,0,{a,a,b}};
  evidence.Observe(a1);
  auto hot=evidence.HotCallsites();
  assert(hot.size()==2 && hot[0].xdk_function==a.function);
  assert(hot[0].caller_lr==a.lr && hot[0].recorded_draws==2);
  assert(hot[0].frames_seen==1 && !hot[0].varying_gpr_mask);
  RawDraw moving=a;
  moving.gpr[0]=20;
  moving.gpr[4]=0;
  evidence.Observe(Frame{2,3,{moving,a}});
  hot=evidence.HotCallsites(1);
  assert(hot.size()==1 && hot[0].recorded_draws==4);
  assert(hot[0].frames_seen==2 && hot[0].first_frame==1 &&
         hot[0].last_frame==2 && hot[0].varying_gpr_mask==(1u<<0 | 1u<<4));
  assert(evidence.DroppedDraws()==3 && evidence.UntrackedCalls()==0);
  bool outOfOrder=false;
  try { evidence.Observe(Frame{2,0,{a}}); }
  catch(const std::invalid_argument&) { outOfOrder=true; }
  assert(outOfOrder);
  bool tooMany=false;
  try { (void)evidence.HotCallsites(XdkCallsiteEvidence::kMaxCallsites+1); }
  catch(const std::invalid_argument&) { tooMany=true; }
  assert(tooMany);
  evidence.Reset();
  assert(evidence.HotCallsites().empty() && evidence.DroppedDraws()==0);
}
