#include "../native_port/src/eor_fmod_project.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
using namespace hulk::eor::fmod;
template<class Fn> void MustThrow(Fn f) {
  bool rejected=false;
  try {f();} catch(const std::invalid_argument&) {rejected=true;}
  assert(rejected);
}
int main() {
  // Synthetic-only 2008 EoR FMOD event, no original event names/FEV bytes.
  const std::string name="synthetic_audio_event_01";
  const uint32_t hash=hulk::eor::ResourceHash(name);
  std::vector<std::byte> data;
  data.push_back(std::byte{1});
  for(char c:name)data.push_back(std::byte{static_cast<unsigned char>(c)});
  data.push_back(std::byte{0});
  for(unsigned i=0;i<4;++i)data.push_back(std::byte{0});
  for(char c:std::string("FEV1synthetic"))data.push_back(std::byte{static_cast<unsigned char>(c)});
  auto record=Inspect(data,hash);
  assert(record.identifier==name && record.index_hash==hash);
  assert(record.embedded_fev_bytes==13);
  MustThrow([&]{(void)Inspect(data,hash+1);});
  data[record.embedded_fev_offset]=std::byte{0};
  MustThrow([&]{(void)Inspect(data,hash);});
  data[record.embedded_fev_offset]=std::byte{'F'};
  data[0]=std::byte{2};
  MustThrow([&]{(void)Inspect(data,hash);});
}
