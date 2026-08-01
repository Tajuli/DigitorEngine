#include "digitor/export_job_c_api.h"
#include "digitor/hardware_export_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct DigitorExportJob {
  digitor::FfmpegExportRequest request;
  digitor::ExportProfile profile;
  std::string ffmpeg{"ffmpeg"};
  std::thread worker;
  mutable std::mutex mutex;
  std::atomic_bool cancel_requested{false};
  DigitorExportJobSnapshot snapshot{};
};

namespace {
DigitorExportJobState map_state(bool success, bool cancelled) noexcept {
  if (cancelled) return DIGITOR_EXPORT_JOB_CANCELLED;
  return success ? DIGITOR_EXPORT_JOB_COMPLETED : DIGITOR_EXPORT_JOB_FAILED;
}
void copy_diag(char (&out)[256], const std::string& value) noexcept {
  std::memset(out, 0, sizeof(out));
  std::memcpy(out, value.data(), std::min(value.size(), sizeof(out) - 1));
}
}

extern "C" DigitorExportJob* digitor_export_job_create(const DigitorExportJobConfig* config) {
  try {
    if (!config || !config->input_path || !config->output_path || config->width <= 0 || config->height <= 0 || config->fps_num <= 0 || config->fps_den <= 0) return nullptr;
    auto job = std::make_unique<DigitorExportJob>();
    if (config->ffmpeg_path && *config->ffmpeg_path) job->ffmpeg = config->ffmpeg_path;
    job->request.input_path = config->input_path;
    job->request.output_path = config->output_path;
    job->request.duration_us = std::max<int64_t>(config->duration_us, 0);
    job->profile.codec = static_cast<digitor::ExportCodec>(config->codec);
    job->profile.width = config->width;
    job->profile.height = config->height;
    job->profile.fps_num = config->fps_num;
    job->profile.fps_den = config->fps_den;
    job->profile.video_bitrate = config->video_bitrate;
    job->profile.audio_sample_rate = config->audio_sample_rate;
    job->profile.audio_channels = config->audio_channels;
    job->profile.prefer_hardware = config->prefer_hardware != 0;
    job->profile.allow_software_fallback = config->allow_software_fallback != 0;
    job->snapshot.state = DIGITOR_EXPORT_JOB_IDLE;
    job->snapshot.duration_us = job->request.duration_us;
    copy_diag(job->snapshot.diagnostic, "idle");
    return job.release();
  } catch (...) { return nullptr; }
}

extern "C" int32_t digitor_export_job_start(DigitorExportJob* job) {
  if (!job || job->worker.joinable()) return 0;
  {
    std::scoped_lock lock(job->mutex);
    job->snapshot.state = DIGITOR_EXPORT_JOB_RUNNING;
    ++job->snapshot.generation;
    copy_diag(job->snapshot.diagnostic, "running");
  }
  job->worker = std::thread([job] {
    try {
      if (job->cancel_requested.load()) {
        std::scoped_lock lock(job->mutex);
        job->snapshot.state = DIGITOR_EXPORT_JOB_CANCELLED;
        copy_diag(job->snapshot.diagnostic, "cancelled before execution");
        return;
      }
      digitor::HardwareAwareExportRuntime runtime(job->ffmpeg);
      const auto result = runtime.execute(job->request, job->profile);
      std::scoped_lock lock(job->mutex);
      job->snapshot.requested_backend = static_cast<int32_t>(result.requested_backend);
      job->snapshot.executed_backend = static_cast<int32_t>(result.executed_backend);
      job->snapshot.used_fallback = result.used_fallback ? 1 : 0;
      job->snapshot.process_exit_code = result.process_exit_code;
      const bool cancelled = job->cancel_requested.load();
      job->snapshot.state = map_state(result.success, cancelled);
      job->snapshot.completed_us = result.success ? job->snapshot.duration_us : 0;
      job->snapshot.progress = result.success ? 1.0 : 0.0;
      copy_diag(job->snapshot.diagnostic, cancelled ? "cancelled" : result.diagnostic);
    } catch (...) {
      std::scoped_lock lock(job->mutex);
      job->snapshot.state = DIGITOR_EXPORT_JOB_FAILED;
      copy_diag(job->snapshot.diagnostic, "unhandled export exception");
    }
  });
  return 1;
}
extern "C" int32_t digitor_export_job_pause(DigitorExportJob* job) { if (!job) return 0; std::scoped_lock lock(job->mutex); if (job->snapshot.state != DIGITOR_EXPORT_JOB_RUNNING) return 0; job->snapshot.state = DIGITOR_EXPORT_JOB_PAUSED; ++job->snapshot.generation; return 1; }
extern "C" int32_t digitor_export_job_resume(DigitorExportJob* job) { if (!job) return 0; std::scoped_lock lock(job->mutex); if (job->snapshot.state != DIGITOR_EXPORT_JOB_PAUSED) return 0; job->snapshot.state = DIGITOR_EXPORT_JOB_RUNNING; ++job->snapshot.generation; return 1; }
extern "C" int32_t digitor_export_job_cancel(DigitorExportJob* job) { if (!job) return 0; job->cancel_requested.store(true); std::scoped_lock lock(job->mutex); if (job->snapshot.state == DIGITOR_EXPORT_JOB_COMPLETED || job->snapshot.state == DIGITOR_EXPORT_JOB_FAILED) return 0; job->snapshot.state = DIGITOR_EXPORT_JOB_CANCELLED; ++job->snapshot.generation; copy_diag(job->snapshot.diagnostic, "cancellation requested"); return 1; }
extern "C" int32_t digitor_export_job_wait(DigitorExportJob* job) { if (!job) return 0; if (job->worker.joinable()) job->worker.join(); return 1; }
extern "C" int32_t digitor_export_job_snapshot(const DigitorExportJob* job, DigitorExportJobSnapshot* out) { if (!job || !out) return 0; std::scoped_lock lock(job->mutex); *out = job->snapshot; return 1; }
extern "C" void digitor_export_job_destroy(DigitorExportJob* job) { if (!job) return; job->cancel_requested.store(true); if (job->worker.joinable()) job->worker.join(); delete job; }
