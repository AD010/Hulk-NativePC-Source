// Automatic in-game renderer health summary: no RenderDoc UI or manual
// category copying. The existing native renderer emits these true events.
#include "native_gpu_autodiag.h"
#include <rex/logging.h>
#include <spdlog/sinks/base_sink.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
namespace hulk {
namespace {
struct Counters {
  uint64_t comp_lod=0, vertex_programs=0, bad_vertex=0;
  uint64_t bad_xdk=0, pm4_dma=0, present_recovery=0;
  uint64_t scene_resolve=0, pm4_replay=0, timing_windows=0;
  double worst_gpu_avg=0, worst_gpu_max=0, frame_interval=0;
  bool guest720=false, host1440=false, wide_camera=false;
};
class GpuSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  Counters Snapshot() {
    std::lock_guard<std::mutex> guard(mutex_);
    return values_;
  }
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    const std::string_view s(msg.payload.data(),msg.payload.size());
    if(s.find("[HULK/AUTO-GPU]")!=s.npos)return;
    if(s.find("getCompTexLOD requires pixel shader implicit derivatives")!=s.npos)
      ++values_.comp_lod;
    if(s.find("vertex shader ")!=s.npos && s.find("analysis failed:")!=s.npos)
      ++values_.vertex_programs;
    // HULK/AUTO-GPU-211-COUNTS: these warnings are throttled, e.g. line
    // #60000 follows #16. Counting emitted log lines is deeply misleading.
    // Store the highest explicit sequence observed, never invent a total.
    MaxSequence(s,"[HULK/INVALID-VERTEX-DRAW]",values_.bad_vertex);
    MaxSequence(s,"[HULK/INCOMPLETE-XDK-DRAW]",values_.bad_xdk);
    MaxSequence(s,"[HULK/PM4-DMA-REJECT]",values_.pm4_dma);
    MaxSequence(s,"[HULK/PRESENT-FRAME-RECOVERY]",values_.present_recovery);
    MaxSequence(s,"[HULK/SCENE-RESOLVE]",values_.scene_resolve);
    if(s.find("[HULK/GAP-STREAM]")!=s.npos)++values_.pm4_replay;
    if(s.find("guest_size=1280x720")!=s.npos)values_.guest720=true;
    if(s.find("host_size=2560x1440")!=s.npos)values_.host1440=true;
    if(s.find("widescreen_camera_hook=true")!=s.npos)values_.wide_camera=true;
    if(auto p=s.find("GPU ");p!=s.npos) {
      auto q=s.find(" ms average",p);
      if(q!=s.npos) {
        ++values_.timing_windows;
        values_.worst_gpu_avg=std::max(values_.worst_gpu_avg,Number(s.substr(p+4,q-p-4)));
        auto c=s.find(", ",q);
        auto d=c==s.npos?s.npos:s.find(" ms max",c);
        if(d!=s.npos)
          values_.worst_gpu_max=std::max(values_.worst_gpu_max,Number(s.substr(c+2,d-c-2)));
      }
    }
    if(auto p=s.find("frame interval ");p!=s.npos) {
      auto q=s.find(" ms average",p);
      if(q!=s.npos)values_.frame_interval=Number(s.substr(p+15,q-p-15));
    }
  }
  void flush_() override {}
 private:
  static void MaxSequence(std::string_view line, std::string_view marker,
                          uint64_t& value) {
    const size_t offset = line.find(marker);
    if (offset == line.npos) return;
    size_t start = offset + marker.size();
    while (start < line.size() && line[start] == ' ') ++start;
    if (start == line.size() || line[start] != '#') return;
    ++start;
    uint64_t n = 0;
    bool seen = false;
    while (start < line.size() &&
           line[start] >= '0' && line[start] <= '9') {
      seen = true;
      const uint64_t digit = uint64_t(line[start] - '0');
      if (n > (UINT64_MAX - digit) / 10) return;
      n = n * 10 + digit;
      ++start;
    }
    if (seen && n > value) value = n;
  }
  static double Number(std::string_view v) {
    if(v.empty()||v.size()>32)return 0;
    const std::string copy(v);char*end=nullptr;
    const double n=std::strtod(copy.c_str(),&end);
    return end==copy.c_str()+copy.size() && n>=0 && n<100000?n:0;
  }
  Counters values_;
};
std::shared_ptr<GpuSink> sink;
std::atomic<bool> reported{false};
}
void InstallAutomaticGpuDiagnostics() {
  if(sink)return;
  sink=std::make_shared<GpuSink>();
  rex::AddSink(sink);
  REXLOG_INFO("[HULK/AUTO-GPU] enabled in existing log: shader/PM4/Resolve "
              "fault aggregation and Vulkan GPU timings; no RenderDoc required");
}
void ReportAutomaticGpuDiagnostics() {
  if(!sink || reported.exchange(true,std::memory_order_acq_rel))return;
  const Counters s=sink->Snapshot();
  REXLOG_INFO("[HULK/AUTO-GPU] getCompTexLOD_failures={} "
              "vertex_shader_analysis_failures={} bad_vertex_fetch_at_least={} "
              "incomplete_XDK_draws_at_least={} DMA_rejects_at_least={} "
              "present_recoveries_at_least={}",
              s.comp_lod,s.vertex_programs,s.bad_vertex,s.bad_xdk,
              s.pm4_dma,s.present_recovery);
  REXLOG_INFO("[HULK/AUTO-GPU] scene_resolves_at_least={} PM4_replay_log_lines={} "
              "gpu_windows={} worst_GPU_average_ms={:.3f} "
              "worst_GPU_spike_ms={:.3f} last_frame_interval_ms={:.3f}",
              s.scene_resolve,s.pm4_replay,s.timing_windows,
              s.worst_gpu_avg,s.worst_gpu_max,s.frame_interval);
  REXLOG_INFO("[HULK/AUTO-GPU] guest_720p={} host_2560x1440={} "
              "verified_16_10_camera={} real_EoR_Vulkan_scene_NOT_YET_VERIFIED",
              s.guest720,s.host1440,s.wide_camera);
}
} // namespace hulk
