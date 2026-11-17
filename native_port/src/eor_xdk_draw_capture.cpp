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
void CompletedFrameMailbox::Publish(Frame frame) {
  if(!frame.sequence)
    throw std::invalid_argument("Original XDK frame sequence missing at native handoff");
  if(pending_) {
    ++total_overwritten_;
    ++missed_since_take_;
  }
  // Capture transfers its complete per-Draw VS/PS/object-state snapshot
  // atomically at Present without copying thousands of original matrices.
  pending_.emplace(std::move(frame));
}
std::optional<DeliveredFrame> CompletedFrameMailbox::Take() {
  if(!pending_)return std::nullopt;
  DeliveredFrame result{std::move(*pending_),missed_since_take_};
  pending_.reset();
  missed_since_take_=0;
  return result;
}
void CompletedFrameMailbox::Reset() noexcept {
  pending_.reset();
  missed_since_take_=0;
  total_overwritten_=0;
}
namespace {
bool DrawKind(CommandKind kind) noexcept {
  return kind==CommandKind::DrawVertices ||
         kind==CommandKind::DrawIndexedVertices ||
         kind==CommandKind::DrawVerticesUP;
}
std::optional<CommandKind> ConvertDraw(Kind kind) noexcept {
  switch(kind) {
    case Kind::DrawVertices:return CommandKind::DrawVertices;
    case Kind::DrawIndexedVertices:return CommandKind::DrawIndexedVertices;
    case Kind::DrawVerticesUP:return CommandKind::DrawVerticesUP;
  }
  return std::nullopt;
}
uint32_t KnownUsaCommandAddress(CommandKind kind) noexcept {
  switch(kind) {
    case CommandKind::SetRenderTarget:return 0x8287F2C0u;
    case CommandKind::SetDepthStencilSurface:return 0x8287F628u;
    case CommandKind::ClearF:return 0x82888C40u;
    case CommandKind::Resolve:return 0x828897F8u;
    case CommandKind::BeginTiling:return 0x8288A900u;
    case CommandKind::EndTiling:return 0x8288AD98u;
    case CommandKind::Present:return 0x82880778u;
    default:return 0u;
  }
}
}
void FrameCapture::Record(const RawDraw& call) {
  if(!call.function)throw std::invalid_argument("Missing verified XDK address");
  if(calls_.size()>=kMaxDraws || commands_.size()>=kMaxCommands) {
    ++dropped_;
    ++dropped_commands_;
    return;
  }
  const auto kind=ConvertDraw(call.kind);
  if(!kind)throw std::invalid_argument("Unknown original XDK Draw kind");
  const RawCommand command{*kind,call.function,call.lr,
                           call.gpr,static_cast<uint32_t>(calls_.size())};
  calls_.push_back(call);
  try {
    commands_.push_back(command);
  } catch (...) {
    calls_.pop_back(); // never manufacture an unmatched Draw
    throw;
  }
}
void FrameCapture::RecordCommand(const RawCommand& command) {
  if(DrawKind(command.kind) ||
     !KnownUsaCommandAddress(command.kind) ||
     command.function!=KnownUsaCommandAddress(command.kind) ||
     command.draw_ordinal!=UINT32_MAX)
    throw std::invalid_argument("Original XDK non-Draw command ABI unverified");
  if(commands_.size()>=kMaxCommands) {
    ++dropped_commands_;
    return;
  }
  commands_.push_back(command);
}
bool ValidateOriginalCommandTimeline(const Frame& frame) noexcept {
  if(!frame.sequence || frame.dropped || frame.dropped_commands ||
     frame.commands.empty() || frame.calls.empty())
    return false;
  size_t next_draw=0;
  bool presented=false;
  for(size_t i=0;i<frame.commands.size();++i) {
    const auto& event=frame.commands[i];
    if(!event.function || presented)return false;
    if(event.kind==CommandKind::Present) {
      if(event.function!=KnownUsaCommandAddress(event.kind) ||
         event.draw_ordinal!=UINT32_MAX ||
         i+1!=frame.commands.size())return false;
      presented=true;
    } else if(DrawKind(event.kind)) {
      if(next_draw>=frame.calls.size() ||
         event.draw_ordinal!=next_draw)return false;
      const auto& original=frame.calls[next_draw++];
      const auto kind=ConvertDraw(original.kind);
      if(!kind || event.kind!=*kind ||
         event.function!=original.function ||
         event.lr!=original.lr || event.gpr!=original.gpr)
        return false;
    } else if(event.function!=KnownUsaCommandAddress(event.kind) ||
              event.draw_ordinal!=UINT32_MAX) {
      return false;
    }
  }
  return presented && next_draw==frame.calls.size();
}
Frame FrameCapture::Finish() {
  Frame out;
  out.sequence=++seq_;
  out.dropped=dropped_;
  out.dropped_commands=dropped_commands_;
  out.calls.swap(calls_);
  out.commands.swap(commands_);
  dropped_=0;
  dropped_commands_=0;
  return out;
}
void FrameCapture::Reset() {
  calls_.clear();
  commands_.clear();
  dropped_=0;
  dropped_commands_=0;
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
