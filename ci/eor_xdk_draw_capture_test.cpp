#include "../native_port/src/eor_xdk_draw_capture.h"
#include <cassert>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace hulk::eor::xdk;
namespace {
struct GuestMemory {
  std::vector<std::byte> bytes=std::vector<std::byte>(0x6000);
};
bool ReadFake(void* state,uint32_t address,std::byte* output,size_t size) noexcept {
  const auto& data=static_cast<GuestMemory*>(state)->bytes;
  if(!output || address>data.size() || size>data.size()-address)return false;
  std::memcpy(output,data.data()+address,size);
  return true;
}
void W(GuestMemory& m,uint32_t address,uint32_t v) {
  auto* p=m.bytes.data()+address;
  for(int i=0;i<4;++i)p[i]=std::byte(uint8_t(v>>(24-8*i)));
}
}
int main() {
  // The original Hulk USA device shadow state has proven BE c7..c25 VS
  // registers and VS/PS guest object slots. Capture at the actual Draw.
  GuestMemory guest;
  constexpr uint32_t device=0x2000;
  W(guest,device+12696,0x82ABCDEF);
  W(guest,device+12692,0x82FEDCBA);
  W(guest,device+11820,0x82345678);
  W(guest,device+12436,0x826789AB);
  W(guest,device+12440,0x82888888);
  W(guest,device+12456,0x82999999);
  W(guest,device+12544,0x8200BEEF);
  W(guest,device+12548,0x8200BABE);
  W(guest,device+1920+11*16,std::bit_cast<uint32_t>(1.5f));
  const auto matrix=CaptureUsaVsMatrices(device,ReadFake,&guest);
  assert(matrix && matrix->original_guest_device==device &&
         matrix->original_vs_guest_object==0x82ABCDEFu &&
         matrix->original_ps_guest_object==0x82FEDCBAu &&
         matrix->original_vertex_declaration_guest_object==0x82345678u &&
         matrix->original_index_buffer_guest_object==0x826789ABu &&
         matrix->original_rt0_guest_object==0x82888888u &&
         matrix->original_depth_guest_object==0x82999999u &&
         matrix->original_texture_guest_objects[0]==0x8200BEEFu &&
         matrix->original_texture_guest_objects[1]==0x8200BABEu);
  const auto view=ReadOriginalMatrix(*matrix,11,4);
  assert(view && view->original_register_count==4 &&
         view->values[0]==1.5f && view->values[15]==0.f);
  const auto world3=ReadOriginalMatrix(*matrix,7,3);
  assert(world3 && world3->original_register_count==3);
  assert(!ReadOriginalMatrix(*matrix,7,5));
  assert(!ReadOriginalMatrix(*matrix,23,4));
  W(guest,device+1920+7*16,
    std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
  const auto nonfinite=CaptureUsaVsMatrices(device,ReadFake,&guest);
  assert(nonfinite && !ReadOriginalMatrix(*nonfinite,7,4) &&
         ReadOriginalMatrix(*nonfinite,11,4));
  assert(!CaptureUsaVsMatrices(0,ReadFake,&guest));
  assert(!CaptureUsaVsMatrices(UINT32_MAX,ReadFake,&guest));
  // The verified USA +1920 VS and +6016 PS banks contain 256 registers
  // EACH. Original pixel-lighting inputs must not be guessed from VS c7..25.
  W(guest,device+1920+255*16,std::bit_cast<uint32_t>(42.0f));
  W(guest,device+6016+0*16,std::bit_cast<uint32_t>(0.125f));
  W(guest,device+6016+127*16,std::bit_cast<uint32_t>(2.5f));
  W(guest,device+6016+255*16,std::bit_cast<uint32_t>(99.0f));
  OriginalShaderRegisters complete;
  assert(CaptureUsaAllShaderRegisters(device,ReadFake,&guest,complete));
  auto last_vs=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Vertex,255);
  auto first_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,0);
  auto middle_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,127);
  auto last_ps=ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,255);
  assert(last_vs && (*last_vs)[0]==42.f);
  assert(first_ps && (*first_ps)[0]==0.125f);
  assert(middle_ps && (*middle_ps)[0]==2.5f);
  assert(last_ps && (*last_ps)[0]==99.f);
  assert(!ReadOriginalShaderFloat4(
      complete,OriginalShaderStage::Pixel,256));
  auto bad_reg=complete;
  bad_reg.finite_pixel[1]=0;
  assert(!ReadOriginalShaderFloat4(
      bad_reg,OriginalShaderStage::Pixel,127));
  W(guest,device+6016+7*16,
    std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
  assert(CaptureUsaAllShaderRegisters(device,ReadFake,&guest,complete));
  assert(!ReadOriginalShaderFloat4(complete,OriginalShaderStage::Pixel,7));
  assert(ReadOriginalShaderFloat4(complete,OriginalShaderStage::Vertex,255));
  assert(!CaptureUsaAllShaderRegisters(UINT32_MAX,ReadFake,&guest,complete));
  assert(!CaptureUsaAllShaderRegisters(0,ReadFake,&guest,complete));

  FrameCapture capture;
  RawDraw a{Kind::DrawIndexedVertices,0x8288E318u,0x8263BF50u,{3,4,5,6,7,8,9,10}};
  RawDraw b{Kind::DrawVertices,0x8288DF30u,0,{4,5,6,7,8,9,10,11}};
  a.observed_vs_matrices=*matrix;
  a.observed_shader_constants=std::make_shared<OriginalShaderRegisters>(complete);
  capture.Record(a);
  capture.Record(b);
  Frame first=capture.Finish();
  assert(first.sequence==1 && first.dropped==0);
  assert(first.calls.size()==2 && first.calls[0].function==0x8288E318u);
  assert(first.calls[1].gpr[7]==11);
  assert(first.calls[0].lr==0x8263BF50u);
  // A Draw-only list cannot become a native sole-presenter frame:
  // original RT/depth, clear and Resolve ownership MUST be ordered too.
  assert(first.commands.size()==2 &&
         first.commands[0].kind==CommandKind::DrawIndexedVertices &&
         first.commands[0].draw_ordinal==0 &&
         first.commands[1].draw_ordinal==1);
  assert(!ValidateOriginalCommandTimeline(first));
  FrameCapture chronology;
  chronology.RecordCommand(RawCommand{
      CommandKind::SetRenderTarget,0x8287F2C0u,0x8263A000u,
      {3,0,0x82001234u,0,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::SetDepthStencilSurface,0x8287F628u,0x8263A004u,
      {3,0x82004321u,0,0,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::ClearF,0x82888C40u,0x8263A008u,
      {3,1,0x82001300u,0x82001400u,0,0,0,0}});
  chronology.Record(a);
  chronology.RecordCommand(RawCommand{
      CommandKind::BeginTiling,0x8288A900u,0x8263A00Cu,
      {3,1,0,0,0,0,0,0}});
  chronology.Record(b);
  chronology.RecordCommand(RawCommand{
      CommandKind::EndTiling,0x8288AD98u,0x8263A010u,
      {3,1,0,0,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::Resolve,0x828897F8u,0x8263A014u,
      {3,1,0,0x82002000u,0,0,0,0}});
  chronology.RecordCommand(RawCommand{
      CommandKind::Present,0x82880778u,0x8263A018u,
      {3,0x82003000u,0,0,0,0,0,0}});
  const Frame full_timeline=chronology.Finish();
  assert(full_timeline.sequence==1 && full_timeline.calls.size()==2 &&
         full_timeline.commands.size()==9 &&
         ValidateOriginalCommandTimeline(full_timeline));
  auto invalid_timeline=full_timeline;
  std::swap(invalid_timeline.commands[3],invalid_timeline.commands[5]);
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands[3].draw_ordinal=7;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands.erase(invalid_timeline.commands.begin()+3);
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.dropped_commands=1;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  invalid_timeline=full_timeline;
  invalid_timeline.commands[8].kind=CommandKind::ClearF;
  assert(!ValidateOriginalCommandTimeline(invalid_timeline));
  bool rejected_unknown_command=false;
  try {
    chronology.RecordCommand(RawCommand{CommandKind::ClearF,0x82888D68u});
  } catch(const std::invalid_argument&) {rejected_unknown_command=true;}
  assert(rejected_unknown_command); // original outer Clear nests ClearF
  assert(first.calls[0].observed_vs_matrices &&
         ReadOriginalMatrix(*first.calls[0].observed_vs_matrices,11,4)
             ->values[0]==1.5f);
  assert(first.calls[0].observed_shader_constants &&
         ReadOriginalShaderFloat4(
             *first.calls[0].observed_shader_constants,
             OriginalShaderStage::Pixel,255)->at(0)==99.0f);
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
  RawDraw vertex{Kind::DrawVertices,0x8288DF30u,0x8263BF50u,
                 {0x40002880u,4u,21u,6u,0u,0u,0u,0u}};
  const auto nonindexed=DecodeOriginalVerticesXdkCall(vertex);
  assert(nonindexed && nonindexed->guest_device==0x40002880u &&
         nonindexed->primitive==4u && nonindexed->start_vertex==21u &&
         nonindexed->vertex_count==6u);
  vertex.kind=Kind::DrawIndexedVertices;
  assert(!DecodeOriginalVerticesXdkCall(vertex));
  vertex.kind=Kind::DrawVertices;
  vertex.gpr[2]=UINT32_MAX;
  assert(!DecodeOriginalVerticesXdkCall(vertex));
  CompletedFrameMailbox mailbox;
  mailbox.Publish(first);
  mailbox.Publish(Frame{42,0,{a}});
  assert(mailbox.TotalOverwritten()==1);
  auto delivered=mailbox.Take();
  assert(delivered && delivered->frame.sequence==42 &&
         delivered->overwritten_since_take==1 &&
         delivered->frame.calls.size()==1);
  assert(!mailbox.Take());
  mailbox.Publish(Frame{43,0,{a,b}});
  delivered=mailbox.Take();
  assert(delivered && delivered->frame.sequence==43 &&
         delivered->overwritten_since_take==0);
  mailbox.Reset();
  assert(mailbox.TotalOverwritten()==0 && !mailbox.Take());
  bool missing_sequence=false;
  try {mailbox.Publish(Frame{});}
  catch(const std::invalid_argument&){missing_sequence=true;}
  assert(missing_sequence);
  mailbox.Publish(Frame{55,0,{a}});
  mailbox.Publish(Frame{56,0,{a,b}});
  mailbox.Publish(Frame{57,0,{a}});
  assert(mailbox.TotalOverwritten()==2);
  auto newest=mailbox.Take();
  assert(newest && newest->frame.sequence==57 &&
         newest->overwritten_since_take==2 &&
         newest->frame.calls.size()==1);
  assert(!mailbox.Take());
  mailbox.Reset();
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
