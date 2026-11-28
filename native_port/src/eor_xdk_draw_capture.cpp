#include "eor_xdk_draw_capture.h"
#include "eor_snapshot_budget.h"
#include <bit>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <new>

namespace hulk::eor::xdk {
namespace {
uint32_t SourceWord(const std::byte* b) noexcept {
  uint32_t v=0;
  for(unsigned i=0;i<4;++i)v=(v<<8)|std::to_integer<uint8_t>(b[i]);
  return v;
}
}
std::optional<UsaIndexBufferBinding> ReadUsaIndexBufferBinding(
    uint32_t object,ReadGuestBytes read,void* state) noexcept {
  if(!object || !read || uint64_t(object)+32>(uint64_t{1}<<32))return {};
  std::array<std::byte,32> header{};
  if(!read(state,object,header.data(),header.size()))return {};
  // USA DrawIndexedVertices: r22 = device+12436; 8288E5CC..E654 uses
  // flag bit 31 for startIndex*2 or *4, and bits 29..30 for VGT_DMA_SIZE.
  const uint32_t flags=SourceWord(header.data()),width=(flags>>31)?4u:2u;
  const uint32_t address=SourceWord(header.data()+24),bytes=SourceWord(header.data()+28);
  if(!address || address%width || !bytes || bytes%width ||
     uint64_t(address)+bytes>(uint64_t{1}<<32))return {};
  return UsaIndexBufferBinding{object,address,bytes,width,(flags>>29)&3u};
}
std::optional<uint32_t> UsaIndexBufferBinding::Read(
    std::span<const std::byte> raw,uint32_t index) const noexcept {
  if((element_bytes!=2 && element_bytes!=4) || endian>=4 || bytes!=raw.size() ||
     !address || address%element_bytes || bytes%element_bytes ||
     uint64_t(address)+bytes>(uint64_t{1}<<32) ||
     index>=bytes/element_bytes)return {};
  const uint64_t at=uint64_t(address)+uint64_t(index)*element_bytes;
  uint32_t value=0;
  for(uint32_t i=0;i<element_bytes;++i) {
    const auto physical=(at+i)^ByteXor();
    if(physical<address || physical-address>=bytes)return {};
    value|=std::to_integer<uint32_t>(raw[size_t(physical-address)])<<(i*8);
  }
  return value;
}
std::optional<UsaVertexDeclaration> ReadUsaVertexDeclaration(
    uint32_t object,ReadGuestBytes read,void* state) noexcept {
  // USA 82871188 reads the element count at +24 and walks the entries
  // at +52. The creator at 82870C40 writes stream/offset as two BE16s,
  // followed by type and method/usage/index. The count excludes the terminator.
  constexpr size_t max_elements=64,entry_bytes=12;
  if(!object || !read || uint64_t(object)+52>(uint64_t{1}<<32))return {};
  std::array<std::byte,4> count_bytes{};
  if(!read(state,object+24,count_bytes.data(),count_bytes.size()))return {};
  const auto count=SourceWord(count_bytes.data());
  if(!count || count>max_elements ||
     uint64_t(object)+52+count*entry_bytes>(uint64_t{1}<<32))return {};
  std::array<std::byte,max_elements*entry_bytes> raw;
  if(!read(state,object+52,raw.data(),count*entry_bytes))return {};
  constexpr std::array<uint32_t,9> font{
      0,0x2A23B9,0, 12,0x182886,0xA0000, 16,0x2C23A5,0x50000};
  UsaVertexDeclaration declaration{object,0,count==3};
  declaration.elements.reserve(count);
  for(uint32_t i=0;i<count;++i) {
    const auto* entry=raw.data()+i*entry_bytes;
    const uint32_t stream_offset=SourceWord(entry);
    const uint32_t stream=stream_offset>>16;
    if(stream>=16)return {};
    declaration.elements.push_back(UsaVertexElement{
        static_cast<uint16_t>(stream),
        static_cast<uint16_t>(stream_offset),
        SourceWord(entry+4),SourceWord(entry+8)});
    declaration.streams|=static_cast<uint16_t>(1u<<stream);
    if(declaration.font_main_elements)
      for(size_t word=0;word<3;++word)
        if(SourceWord(entry+word*4)!=font[i*3+word])declaration.font_main_elements=false;
  }
  return declaration;
}
std::optional<UsaVertexStreamBinding> ReadUsaVertexStreamBinding(
    uint32_t device,uint32_t slot,uint32_t object,ReadGuestBytes read,void* state) noexcept {
  if(!device || !object || slot>=16 || !read || uint64_t(object)+32>(uint64_t{1}<<32) ||
     uint64_t(device)+12544>(uint64_t{1}<<32))return {};
  std::array<std::byte,8> resource{},bound{};
  std::byte stride{};
  // SetStreamSource 8287E674..E6B0 adds the byte offset to the tagged
  // address and subtracts it from the encoded length. GetStreamSource
  // 8287E7A4..E7E8 reads the same reverse-numbered slot and stride / 4.
  if(!read(state,object+24,resource.data(),8) ||
     !read(state,device+(239-slot)*8,bound.data(),8) ||
     !read(state,device+12528+slot,&stride,1))return {};
  const uint32_t tagged=SourceWord(resource.data()),source_size=SourceWord(resource.data()+4);
  const uint32_t physical=SourceWord(bound.data()),bound_size=SourceWord(bound.data()+4);
  constexpr uint32_t length_mask=0x03fffffcu; // 24-bit size in DWORDs at bits 2..25
  const uint32_t address=tagged&~3u,bytes=source_size&length_mask,length=bound_size&length_mask;
  if((tagged&3u)!=3 || !address || length>bytes || !length || !std::to_integer<uint8_t>(stride) ||
     (source_size&~length_mask)!=(bound_size&~length_mask) ||
     uint64_t(address)+bytes>(uint64_t{1}<<32))return {};
  const auto offset=bytes-length;
  const auto effective=tagged+offset;
  // PPC srwi/addi/rlwinm (mask bit 19)/clrlwi at 8287E688..E6A8.
  const uint32_t expected=(effective&0x1fffffffu)+(((effective>>20)+512u)&0x1000u);
  if(physical!=expected)return {};
  return UsaVertexStreamBinding{object,address,bytes,offset,length,
                                uint32_t(std::to_integer<uint8_t>(stride))*4u};
}
void OriginalGuestBufferSnapshots::ReclaimExpiredVersions() noexcept {
  auto end=std::remove_if(retired_.begin(),retired_.end(),[this](const auto& entry){
    if(!entry.first.expired())return false;
    retained_bytes_-=entry.second;
    return true;
  });
  retired_.erase(end,retired_.end());
}
std::shared_ptr<const OriginalGuestBufferBytes>
OriginalGuestBufferSnapshots::Capture(
    uint32_t object,uint32_t address,uint32_t bytes,
    ReadGuestBytes reader,void* reader_state) {
  if(!object || !address || !reader || bytes<6 || bytes>(8u<<20) ||
     bytes>budget_ || uint64_t(address)+bytes>uint64_t(UINT32_MAX)+1u)
    return {};
  const auto key=std::make_tuple(object,address,bytes);
  const auto prior=latest_.find(key);
  // A brand new allocation cannot reuse any retained version. Do not
  // allocate scratch or read megabytes from guest memory when there
  // is no budget left to retain this new allocation.
  if(prior==latest_.end() && bytes>budget_-retained_bytes_) {
    ReclaimExpiredVersions();
    if(!ReclaimUnusedSnapshotEntries(latest_,prior,retained_bytes_,budget_,bytes))return {};
  }
  scratch_.resize(bytes);
  // Failed/partial reads cannot fall back to an older cached revision.
  if(!reader(reader_state,address,scratch_.data(),bytes))return {};
  if(prior!=latest_.end() && prior->second->bytes==scratch_)
    return prior->second;
  // Charge only LIVE immutable versions. A dynamic Xbox vertex/index
  // allocation can be rewritten hundreds of times during one Present.
  // Once its preceding version is no longer referenced by ANY captured
  // Draw (the cache is its only owner), replacing that version must reclaim
  // its budget. Otherwise after only 64 MiB of cumulative rewrites every
  // later genuine Draw loses its original buffer bytes, despite memory
  // actually having been released on insert_or_assign.
  //
  // A version held by an earlier Draw is NEVER reclaimed: both generations
  // must remain alive and count toward the fixed per-Present memory budget.
  const size_t recyclable =
      prior!=latest_.end() && prior->second.use_count()==1
          ? prior->second->bytes.size() : 0u;
  if(recyclable>retained_bytes_)return {};
  if(bytes>budget_-(retained_bytes_-recyclable)) {
    ReclaimExpiredVersions();
    if(!ReclaimUnusedSnapshotEntries(latest_,prior,retained_bytes_,budget_,bytes,recyclable))return {};
  }
  // Complete the new snapshot before replacing a cached version. Allocation
  // failure must preserve the previous valid revision and budget unchanged.
  auto next=std::make_shared<OriginalGuestBufferBytes>();
  next->guest_object=object;
  next->guest_address=address;
  next->guest_size=bytes;
  // Transfer the completed byte-exact read rather than copy the entire
  // original allocation a second time. Repeated unchanged reads still
  // reuse scratch; a failed read never publishes or mutates a revision.
  next->bytes.swap(scratch_);
  if(prior!=latest_.end()) {
    if(!recyclable)retired_.emplace_back(prior->second,prior->second->bytes.size());
    prior->second=next;
  } else latest_.emplace(key,next);
  retained_bytes_=retained_bytes_-recyclable+bytes;
  return next;
}
void OriginalGuestBufferSnapshots::Reset() noexcept {
  latest_.clear();
  retired_.clear();
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
  // The color pointer is not an input to a depth/stencil-only clear.
  // Never reject that operation based on an unused r6 value.
  if((out.flags&0x0fu) && gpr[3]) {
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
// Verified USA Resolve 0x828897F8: original AOT host 0x141D58D33..0x141D58D78
// reads two BE signed destination POINT words at r7; host
// 0x141D594B4..0x141D59575 reads four BE RECT words at r5.
// Snapshot the arguments before the guest call can mutate their storage.
std::optional<OriginalResolveRegion> CaptureOriginalColorResolveRegion(
    const std::array<uint32_t,8>& gpr,ReadGuestBytes read,void* state) noexcept {
  if(!read || gpr[1] || !gpr[3] || gpr[5] || gpr[6])return {};
  OriginalResolveRegion region{};
  auto read_words=[&](uint32_t address,auto& words) {
    if(!address)return true;
    if(address&3u || uint64_t(address)+words.size()*4u>(uint64_t{1}<<32))return false;
    std::array<std::byte,16> raw{};
    if(!read(state,address,raw.data(),words.size()*4u))return false;
    for(size_t i=0;i<words.size();++i)words[i]=std::bit_cast<int32_t>(SourceWord(raw.data()+i*4));
    return true;
  };
  if(!read_words(gpr[2],region.source) || !read_words(gpr[4],region.destination))return {};
  return region;
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
std::optional<OriginalInlineGlyphBatch> CaptureUsaInlineGlyphs(
    const RawDraw& call,uint32_t wrapper,uint32_t sp,ReadGuestBytes read,
    void* state,OriginalGuestBufferSnapshots& buffers) {
  const auto& parent=call.original_engine_shader_method;
  const auto& gpu=call.observed_vs_matrices;
  if (!read || call.kind!=Kind::EndVertices || call.function!=0x8288D8F8u ||
      call.lr!=0x82676344u || !wrapper || !sp || !parent || !gpu ||
      !parent->original_shader_method_vtable_verified || !parent->nesting_depth ||
      parent->original_shader_method_guest!=0x8266A0C0u ||
      parent->original_shader_vtable_guest!=0x8206ED6Cu ||
      !parent->original_shader_instance_guest || !call.gpr[0] ||
      gpu->original_guest_device!=call.gpr[0]) return {};
  const auto word=[&](uint64_t at)->uint32_t {
    std::array<std::byte,4> raw{};
    if (at>UINT32_MAX-3u || !read(state,uint32_t(at),raw.data(),4)) return 0;
    uint32_t v=0;
    for (auto b:raw) v=(v<<8)|std::to_integer<uint8_t>(b);
    return v;
  };
  const uint64_t engine=wrapper, device=call.gpr[0], stack=sp;
  const uint64_t decl_ref=uint64_t(parent->original_shader_instance_guest)+200u;
  // The wrapper at 82676328 saves its caller LR at old SP-8 then
  // allocates 96 bytes. The font emitter keeps the glyph count at old SP+80.
  if (word(stack)!=stack+96u || word(stack+88u)!=0x8266A0B8u ||
      word(engine)!=device || word(engine+12u)!=decl_ref ||
      word(engine+28u)!=13u || word(engine+36u)!=24u ||
      word(decl_ref)!=gpu->original_vertex_declaration_guest_object ||
      !gpu->original_vertex_declaration_guest_object) return {};
  const uint32_t glyphs=word(stack+96u+80u);
  if (!glyphs || glyphs>65535u/4 || word(engine+32u)!=glyphs*4u) return {};
  // In USA BeginVertices, the returned r3 is written to device+13448
  // and the allocation's dword count to +13456. These are the bytes
  // written by the original memcpy, observed before the guest commits.
  const uint32_t payload=word(device+13448u);
  if (!payload || word(device+13456u)!=glyphs*24u) return {};
  const uint32_t decl=gpu->original_vertex_declaration_guest_object;
  const auto declaration=ReadUsaVertexDeclaration(decl,read,state);
  if(!declaration || !declaration->font_main_elements)return {};
  auto bytes=buffers.Capture(wrapper,payload,glyphs*96u,read,state);
  if (!bytes) return {};
  return OriginalInlineGlyphBatch{uint32_t(decl),glyphs*4u,std::move(bytes)};
}

std::optional<std::array<uint32_t,6>> CaptureUsaFontFetch(
    uint32_t device,ReadGuestBytes read,void* state) {
  // USA EoR 82675EA0 writes clamp bits in device+(slot+48)*24;
  // 82675E00 writes the mip filter in word 3 of that same descriptor.
  constexpr uint32_t offset=48u*24u;
  std::array<std::byte,24> bytes{};
  if(!device || !read || uint64_t(device)+offset+bytes.size()>uint64_t(UINT32_MAX)+1 ||
     !read(state,device+offset,bytes.data(),bytes.size()))return {};
  std::array<uint32_t,6> words{};
  for(size_t i=0;i<bytes.size();++i)
    words[i/4]=(words[i/4]<<8)|std::to_integer<uint8_t>(bytes[i]);
  if((words[0]&3u)!=2u || !(words[1]>>12))return {};
  return words;
}

std::optional<VerticesXdkCall> DecodeOriginalFontVertices(const RawDraw& call) {
  if (call.kind!=Kind::EndVertices) return DecodeOriginalVerticesXdkCall(call);
  const auto& batch=call.inline_glyphs;
  if (call.function!=0x8288D8F8u || call.lr!=0x82676344u || !call.gpr[0] ||
      !batch || !batch->payload || !batch->declaration ||
      batch->vertex_count<4 || batch->vertex_count>65535 || batch->vertex_count%4 ||
      batch->payload->guest_size!=24u*batch->vertex_count ||
      batch->payload->bytes.size()!=batch->payload->guest_size) return {};
  return VerticesXdkCall{call.function,call.lr,call.gpr[0],13u,0,batch->vertex_count};
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
std::shared_ptr<const Frame> CompletedFrameMailbox::ClaimCleanIfSequence(
    uint64_t source_sequence) noexcept {
  if(!source_sequence || !pending_ || pending_->sequence!=source_sequence ||
     missed_since_take_)return {};
  std::shared_ptr<const Frame> claimed=std::move(pending_);
  missed_since_take_=0;
  return claimed;
}
std::shared_ptr<const Frame>
CompletedFrameMailbox::ClaimLatestIndependentIfSequence(
    uint64_t source_sequence) noexcept {
  if(!source_sequence || !pending_ || pending_->sequence!=source_sequence ||
     pending_->dropped || pending_->dropped_commands)return {};
  std::shared_ptr<const Frame> claimed=std::move(pending_);
  missed_since_take_=0;
  return claimed;
}
bool CompletedFrameMailbox::DiscardIfSequence(
    uint64_t source_sequence) noexcept {
  if(!source_sequence || !pending_ ||
     pending_->sequence!=source_sequence)return false;
  // Preview owners keep the immutable snapshot alive; clearing ONLY the
  // mailbox permits a new source Present to recover after an overwritten
  // frame without ever admitting a stale frame for Vulkan submission.
  pending_.reset();
  missed_since_take_=0;
  return true;
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
         kind==CommandKind::DrawVerticesUP || kind==CommandKind::EndVertices;
}
std::optional<CommandKind> ConvertDraw(Kind kind) noexcept {
  switch(kind) {
    case Kind::DrawVertices:return CommandKind::DrawVertices;
    case Kind::DrawIndexedVertices:return CommandKind::DrawIndexedVertices;
    case Kind::EndVertices:return CommandKind::EndVertices;
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
void FrameCapture::Record(RawDraw call) {
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
  calls_.push_back(std::move(call));
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
      // Original engine ShaderClass -> XDK Draw identity is retained
      // only if the exact source method/vtable pair, original guest this
      // and nesting depth are internally valid. A copied unverified
      // engine attribution must not enter Vulkan's source timeline.
      if(!ValidOriginalEorSourceRenderItem(original) ||
         !ValidOriginalDecalGeometryBinding(original) ||
         !ValidOriginalDecalItemMaterialBinding(original) ||
         !ValidOriginalShaderPassStageBinding(original) ||
         !ValidOriginalShaderPassSamplerStateAtDraw(original))
        return false;
      if(const auto& shader=original.original_engine_shader_method;shader) {
        if(!shader->original_shader_method_vtable_verified ||
           !shader->original_shader_instance_guest ||
           !shader->original_shader_vtable_guest ||
           !ProvenUsaEorShaderMethodVtablePair(
               shader->original_shader_method_guest,
               shader->original_shader_vtable_guest) ||
           !shader->nesting_depth ||
           shader->nesting_depth>16 ||
           !ProvenUsaEorShaderMethodVtablePair(
               shader->original_shader_method_guest,
               shader->original_shader_vtable_guest))
          return false;
      }
      // Agreement between two observer copies is not XDK provenance.
      // Validate the verified USA 6534 function, PPC caller LR, device,
      // count and bounds for the SAME original Draw. UP has no proven
      // engine-level geometry/vertex pointer ownership and stays observe-only.
      if((original.kind==Kind::DrawIndexedVertices &&
          !DecodeOriginalIndexedXdkCall(original)) ||
         (original.kind==Kind::DrawVertices &&
          !DecodeOriginalVerticesXdkCall(original)) ||
         (original.kind==Kind::EndVertices && !DecodeOriginalFontVertices(original)) ||
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
  // Prime the next collector with the actual completed frame size, not
  // the maximum budget. Avoid geometric relocations of large RawDraws
  // every Present; the published frame keeps its own immutable storage.
  try {
    calls_.reserve(out.calls.size());
    commands_.reserve(out.commands.size());
  } catch(const std::bad_alloc&) {
    // Reserving is optional: preserve the completed frame and let the
    // next Record grow normally if memory pressure prevented the hint.
  }
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
