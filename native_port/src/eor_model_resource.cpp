#include "eor_model_resource.h"
#include "eor_model_binary.h"
#include <stdexcept>
#include <utility>
namespace hulk::eor::model {
namespace {
using binary::B;
using binary::U32;
std::pair<std::string,size_t> AsciiName(std::span<const std::byte> raw,
                                        size_t from, size_t max_length) {
  if (from >= raw.size()) throw std::invalid_argument("Missing EoR string");
  size_t end = from;
  for (; end < raw.size() && end - from <= max_length; ++end) {
    const auto c = B(raw,end);
    if (!c) {
      if (end == from) throw std::invalid_argument("Empty EoR name");
      return {std::string(reinterpret_cast<const char*>(raw.data()+from),
                          end-from), end};
    }
    if (c < 0x20 || c > 0x7e || c == '/' || c == '\\')
      throw std::invalid_argument("EoR name contains invalid byte");
  }
  throw std::invalid_argument("EoR name lacks bounded NUL terminator");
}
} // namespace
Identity Inspect(std::span<const std::byte> raw) {
  if (raw.size() < 16 || raw.size() > (32u << 20))
    throw std::invalid_argument("Invalid original EoR model record length");
  Platform p;
  if (B(raw,0)==1 && B(raw,1)==0) p=Platform::PcD3d9;
  else if (B(raw,0)==0 && B(raw,1)==1) p=Platform::Xbox360;
  else throw std::invalid_argument("Unknown EoR model byte order");
  for (size_t i=2; i<6; ++i)
    if (B(raw,i)!=0) throw std::invalid_argument("Unknown EoR model version/header");
  const auto [name,name_end]=AsciiName(raw,6,128);
  // Verified across ALL 5,997 original Xbox+PC indexed model records:
  // the model name NUL is followed by kind 0 or 3, then endian uint32.
  if (name_end+6 > raw.size() ||
      (B(raw,name_end+1)!=0 && B(raw,name_end+1)!=3))
    throw std::invalid_argument("Malformed EoR model surface directory");
  const auto kind=B(raw,name_end+1);
  const auto count=U32(raw,name_end+2,p);
  if (count>1024) throw std::invalid_argument("Invalid original surface count");
  std::string first;
  size_t offset=name_end+6;
  if (count) {
    const auto [surface,surface_end]=AsciiName(raw,offset,240);
    first=surface;
    offset=surface_end+1;
  }
  return {p,name,kind,count,first,offset};
}
} // namespace hulk::eor::model
