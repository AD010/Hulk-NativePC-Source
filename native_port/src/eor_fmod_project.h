// Native Edge of Reality original FMOD Event metadata reader.
// This validates FEV1 references; it does NOT decode audio or launch FMOD.
#pragma once
#include "eor_archive.h"
#include "eor_fev_automation.h"
#include "eor_fev_voice_schedule.h"
#include "eor_fmod_live_binding.h"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
namespace hulk::eor::fmod {
struct Event {
  uint32_t index_hash = 0;
  std::string identifier;
  size_t embedded_fev_offset = 0;
  size_t embedded_fev_bytes = 0;
};
Event Inspect(std::span<const std::byte> source, uint32_t expected_hash);
Event ReadIndexed(const Index& index, const std::filesystem::path& archive,
                  uint32_t hash, size_t max_bytes = 2u << 20);
// The exact four FEV bank references, including absent optional banks.
// Resource identity is authenticated separately for the selected platform.
struct BankReference {
  std::string stem;
  uint32_t hash=0,flags=0,opaque=0;
  std::optional<Resource> indexed;
};
std::vector<BankReference> ResolveIndexedProjectBanks(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash);
struct CompressedWaveSample {
  std::string name;
  uint32_t frames=0,mode=0,rate=0;
  uint16_t channels=0;
  std::vector<std::byte> payload;
};
// This function routes an explicit FEV bank and sample ordinal through
// the real indexed resource. It does NOT infer an event's sample ordinal,
// decode MPEG/ADPCM, or replace an existing playing guest sound.
std::optional<CompressedWaveSample> ReadProjectWaveSample(const Index& index,
    const std::filesystem::path& project_archive,
    const std::filesystem::path& wave_archive,uint32_t project_hash,
    uint32_t bank_ordinal,uint32_t sample_ordinal);
// Resolve an observed FEV waveform byte offset within its own project.
struct WaveformRoute {
  size_t fev_offset=0;
  uint32_t bank_ordinal=0,weight=0,sample_ordinal=0,duration_ms=0;
  std::string path;
};
std::vector<WaveformRoute> ReadProjectWaveforms(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash);
std::optional<CompressedWaveSample> ReadProjectWaveform(const Index& index,
    const std::filesystem::path& project_archive,
    const std::filesystem::path& wave_archive,uint32_t project_hash,size_t fev_offset);
// Select one explicit original event layer/instance/variant; this is not
// an event scheduler. The caller must supply the actual runtime selection.
std::optional<size_t> ResolveEventWaveform(const Index& index,
    const std::filesystem::path& project_archive,uint32_t project_hash,
    std::string_view group,std::string_view event_name,
    uint32_t layer,uint32_t instance,uint32_t variant);
// Produce DSP blocks from original event data and explicit caller-proven
// associations. Interpolation follows the reconstructed USA evaluator.
std::optional<std::vector<audio::DspParameterBlock>> ResolveEventNamedDsp(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevDspBinding> bindings,
    std::span<const audio::FevParameterSnapshot> snapshots,uint32_t sample_rate);
// Fixed gain/pitch admission from original controls. No live scheduler or
// unsupported control may be silently approximated as a constant.
std::optional<audio::FevGainPitch> ResolveEventFixedGainPitch(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevParameterSnapshot> snapshots);
std::optional<std::vector<audio::FevVoiceBlock>> ResolveEventVoiceSchedule(
    const Index& index,const std::filesystem::path& project_archive,
    uint32_t project_hash,std::string_view group,std::string_view event_name,uint32_t layer,
    std::span<const audio::FevParameterSnapshot> snapshots,bool spatial_ready=false,bool moving_seek_ready=false);
// Own archive bytes for all string_view/span fields of the parsed FEV.
// No copies or moves can detach those views from their backing allocation.
struct LiveFevCatalog {
  struct Project {uint32_t hash=0;audio::FevEventTable table;};
  LiveFevCatalog()=default;
  LiveFevCatalog(const LiveFevCatalog&)=delete;
  LiveFevCatalog& operator=(const LiveFevCatalog&)=delete;
  std::vector<std::byte> archive_bytes;
  std::map<std::string,Project,std::less<>> projects;
};
std::unique_ptr<LiveFevCatalog> LoadLiveFevCatalog(const Index& index,
    const std::filesystem::path& archive);
} // namespace hulk::eor::fmod
