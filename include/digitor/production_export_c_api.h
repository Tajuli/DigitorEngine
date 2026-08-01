#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorExportSession DigitorExportSession;

typedef enum DigitorExportState {
  DIGITOR_EXPORT_IDLE = 0,
  DIGITOR_EXPORT_RUNNING = 1,
  DIGITOR_EXPORT_PAUSED = 2,
  DIGITOR_EXPORT_COMPLETED = 3,
  DIGITOR_EXPORT_CANCELLED = 4,
  DIGITOR_EXPORT_FAILED = 5
} DigitorExportState;

typedef struct DigitorExportSnapshot {
  int32_t state;
  int64_t duration_us;
  int64_t completed_us;
  double progress;
  uint64_t generation;
} DigitorExportSnapshot;

DigitorExportSession* digitor_export_session_create(const char* project_id,
                                                     const char* output_path,
                                                     const char* checkpoint_path,
                                                     int64_t duration_us,
                                                     uint64_t timeline_revision,
                                                     uint64_t render_revision);
int32_t digitor_export_session_start(DigitorExportSession* session);
int32_t digitor_export_session_pause(DigitorExportSession* session);
int32_t digitor_export_session_resume(DigitorExportSession* session);
int32_t digitor_export_session_advance(DigitorExportSession* session,
                                       int64_t completed_us);
int32_t digitor_export_session_cancel(DigitorExportSession* session);
int32_t digitor_export_session_snapshot(const DigitorExportSession* session,
                                        DigitorExportSnapshot* snapshot);
void digitor_export_session_destroy(DigitorExportSession* session);

#ifdef __cplusplus
}
#endif
