#include "digitor/production_export_c_api.h"

#include "digitor/production_export.hpp"

#include <filesystem>
#include <memory>
#include <new>

struct DigitorExportSession {
  digitor::ResumableExportSession session;
  explicit DigitorExportSession(digitor::ResumableExportSession value)
      : session(std::move(value)) {}
};

extern "C" {

DigitorExportSession* digitor_export_session_create(const char* project_id,
                                                     const char* output_path,
                                                     const char* checkpoint_path,
                                                     int64_t duration_us,
                                                     uint64_t timeline_revision,
                                                     uint64_t render_revision) {
  try {
    if (!project_id || !output_path || !checkpoint_path || duration_us <= 0) return nullptr;
    digitor::ExportCheckpoint checkpoint;
    checkpoint.project_id = project_id;
    checkpoint.output_path = output_path;
    checkpoint.duration_us = duration_us;
    checkpoint.timeline_revision = timeline_revision;
    checkpoint.render_revision = render_revision;
    return new DigitorExportSession(digitor::ResumableExportSession(
        std::move(checkpoint), std::filesystem::path(checkpoint_path)));
  } catch (...) {
    return nullptr;
  }
}

int32_t digitor_export_session_start(DigitorExportSession* session) {
  try { return session && session->session.start() ? 1 : 0; } catch (...) { return 0; }
}

int32_t digitor_export_session_pause(DigitorExportSession* session) {
  try { return session && session->session.pause() ? 1 : 0; } catch (...) { return 0; }
}

int32_t digitor_export_session_resume(DigitorExportSession* session) {
  try { return session && session->session.resume() ? 1 : 0; } catch (...) { return 0; }
}

int32_t digitor_export_session_advance(DigitorExportSession* session,
                                       int64_t completed_us) {
  try { return session && session->session.advance(completed_us) ? 1 : 0; } catch (...) { return 0; }
}

int32_t digitor_export_session_cancel(DigitorExportSession* session) {
  try { return session && session->session.cancel() ? 1 : 0; } catch (...) { return 0; }
}

int32_t digitor_export_session_snapshot(const DigitorExportSession* session,
                                        DigitorExportSnapshot* snapshot) {
  try {
    if (!session || !snapshot) return 0;
    const auto value = session->session.snapshot();
    snapshot->state = static_cast<int32_t>(value.state);
    snapshot->duration_us = value.duration_us;
    snapshot->completed_us = value.completed_us;
    snapshot->progress = value.progress;
    snapshot->generation = value.generation;
    return 1;
  } catch (...) {
    return 0;
  }
}

void digitor_export_session_destroy(DigitorExportSession* session) {
  try { delete session; } catch (...) {}
}

}  // extern "C"
