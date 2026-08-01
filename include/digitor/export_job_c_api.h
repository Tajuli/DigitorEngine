#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DigitorExportJob DigitorExportJob;
typedef enum DigitorExportJobState { DIGITOR_EXPORT_JOB_IDLE=0, DIGITOR_EXPORT_JOB_RUNNING=1, DIGITOR_EXPORT_JOB_PAUSED=2, DIGITOR_EXPORT_JOB_COMPLETED=3, DIGITOR_EXPORT_JOB_CANCELLED=4, DIGITOR_EXPORT_JOB_FAILED=5 } DigitorExportJobState;
typedef struct DigitorExportJobConfig { const char* ffmpeg_path; const char* input_path; const char* output_path; const char* checkpoint_path; int32_t codec; int32_t width; int32_t height; int32_t fps_num; int32_t fps_den; int64_t video_bitrate; int32_t audio_sample_rate; int32_t audio_channels; int32_t prefer_hardware; int32_t allow_software_fallback; int64_t duration_us; } DigitorExportJobConfig;
typedef struct DigitorExportJobSnapshot { DigitorExportJobState state; int32_t requested_backend; int32_t executed_backend; int32_t used_fallback; int32_t process_exit_code; int64_t duration_us; int64_t completed_us; double progress; uint64_t generation; char diagnostic[256]; } DigitorExportJobSnapshot;
DigitorExportJob* digitor_export_job_create(const DigitorExportJobConfig* config);
int32_t digitor_export_job_start(DigitorExportJob* job);
int32_t digitor_export_job_pause(DigitorExportJob* job);
int32_t digitor_export_job_resume(DigitorExportJob* job);
int32_t digitor_export_job_cancel(DigitorExportJob* job);
int32_t digitor_export_job_wait(DigitorExportJob* job);
int32_t digitor_export_job_snapshot(const DigitorExportJob* job, DigitorExportJobSnapshot* out_snapshot);
void digitor_export_job_destroy(DigitorExportJob* job);
#ifdef __cplusplus
}
#endif
