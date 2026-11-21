#include "../native_port/src/eor_original_virtual_shader_identity.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>

using namespace hulk::eor::aot;
void Le32(std::vector<uint8_t>& out,uint32_t value) {
  for(unsigned i=0;i<4;++i)out.push_back(uint8_t(value>>(8*i)));
}
std::array<uint8_t,36> SourceHeader(Stage stage) {
  std::array<uint8_t,36> out{};
  out[0]=0x10;out[1]=0x2a;out[2]=0x11;
  out[3]=stage==Stage::Vertex?1u:0u;
  out[7]=36; // Original ShaderContainer virtual byte size BE32
  out[11]=4; // Source physical microcode bytes (not part of this digest)
  return out;
}
std::vector<uint8_t> PrivateIndex() {
  std::vector<uint8_t> out{'H','E','O','R','V','R','T','1'};
  Le32(out,2);Le32(out,1);
  for(const auto stage:{Stage::Vertex,Stage::Pixel}) {
    const auto header=SourceHeader(stage);
    const auto digest=OriginalShaderVirtualSha256(header);
    out.insert(out.end(),digest.begin(),digest.end());
    out.insert(out.end(),32,stage==Stage::Vertex?0x11u:0x22u);
    Le32(out,static_cast<uint32_t>(stage));
    Le32(out,36u);
  }
  return out;
}
void Write(const std::filesystem::path& p,const std::vector<uint8_t>& blob) {
  std::ofstream stream(p,std::ios::binary|std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(blob.data()),
               static_cast<std::streamsize>(blob.size()));
}
void Reject(const std::function<void()>& run) {
  bool failed=false;
  try {run();}
  catch(const std::invalid_argument&) {failed=true;}
  assert(failed);
}
int main() {
  const std::array<uint8_t,3> abc{'a','b','c'};
  constexpr Digest abc_sha{
      0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
      0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
      0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
      0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  assert(OriginalShaderVirtualSha256(abc)==abc_sha);
  // Independent Python hashlib reference SHA256 for a 36-byte Xenos
  // virtual stage header; no source program was embedded in the test.
  const auto vs=SourceHeader(Stage::Vertex),ps=SourceHeader(Stage::Pixel);
  const Digest vs_hash{
      0xfd,0x4d,0xc1,0x43,0xd3,0x7a,0x6c,0xbc,
      0x31,0x89,0x24,0x79,0x3a,0x82,0x84,0x1c,
      0xe6,0x55,0x7b,0x3b,0xd8,0x66,0x36,0x77,
      0xa9,0x11,0x7d,0x3f,0xad,0x7d,0x2d,0x3d};
  assert(OriginalShaderVirtualSha256(vs)==vs_hash);
  const auto p=std::filesystem::temp_directory_path()/
      "hulk-original-virtual-shader-synthetic.bin";
  auto blob=PrivateIndex();
  Write(p,blob);
  OriginalVirtualShaderIdentity bank;bank.Load(p);
  assert(bank.size()==2);
  auto vertex=bank.FindOriginalSource(vs,Stage::Vertex);
  auto pixel=bank.FindOriginalSource(ps,Stage::Pixel);
  assert(vertex && (*vertex)[0]==0x11u);
  assert(pixel && (*pixel)[0]==0x22u);
  assert(!bank.FindOriginalSource(vs,Stage::Pixel));
  assert(!bank.FindOriginalSource(ps,Stage::Vertex));
  auto corrupt=vs;
  corrupt[34]=0x55;
  assert(!bank.FindOriginalSource(corrupt,Stage::Vertex));
  corrupt=vs;corrupt[7]=35;
  assert(!bank.FindOriginalSource(corrupt,Stage::Vertex));
  assert(!bank.FindOriginalSource(
      std::span<const uint8_t>(vs.data(),35),Stage::Vertex));
  // Runtime proof must match the SAME live XDK object, authentic guest
  // header, stage AND original full source SHA. No class-name shortcut.
  OriginalLiveXdkShaderHeaderWitness live{
      0x81234000u,0x89001000u,true,
      std::vector<uint8_t>(vs.begin(),vs.end()),17u,3u};
  Digest expected_vertex{};expected_vertex.fill(0x11u);
  Digest expected_pixel{};expected_pixel.fill(0x22u);
  assert(MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,17u,3u));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234004u,Stage::Vertex,expected_vertex,17u,3u));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Pixel,expected_pixel,17u,3u));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_pixel,17u,3u));
  // Reused object at another frame or ordinal cannot borrow a stale SHA.
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,18u,3u));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,17u,4u));
  live.original_object_to_header_abi_verified=false;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,17u,3u));
  live.original_object_to_header_abi_verified=true;
  live.original_virtual_header_guest_address=0x89001001u;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,17u,3u));
  live.original_virtual_header_guest_address=0x89001000u;
  live.exact_original_guest_virtual_header[34]^=0x55u;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex,17u,3u));
  // Source digests are recovered from live VS/PS header bytes and their
  // original guest-object identities, then mapped back to the unique
  // ShaderEffects effect/pass without trusting an asserted class name.
  live.exact_original_guest_virtual_header.assign(vs.begin(),vs.end());
  const auto resolved_vs=ResolveOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,17u,3u);
  assert(resolved_vs && *resolved_vs==expected_vertex);
  OriginalLiveXdkShaderHeaderWitness pixel_live{
      0x81235000u,0x89002000u,true,
      std::vector<uint8_t>(ps.begin(),ps.end()),17u,3u};
  const auto resolved_ps=ResolveOriginalLiveXdkShaderSource(
      bank,pixel_live,0x81235000u,Stage::Pixel,17u,3u);
  assert(resolved_ps && *resolved_ps==expected_pixel);
  std::vector<uint8_t> effect_blob{'H','E','O','R','M','A','T','2'};
  Le32(effect_blob,1u);Le32(effect_blob,2u);
  Le32(effect_blob,0x1234u);Le32(effect_blob,0u);
  effect_blob.insert(effect_blob.end(),expected_vertex.begin(),expected_vertex.end());
  effect_blob.insert(effect_blob.end(),expected_pixel.begin(),expected_pixel.end());
  const auto effect_file=std::filesystem::temp_directory_path()/
      "hulk-original-live-shader-effect-synthetic.bin";
  Write(effect_file,effect_blob);
  EffectCatalog effects;effects.Load(effect_file);
  auto live_pass=ResolveOriginalLiveEorEffectPass(
      bank,effects,live,pixel_live,0x81234000u,0x81235000u,17u,3u);
  assert(live_pass && live_pass->effect_hash==0x1234u &&
         live_pass->pass_index==0u);
  assert(!ResolveOriginalLiveEorEffectPass(
      bank,effects,live,pixel_live,0x81234000u,0x81235004u,17u,3u));
  assert(!ResolveOriginalLiveEorEffectPass(
      bank,effects,live,pixel_live,0x81234000u,0x81235000u,17u,4u));
  auto wrong_pixel=pixel_live;
  wrong_pixel.exact_original_guest_virtual_header[25]^=0x40u;
  assert(!ResolveOriginalLiveEorEffectPass(
      bank,effects,live,wrong_pixel,0x81234000u,0x81235000u,17u,3u));
  std::filesystem::remove(effect_file);
  // A real producer may read the second 32-bit word of the original
  // EoR stage row, but it must not trust it until the complete bytes match
  // the private ShaderContainer index AND the current shader object.
  std::vector<uint8_t> guest(0x4000u,0);
  const auto put_be32=[&](uint32_t where,uint32_t value) {
    assert(uint64_t(where)+4u<=guest.size());
    for(unsigned i=0;i<4u;++i)
      guest[where+i]=uint8_t(value>>(8u*(3u-i)));
  };
  const auto read_guest=[](void* user,uint32_t where,
                           std::byte* output,size_t length)->bool {
    const auto& bytes=*static_cast<const std::vector<uint8_t>*>(user);
    if(!output || uint64_t(where)+length>bytes.size())return false;
    for(size_t i=0;i<length;++i)
      output[i]=std::byte(bytes[where+i]);
    return true;
  };
  put_be32(0x1000u,0x81234000u);
  put_be32(0x1004u,0x2000u);
  for(size_t i=0;i<vs.size();++i)guest[0x2000u+i]=vs[i];
  auto from_row=ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234000u,Stage::Vertex,
      read_guest,&guest,17u,3u);
  assert(from_row);
  assert(from_row->original_object_to_header_abi_verified);
  assert(from_row->exact_original_guest_virtual_header.size()==vs.size());
  assert(from_row->original_frame_sequence==17u &&
         from_row->original_call_ordinal==3u);
  assert(MatchesOriginalLiveXdkShaderSource(
      bank,*from_row,0x81234000u,Stage::Vertex,
      expected_vertex,17u,3u));
  assert(!ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234004u,Stage::Vertex,
      read_guest,&guest,17u,3u));
  assert(!ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234000u,Stage::Pixel,
      read_guest,&guest,17u,3u));
  guest[0x2000u+25]^=0x20u;
  assert(!ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234000u,Stage::Vertex,
      read_guest,&guest,17u,3u));
  guest[0x2000u+25]^=0x20u;
  put_be32(0x1004u,0x2001u);
  assert(!ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234000u,Stage::Vertex,
      read_guest,&guest,17u,3u));
  put_be32(0x1004u,0x2000u);
  // Bounded read: even valid magic with a forged oversized header is
  // rejected before any heap allocation or guest memory traversal.
  put_be32(0x2004u,0x01000000u);
  assert(!ObserveOriginalLiveXdkShaderHandleEntry(
      bank,0x1000u,0x81234000u,Stage::Vertex,
      read_guest,&guest,17u,3u));
  auto original=blob;
  blob[0]='X';Write(p,blob);
  Reject([&] {bank.Load(p);}); assert(bank.size()==2);
  blob=original;blob.pop_back();Write(p,blob);
  Reject([&] {bank.Load(p);});
  blob=original;
  // Second virtual hash and first hash duplicate -> ambiguous source owner.
  for(size_t i=0;i<32;++i)blob[16+72+i]=blob[16+i];
  Write(p,blob);Reject([&] {bank.Load(p);});
  blob=original;
  // Different virtual identity cannot point to same original full SHA.
  for(size_t i=0;i<32;++i)blob[16+72+32+i]=blob[16+32+i];
  Write(p,blob);Reject([&] {bank.Load(p);});
  blob=original;
  blob[16+64]=3; // unknown original stage
  Write(p,blob);Reject([&] {bank.Load(p);});
  assert(bank.FindOriginalSource(vs,Stage::Vertex));
  bank.Clear();assert(bank.size()==0 &&
                      !bank.FindOriginalSource(vs,Stage::Vertex));
  std::filesystem::remove(p);
}
