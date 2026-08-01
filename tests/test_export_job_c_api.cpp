#include "digitor/export_job_c_api.h"
#include <cassert>
#include <cstring>

int main() {
  DigitorExportJobConfig invalid{};
  assert(digitor_export_job_create(&invalid) == nullptr);

  DigitorExportJobConfig config{};
  config.ffmpeg_path = "ffmpeg";
  config.input_path = "input.mp4";
  config.output_path = "output.mp4";
  config.codec = 0;
  config.width = 1920;
  config.height = 1080;
  config.fps_num = 30;
  config.fps_den = 1;
  config.video_bitrate = 8000000;
  config.audio_sample_rate = 48000;
  config.audio_channels = 2;
  config.prefer_hardware = 1;
  config.allow_software_fallback = 1;
  config.duration_us = 2000000;

  DigitorExportJob* job = digitor_export_job_create(&config);
  assert(job != nullptr);
  DigitorExportJobSnapshot snapshot{};
  assert(digitor_export_job_snapshot(job, &snapshot) == 1);
  assert(snapshot.state == DIGITOR_EXPORT_JOB_IDLE);
  assert(snapshot.duration_us == 2000000);
  assert(std::strcmp(snapshot.diagnostic, "idle") == 0);
  assert(digitor_export_job_cancel(job) == 1);
  assert(digitor_export_job_start(job) == 1);
  assert(digitor_export_job_wait(job) == 1);
  assert(digitor_export_job_snapshot(job, &snapshot) == 1);
  assert(snapshot.state == DIGITOR_EXPORT_JOB_CANCELLED);
  assert(snapshot.generation >= 2);
  digitor_export_job_destroy(job);
  return 0;
}
