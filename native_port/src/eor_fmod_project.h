// Native Edge of Reality original FMOD Event metadata reader.
// This validates FEV1 references; it does NOT decode audio or launch FMOD.
#pragma once
#include "eor_archive.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
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
} // namespace hulk::eor::fmod
