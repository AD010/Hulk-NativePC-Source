#include "eor_xdk_draw_capture.h"
#include <bit>
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace hulk::eor::xdk {
std::optional<IndexedXdkCall> DecodeOriginalIndexedXdkCall(const RawDraw& call) {
  // Method 0x8288E318 is the exact SHA-pinned USA XDK DrawIndexedVertices
  // implementation; the other r8-r10 fields are caller-volatiles, NOT
  // additional method parameters. Do not treat raw r3 as a Vulkan handle.
  if(call.kind!=Kind::DrawIndexedVertices || call.function!=0x8288E318u ||
     !call.gpr[0] || !call.gpr[1] || !call.gpr[4] ||
     call.lr<0x82000000u || call.lr>=0x84000000u || (call.lr&3u) ||
     uint64_t(call.gpr[3])+call.gpr[4]>UINT32_MAX)
    return std::nullopt;
  return IndexedXdkCall{call.function,call.lr,call.gpr[0],call.gpr[1],
                        call.gpr[3],call.gpr[4],
                        std::bit_cast<int32_t>(call.gpr[2])};
}
std::optional<VerticesXdkCall> DecodeOriginalVerticesXdkCall(const RawDraw& call) {
  if(call.kind!=Kind::DrawVertices || call.function!=0x8288DF30u ||
     !call.gpr[0] || !call.gpr[1] || !call.gpr[3] ||
     call.lr<0x82000000u || call.lr>=0x84000000u || (call.lr&3u) ||
     uint64_t(call.gpr[2])+call.gpr[3]>UINT32_MAX)
    return std::nullopt;
  return VerticesXdkCall{call.function,call.lr,call.gpr[0],call.gpr[1],
                         call.gpr[2],call.gpr[3]};
}
void FrameCapture::Record(const RawDraw& call) {
  if (!call.function) throw std::invalid_argument("Missing verified XDK address");
  if (calls_.size() >= kMaxDraws) {
    ++dropped_;
    return;
  }
  calls_.push_back(call);
}
Frame FrameCapture::Finish() {
  Frame out;
  out.sequence = ++seq_;
  out.dropped = dropped_;
  out.calls.swap(calls_);
  dropped_ = 0;
  return out;
}
void FrameCapture::Reset() {
  calls_.clear();
  dropped_ = 0;
}

void XdkCallsiteEvidence::Observe(const Frame& frame) {
  if(!frame.sequence || frame.sequence<=last_sequence_)
    throw std::invalid_argument("Out-of-order original XDK provenance frame");
  last_sequence_=frame.sequence;
  dropped_draws_+=frame.dropped;
  for(const auto& call:frame.calls) {
    if(!call.function)throw std::invalid_argument("Unidentified original XDK draw");
    const auto key=std::make_tuple(call.kind,call.function,call.lr);
    auto it=records_.find(key);
    if(it==records_.end()) {
      if(records_.size()>=kMaxCallsites) {++untracked_calls_;continue;}
      CallsiteEvidenceRecord init{};
      init.kind=call.kind;
      init.xdk_function=call.function;
      init.caller_lr=call.lr;
      init.first_gpr=call.gpr;
      init.first_frame=frame.sequence;
      it=records_.emplace(key,init).first;
    }
    auto& row=it->second;
    if(row.last_frame!=frame.sequence)++row.frames_seen;
    ++row.recorded_draws;
    row.last_frame=frame.sequence;
    for(unsigned i=0;i<8;++i) {
      if(call.gpr[i]!=row.first_gpr[i])row.varying_gpr_mask|=1u<<i;
      if(call.gpr[i])row.nonzero_gpr_mask|=1u<<i;
    }
  }
}
std::vector<CallsiteEvidenceRecord> XdkCallsiteEvidence::HotCallsites(
    std::size_t limit) const {
  if(limit>kMaxCallsites)throw std::invalid_argument("Unbounded original PPC caller report");
  std::vector<CallsiteEvidenceRecord> out;
  out.reserve(records_.size());
  for(const auto& [key,row]:records_)out.push_back(row);
  std::sort(out.begin(),out.end(),[](const auto& a,const auto& b) {
    if(a.recorded_draws!=b.recorded_draws)
      return a.recorded_draws>b.recorded_draws;
    if(a.frames_seen!=b.frames_seen)return a.frames_seen>b.frames_seen;
    if(a.xdk_function!=b.xdk_function)return a.xdk_function<b.xdk_function;
    if(a.caller_lr!=b.caller_lr)return a.caller_lr<b.caller_lr;
    return a.kind<b.kind;
  });
  if(out.size()>limit)out.resize(limit);
  return out;
}
void XdkCallsiteEvidence::Reset() noexcept {
  records_.clear();
  last_sequence_=0;
  dropped_draws_=0;
  untracked_calls_=0;
}
}  // namespace hulk::eor::xdk
