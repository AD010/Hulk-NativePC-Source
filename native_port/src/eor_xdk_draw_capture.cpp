#include "eor_xdk_draw_capture.h"
#include <bit>
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace hulk::eor::xdk {
std::shared_ptr<const OriginalGuestBufferBytes>
OriginalGuestBufferSnapshots::Capture(
    uint32_t object,uint32_t address,uint32_t bytes,
    ReadGuestBytes reader,void* reader_state) {
  if(!object || !address || !reader || bytes<6 || bytes>(8u<<20) ||
     bytes>budget_ || uint64_t(address)+bytes>uint64_t(UINT32_MAX)+1u)
    return {};
  scratch_.resize(bytes);
  // Failed/partial reads cannot fall back to an older cached revision.
  if(!reader(reader_state,address,scratch_.data(),bytes))return {};
  const auto key=std::make_tuple(object,address,bytes);
  const auto prior=latest_.find(key);
  if(prior!=latest_.end() && prior->second->bytes==scratch_)
    return prior->second;
  // Charge only NEW immutable bytes, including rewritten buffer versions
  // still held by earlier Draws. Repeated references work at a full budget.
  if(bytes>budget_-retained_bytes_)return {};
  auto next=std::make_shared<OriginalGuestBufferBytes>();
  next->guest_object=object;
  next->guest_address=address;
  next->guest_size=bytes;
  next->bytes=scratch_;
  latest_.insert_or_assign(key,next);
  retained_bytes_+=bytes;
  return next;
}
void OriginalGuestBufferSnapshots::Reset() noexcept {
  latest_.clear();
  retained_bytes_=0;
  scratch_.clear();
}

std::optional<OriginalXdkClearF> CaptureOriginalXdkClearF(
    const std::array<uint32_t,8>& gpr,double f1,
    ReadGuestBytes reader,void* guest_base) noexcept {
  // Hulk USA XDK ClearF is not the outer Clear (which calls ClearF):
  // r3=device, r4=flags RT0..3/DEPTH/STENCIL, r5=RECT*,
  // r6=float4* color, f1=Z, r8=stencil.
  if(!reader || !guest_base || !gpr[0] ||
     (gpr[1] & 0x3fu)==0 || (gpr[1] & ~0x3fu)!=0)
    return std::nullopt;
  OriginalXdkClearF out{};
  out.flags=gpr[1];
  out.stencil=static_cast<uint8_t>(gpr[5]);
  if((out.flags & 0x10u) && (!std::isfinite(f1) || f1<0.0 || f1>1.0))
    return std::nullopt;
  if(std::isfinite(f1))out.depth=static_cast<float>(f1);
  const auto be32=[](const std::byte* raw) noexcept -> uint32_t {
    return (uint32_t(std::to_integer<uint8_t>(raw[0]))<<24)|
           (uint32_t(std::to_integer<uint8_t>(raw[1]))<<16)|
           (uint32_t(std::to_integer<uint8_t>(raw[2]))<<8)|
            uint32_t(std::to_integer<uint8_t>(raw[3]));
  };
  if(gpr[2]) {
    if(gpr[2]&3u || uint64_t(gpr[2])+16>uint64_t(UINT32_MAX)+1)
      return std::nullopt;
    std::array<std::byte,16> raw{};
    if(!reader(guest_base,gpr[2],raw.data(),raw.size()))
      return std::nullopt;
    out.has_rect=true;
    for(size_t i=0;i<4;++i)
      out.rect[i]=std::bit_cast<int32_t>(be32(raw.data()+4*i));
    if(out.rect[2]<=out.rect[0] || out.rect[3]<=out.rect[1])
      return std::nullopt;
  }
  if(gpr[3]) {
    if(gpr[3]&3u || uint64_t(gpr[3])+16>uint64_t(UINT32_MAX)+1)
      return std::nullopt;
    std::array<std::byte,16> raw{};
    if(!reader(guest_base,gpr[3],raw.data(),raw.size()))
      return std::nullopt;
    out.has_color=true;
    for(size_t i=0;i<4;++i) {
      const float f=std::bit_cast<float>(be32(raw.data()+i*4));
      if(!std::isfinite(f))return std::nullopt;
      out.color[i]=f;
    }
  }
  // Any active original color target needs an actual 4-float clear color.
  if((out.flags & 0x0fu) && !out.has_color)return std::nullopt;
  return out;
}
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
  // Allocate first: OOM must not drop the previous frame or corrupt loss
  // accounting. A read-only preview remains alive if a newer frame arrives.
  auto next=std::make_shared<Frame>(std::move(frame));
  if(pending_) {
    ++total_overwritten_;
    ++missed_since_take_;
  }
  // Source eligibility checks can inspect this snapshot outside g_mutex.
  pending_=std::move(next);
}
std::optional<DeliveredFrame> CompletedFrameMailbox::Take() {
  // A live preview is immutable: never move its storage during validation.
  if(!pending_ || pending_.use_count()!=1)return std::nullopt;
  DeliveredFrame result{std::move(*pending_),missed_since_take_};
  pending_.reset();
  missed_since_take_=0;
  return result;
}
std::optional<DeliveredFrame> CompletedFrameMailbox::TakeIfSequence(
    uint64_t source_sequence) {
  if(!source_sequence || !pending_ ||
     pending_->sequence!=source_sequence)return std::nullopt;
  return Take();
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
      // Agreement between two observer copies is not XDK provenance.
      // Validate the verified USA 6534 function, PPC caller LR, device,
      // count and bounds for the SAME original Draw. UP has no proven
      // engine-level geometry/vertex pointer ownership and stays observe-only.
      if((original.kind==Kind::DrawIndexedVertices &&
          !DecodeOriginalIndexedXdkCall(original)) ||
         (original.kind==Kind::DrawVertices &&
          !DecodeOriginalVerticesXdkCall(original)) ||
         original.kind==Kind::DrawVerticesUP)
        return false;
    } else if(event.function!=KnownUsaCommandAddress(event.kind) ||
              event.draw_ordinal!=UINT32_MAX ||
              (event.kind==CommandKind::ClearF &&
               (!event.captured_clear ||
                event.captured_clear->flags!=event.gpr[1]))) {
      // An unobserved ClearF color/depth/rect is not a replayable original
      // Vulkan clear. The observer can still collect the malformed frame.
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
