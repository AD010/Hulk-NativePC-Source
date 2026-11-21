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
      std::vector<uint8_t>(vs.begin(),vs.end())};
  Digest expected_vertex{};expected_vertex.fill(0x11u);
  Digest expected_pixel{};expected_pixel.fill(0x22u);
  assert(MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234004u,Stage::Vertex,expected_vertex));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Pixel,expected_pixel));
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_pixel));
  live.original_object_to_header_abi_verified=false;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex));
  live.original_object_to_header_abi_verified=true;
  live.original_virtual_header_guest_address=0x89001001u;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex));
  live.original_virtual_header_guest_address=0x89001000u;
  live.exact_original_guest_virtual_header[34]^=0x55u;
  assert(!MatchesOriginalLiveXdkShaderSource(
      bank,live,0x81234000u,Stage::Vertex,expected_vertex));
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
