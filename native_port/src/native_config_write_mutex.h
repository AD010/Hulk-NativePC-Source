#pragma once
#include <mutex>

namespace hulk::config {
// All in-process editors of the single live TOML must serialize the entire
// read/modify/replace transaction, not just their individual temp files.
inline std::mutex& WriteMutex() {
  static std::mutex mutex;
  return mutex;
}
}
