// Automatic in-game renderer health summary: no RenderDoc UI or manual
// category copying. The existing native renderer emits these true events.
#include "native_gpu_autodiag.h"
#include <rex/logging.h>
#include <spdlog/sinks/base_sink.h>
#include <algorithm>
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
    if(s.find("[HULK/INVALID-VERTEX-DRAW]")!=s.npos)++values_.bad_vertex;
    if(s.find("[HULK/INCOMPLETE-XDK-DRAW]")!=s.npos)++values_.bad_xdk;
    if(s.find("[HULK/PM4-DMA-REJECT]")!=s.npos)++values_.pm4_dma;
    if(s.find("[HULK/PRESENT-FRAME-RECOVERY]")!=s.npos)++values_.present_recovery;
    if(s.find("[HULK/SCENE-RESOLVE]")!=s.npos)++values_.scene_resolve;
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
  static double Number(std::string_view v) {
    if(v.empty()||v.size()>32)return 0;
    const std::string copy(v);char*end=nullptr;
    const double n=std::strtod(copy.c_str(),&end);
    return end==copy.c_str()+copy.size() && n>=0 && n<100000?n:0;
  }
  Counters values_;
};
std::shared_ptr<GpuSink> sink;
}
void InstallAutomaticGpuDiagnostics() {
  if(sink)return;
  sink=std::make_shared<GpuSink>();
  rex::AddSink(sink);
  REXLOG_INFO("[HULK/AUTO-GPU] enabled in existing log: shader/PM4/Resolve "
              "fault aggregation and Vulkan GPU timings; no RenderDoc required");
}
void ReportAutomaticGpuDiagnostics() {
  if(!sink)return;
  const Counters s=sink->Snapshot();
  REXLOG_INFO("[HULK/AUTO-GPU] getCompTexLOD_failures={} "
              "vertex_shader_analysis_failures={} bad_vertex_fetch={} "
              "incomplete_XDK_draws={} DMA_rejects={} present_recoveries={}",
              s.comp_lod,s.vertex_programs,s.bad_vertex,s.bad_xdk,
              s.pm4_dma,s.present_recovery);
  REXLOG_INFO("[HULK/AUTO-GPU] scene_resolves={} PM4_replay_events={} "
              "gpu_windows={} worst_GPU_average_ms={:.3f} "
              "worst_GPU_spike_ms={:.3f} last_frame_interval_ms={:.3f}",
              s.scene_resolve,s.pm4_replay,s.timing_windows,
              s.worst_gpu_avg,s.worst_gpu_max,s.frame_interval);
  REXLOG_INFO("[HULK/AUTO-GPU] guest_720p={} host_2560x1440={} "
              "verified_16_10_camera={} real_EoR_Vulkan_scene_NOT_YET_VERIFIED",
              s.guest720,s.host1440,s.wide_camera);
}
} // namespace hulk
