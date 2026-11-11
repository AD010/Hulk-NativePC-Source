#pragma once
// Host-file transaction shared by the Hulk SDK adapter and regression tests.
// Payload bytes are opaque: this does not convert the Xbox save format.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace hulk::save {
inline FILE* OpenCommitFile(const std::filesystem::path& path, bool write) {
#ifdef _WIN32
  return _wfopen(path.c_str(), write ? L"wb" : L"r+b");
#else
  return std::fopen(path.c_str(), write ? "wb" : "r+b");
#endif
}
inline bool FlushCommitFile(FILE* file) {
  if (std::fflush(file) != 0) return false;
#ifdef _WIN32
  return _commit(_fileno(file)) == 0;
#else
  return ::fsync(fileno(file)) == 0;
#endif
}
inline bool ReplaceCommitFile(const std::filesystem::path& from,
                              const std::filesystem::path& to) {
#ifdef _WIN32
  return MoveFileExW(from.c_str(), to.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  return !ec;
#endif
}

inline bool SyncCommitDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
  // Windows commit uses MoveFileExW with WRITE_THROUGH below. Do not claim
  // POSIX directory-fsync semantics for Windows filesystems/cloud providers.
  (void)path;
  return true;
#else
  const int directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (directory < 0) return false;
  const bool synced = ::fsync(directory) == 0;
  const bool closed = ::close(directory) == 0;
  return synced && closed;
#endif
}

// HULK/SAVE-COMMIT: never truncate the currently readable header in place.
// Requires one writer process. In-process writers are serialized. Each prior
// version gets its own backup; existing backups are never overwritten/deleted.
inline bool CommitMetadata(const std::filesystem::path& target,
                           const void* bytes, size_t size) {
  if (!bytes || !size || target.empty() || target.filename().empty()) return false;
  static std::mutex mutex;
  static std::atomic<uint64_t> counter{0};
  std::lock_guard lock(mutex);
  namespace fs = std::filesystem;
  fs::path staging;
  try {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) return false;
    auto status = fs::symlink_status(target, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return false;
    const bool existed = fs::exists(status);
    if (existed && !fs::is_regular_file(status)) return false;
    // An exclusively created directory owns both temporary names. A crashed
    // transaction may leave one behind; it is never mistaken for a live save.
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
      auto candidate = target;
      candidate += ".pending-" + std::to_string(
          uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())) +
          "-" + std::to_string(counter.fetch_add(1));
      ec.clear();
      if (fs::create_directory(candidate, ec)) { staging = candidate; break; }
      if (ec && ec != std::errc::file_exists) return false;
    }
    if (staging.empty()) return false;
    const auto temporary = staging / "new";
    FILE* output = OpenCommitFile(temporary, true);
    if (!output) { fs::remove(staging, ec); return false; }
    const bool written = std::fwrite(bytes, 1, size, output) == size;
    const bool flushed = written && FlushCommitFile(output);
    const bool closed = std::fclose(output) == 0;
    if (!written || !flushed || !closed) {
      fs::remove(temporary, ec); fs::remove(staging, ec); return false;
    }
    if (existed) {
      const auto backup = staging / "previous";
      ec.clear();
      if (!fs::copy_file(target, backup, fs::copy_options::none, ec) || ec) {
        fs::remove(temporary, ec); fs::remove(backup, ec); fs::remove(staging, ec); return false;
      }
      FILE* previous = OpenCommitFile(backup, false);
      bool durable = false;
      if (previous) {
        durable = FlushCommitFile(previous);
        if (std::fclose(previous) != 0) durable = false;
      }
      if (!durable) {
        fs::remove(temporary, ec); fs::remove(backup, ec); fs::remove(staging, ec); return false;
      }
    }
    // Persist recovery-directory entries before replacing the live name.
    if (!SyncCommitDirectory(staging) || !SyncCommitDirectory(target.parent_path()))
      return false;
    if (!ReplaceCommitFile(temporary, target)) {
      fs::remove(temporary, ec);
      // Retain the verified previous version even on a failed replacement.
      if (!existed) fs::remove(staging, ec);
      return false;
    }
    if (!SyncCommitDirectory(target.parent_path())) return false;
    // A successful overwrite keeps its backup directory. First creation
    // leaves no empty transaction directory behind.
    if (!existed) fs::remove(staging, ec);
    return true;
  } catch (...) {
    // Preserve all pre-existing bytes and any staged recovery material.
    return false;
  }
}
}  // namespace hulk::save
