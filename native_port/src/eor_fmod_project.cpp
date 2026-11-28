#include "eor_fmod_project.h"
#include "eor_fsb3_bank.h"
#include "eor_fev_waveform.h"
#include "eor_fev_event_table.h"
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
std::vector<WaveformRoute> ReadProjectWaveforms(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))
    throw std::invalid_argument("Original FMOD project archive/index mismatch");
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record || record->reserved || !record->size)
    throw std::invalid_argument("Original FMOD project absent");
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto event=Inspect(bytes,project_hash);
  const auto waves=audio::InspectFevWaveforms(
      audio::AudioBytes(bytes).subspan(event.embedded_fev_offset),event.identifier);
  if(!waves)throw std::invalid_argument("Original FEV waveform records invalid");
  std::vector<WaveformRoute> result;
  result.reserve(waves->size());
  for(const auto& wave:*waves)
    result.push_back({wave.offset,wave.bank,wave.weight,wave.sample_ordinal,wave.duration_ms,std::string(wave.path)});
  return result;
}
std::optional<CompressedWaveSample> ReadProjectWaveform(const Index& index,
    const std::filesystem::path& project_archive,const std::filesystem::path& wave_archive,
    uint32_t project_hash,size_t fev_offset) {
  const auto waves=ReadProjectWaveforms(index,project_archive,project_hash);
  const auto wave=std::find_if(waves.begin(),waves.end(),[&](const auto& w){return w.fev_offset==fev_offset;});
  if(wave==waves.end())return {};
  const auto refs=ResolveIndexedProjectBanks(index,project_archive,project_hash);
  if(wave->bank_ordinal>=refs.size() || !refs[wave->bank_ordinal].indexed ||
     !MatchesIndexedArchive(index,"WaveBanks",wave_archive))return {};
  const auto& ref=refs[wave->bank_ordinal];
  const auto bytes=ReadResource(wave_archive,*ref.indexed,64u<<20);
  const auto bank=audio::InspectOriginalWaveBank(bytes,ref.hash,index.byte_order()==ByteOrder::Big);
  if(!bank || bank->stem!=ref.stem)return {};
  const audio::FevWaveformRef ref_record{wave->fev_offset,wave->bank_ordinal,
      wave->weight,wave->sample_ordinal,wave->duration_ms,wave->path};
  const auto ordinal=audio::ResolveFevWaveformSample(*bank,ref_record);
  if(!ordinal)return {};
  const auto& sample=bank->samples[*ordinal];
  return CompressedWaveSample{std::string(sample.name),sample.frames,sample.mode,
      sample.rate,sample.channels,{sample.payload.begin(),sample.payload.end()}};
}
std::optional<size_t> ResolveEventWaveform(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash,
    std::string_view group,std::string_view event_name,
    uint32_t layer,uint32_t instance,uint32_t variant) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))return {};
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record || record->reserved || !record->size)return {};
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto project=Inspect(bytes,project_hash);
  const auto table=audio::InspectFevEventTable(
      audio::AudioBytes(bytes).subspan(project.embedded_fev_offset),project.identifier);
  if(!table)return {};
  const audio::FevEventEntry* match=nullptr;
  for(const auto& event:table->events)if(event.group==group&&event.name==event_name) {
    if(match)return {};
    match=&event;
  }
  if(!match||layer>=match->layers.size()||instance>=match->layers[layer].size())return {};
  const auto& definition=table->definitions[match->layers[layer][instance]];
  if(variant>=definition.variants.size())return {};
  return definition.variants[variant];
}
std::optional<std::vector<audio::DspParameterBlock>> ResolveEventNamedDsp(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevDspBinding> bindings,
    std::span<const audio::FevParameterSnapshot> snapshots,uint32_t sample_rate) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))return {};
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record || record->reserved || !record->size)return {};
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto project=Inspect(bytes,project_hash);
  const auto table=audio::InspectFevEventTable(
      audio::AudioBytes(bytes).subspan(project.embedded_fev_offset),project.identifier);
  if(!table)return {};
  const audio::FevEventEntry* match=nullptr;
  for(const auto& event:table->events)if(event.group==group&&event.name==event_name) {
    if(match)return {};
    match=&event;
  }
  return match?audio::BuildFevNamedDspBlocks(*match,layer,bindings,snapshots,sample_rate):std::nullopt;
}
std::optional<audio::FevGainPitch> ResolveEventFixedGainPitch(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevParameterSnapshot> snapshots) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))return {};
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record || record->reserved || !record->size)return {};
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto project=Inspect(bytes,project_hash);
  const auto table=audio::InspectFevEventTable(
      audio::AudioBytes(bytes).subspan(project.embedded_fev_offset),project.identifier);
  if(!table)return {};
  const audio::FevEventEntry* match=nullptr;
  for(const auto& event:table->events)if(event.group==group&&event.name==event_name) {
    if(match)return {};
    match=&event;
  }
  return match?audio::ResolveFevFixedGainPitch(*match,layer,snapshots):std::nullopt;
}
std::optional<std::vector<audio::FevVoiceBlock>> ResolveEventVoiceSchedule(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevParameterSnapshot> snapshots) {
  if(!MatchesIndexedArchive(index,"FmodProjects",project_archive))return {};
  const auto record=index.FindHash("FmodProjects",project_hash);
  if(!record||record->reserved||!record->size)return {};
  const auto bytes=ReadResource(project_archive,*record,2u<<20);
  const auto project=Inspect(bytes,project_hash);
  const auto table=audio::InspectFevEventTable(
      audio::AudioBytes(bytes).subspan(project.embedded_fev_offset),project.identifier);
  if(!table)return {};
  const audio::FevEventEntry* match=nullptr;
  for(const auto& event:table->events)if(event.group==group&&event.name==event_name){
    if(match)return {};
    match=&event;
  }
  return match?audio::BuildFevVoiceSchedule(*match,layer,snapshots):std::nullopt;
}
} // namespace hulk::eor::fmod
