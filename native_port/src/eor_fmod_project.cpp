#include "eor_fmod_project.h"
#include <stdexcept>
#include <utility>
namespace hulk::eor::fmod {
namespace {
uint8_t B(std::span<const std::byte> data, size_t index) {
  return std::to_integer<uint8_t>(data[index]);
}
} // namespace
Event Inspect(std::span<const std::byte> data, uint32_t expected_hash) {
  // Owner's original USA EoR FmodProjects entries start with 0x01,
  // NUL-terminated event name, four format-specific metadata bytes,
  // then a real legacy FMOD Event FEV1 header.
  if (data.size() < 20 || B(data,0) != 1)
    throw std::invalid_argument("Invalid EoR FMOD project envelope");
  size_t end = 1;
  for (; end < data.size() && end <= 128 && B(data,end); ++end) {
    const auto c = B(data,end);
    if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
          (c>='0' && c<='9') || c=='_'))
      throw std::invalid_argument("Unsupported original FMOD event identifier");
  }
  if (end==1 || end>128 || end+9>data.size() || B(data,end)!=0)
    throw std::invalid_argument("Truncated or invalid EoR FMOD event name");
  std::string name;
  name.reserve(end-1);
  for (size_t i=1; i<end; ++i)name.push_back(static_cast<char>(B(data,i)));
  if (ResourceHash(name) != expected_hash)
    throw std::invalid_argument("FMOD event ID disagrees with original Xbox EoR index");
  const size_t fev = end+5;
  if (B(data,fev)!='F' || B(data,fev+1)!='E' ||
      B(data,fev+2)!='V' || B(data,fev+3)!='1')
    throw std::invalid_argument("Embedded FMOD Event FEV1 header missing");
  return {expected_hash, std::move(name), fev, data.size()-fev};
}
Event ReadIndexed(const Index& index,const std::filesystem::path& archive,
                  uint32_t hash,size_t max_bytes) {
  if (index.byte_order()!=ByteOrder::Big)
    throw std::invalid_argument("Original Xbox FMOD project index must be big-endian");
  const auto record=index.FindHash("FmodProjects",hash);
  if (!record || record->reserved || !record->size)
    throw std::invalid_argument("Missing or unsupported indexed FMOD event");
  return Inspect(ReadResource(archive,*record,max_bytes),hash);
}
} // namespace hulk::eor::fmod
