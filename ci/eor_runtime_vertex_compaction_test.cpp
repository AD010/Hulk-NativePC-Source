#include "../native_port/src/eor_runtime_vertex_compaction.h"
#include <cstdlib>
#include <iostream>
using namespace hulk::eor::gpu::live;
static void Check(bool ok){if(!ok)std::abort();}
int main(){
  std::vector<uint32_t> indices{10000000,2,10000000,9,2,9};
  const auto old=indices;
  auto owners=CompactRuntimeIndices(indices);
  Check(owners==std::vector<uint32_t>({2,9,10000000}));
  for(size_t i=0;i<indices.size();++i)Check(owners.at(indices[i])==old[i]);
  std::vector<std::byte> bytes(400);
  for(size_t i=0;i<bytes.size();++i)bytes[i]=std::byte(i%251);
  auto gather=GatherRuntimeVertexBytes(bytes,16,320,16,std::array<uint32_t,3>{0,7,19});
  Check(gather.size()==48);
  for(size_t i=0;i<3;++i)for(size_t j=0;j<16;++j)
    Check(gather[i*16+j]==bytes[16+std::array<uint32_t,3>{0,7,19}[i]*16+j]);
  auto contiguous=GatherRuntimeVertexBytes(bytes,16,320,16,std::array<uint32_t,3>{3,4,5});
  Check(std::equal(contiguous.begin(),contiguous.end(),bytes.begin()+64));
  bool rejected=false;
  try{(void)GatherRuntimeVertexBytes(bytes,16,320,16,std::array<uint32_t,1>{20});}
  catch(const std::invalid_argument&){rejected=true;}
  Check(rejected);
  for(int v=-128;v<=127;++v){
    const float x=std::max(-1.f,float(v)/127.f);
    const auto bits=RuntimeFloatAttributeBits({x,-x,0.f,1.f});
    Check(std::bit_cast<float>(bits[0])==x && std::bit_cast<float>(bits[1])==-x);
    Check(bits[2]==0u && bits[3]==0x3f800000u);
  }
  uint32_t rng=12345;
  for(unsigned trial=0;trial<10000;++trial){
    std::vector<uint32_t> seq(3u+trial%127u);
    for(auto& i:seq){rng=rng*1664525u+1013904223u;i=(rng>>8)%50000u;}
    const auto original=seq;
    auto compact=CompactRuntimeIndices(seq);
    Check(compact.size()<=seq.size());
    for(size_t i=0;i<seq.size();++i)Check(compact.at(seq[i])==original[i]);
  }
  std::cout<<"Runtime vertex ownership, sparse compaction and normal/tangent bits PASS\n";
}
