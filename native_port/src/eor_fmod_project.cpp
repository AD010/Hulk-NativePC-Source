#include "eor_fmod_project.h"
#include "eor_fsb3_bank.h"
#include <algorithm>
#include <array>
#include <fstream>
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
namespace {
bool MatchesIndexedArchive(const Index& index,std::string_view group,
                           const std::filesystem::path& path) {
  const auto g=index.groups().find(std::string(group));
  if(g==index.groups().end() || g->second.empty())return false;
  uint64_t end=0;
  for(const auto& record:g->second) {
    if(record.reserved || record.offset<4u || !record.size)return false;
    end=(std::max)(end,uint64_t(record.offset)+record.size);
  }
  if(end>UINT32_MAX || std::filesystem::file_size(path)<end)return false;
  std::ifstream file(path,std::ios::binary);
  std::array<std::byte,4> header{};
  return bool(file.read(reinterpret_cast<char*>(header.data()),header.size())) &&
      audio::AudioU32(header,0)==end;
}
}
std::vector<BankReference> ResolveIndexedProjectBanks(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))
    throw std::invalid_argument("Original FMOD project archive/index mismatch");
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record || record->reserved || !record->size)
    throw std::invalid_argument("Original FMOD project absent");
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto event=Inspect(bytes,project_hash);
  const auto refs=audio::InspectOriginalFevBanks(
      audio::AudioBytes(bytes).subspan(event.embedded_fev_offset),event.identifier);
  if(!refs)throw std::invalid_argument("Original FEV bank directory invalid");
  std::vector<BankReference> result;
  for(const auto& ref:*refs) {
    auto indexed=index.FindHash("WaveBanks",ref.hash);
    if(indexed && (indexed->reserved || !indexed->size))
      throw std::invalid_argument("Invalid indexed WaveBank record");
    result.push_back({std::string(ref.stem),ref.hash,ref.flags,ref.opaque,indexed});
  }
  return result;
}
std::optional<CompressedWaveSample> ReadProjectWaveSample(const Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,uint32_t bank_ordinal,uint32_t sample_ordinal) {
  const auto refs=ResolveIndexedProjectBanks(index,project_archive,project_hash);
  if(bank_ordinal>=refs.size() || !refs[bank_ordinal].indexed ||
     !MatchesIndexedArchive(index,"WaveBanks",wave_archive))return {};
  const auto& ref=refs[bank_ordinal];
  const auto bytes=ReadResource(wave_archive,*ref.indexed,64u<<20);
  const auto bank=audio::InspectOriginalWaveBank(bytes,ref.hash,index.byte_order()==ByteOrder::Big);
  if(!bank || bank->stem!=ref.stem || sample_ordinal>=bank->samples.size())return {};
  const auto& sample=bank->samples[sample_ordinal];
  return CompressedWaveSample{std::string(sample.name),sample.frames,sample.mode,
      sample.rate,sample.channels,{sample.payload.begin(),sample.payload.end()}};
}
} // namespace hulk::eor::fmod
