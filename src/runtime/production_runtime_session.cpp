#include "digitor/production_runtime_session.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace digitor {
namespace {

[[nodiscard]] bool valid_budget(const RuntimeResourceBudget& budget) noexcept {
  return budget.max_cpu_bytes > 0u && budget.max_gpu_bytes > 0u &&
         budget.max_in_flight_frames > 0u;
}

[[nodiscard]] bool terminal(RuntimeSessionState state) noexcept {
  return state == RuntimeSessionState::completed ||
         state == RuntimeSessionState::cancelled ||
         state == RuntimeSessionState::failed;
}

}  // namespace

ProductionRuntimeSession::ProductionRuntimeSession(
    RuntimeSessionConfig config) noexcept
    : config_(config) {
  if (!valid_budget(config_.budget)) {
    snapshot_.state = RuntimeSessionState::failed;
    snapshot_.last_status = RuntimeSessionStatus::invalid_argument;
  }
}

RuntimeSessionStatus ProductionRuntimeSession::prepare(
    bool gpu_backend_available) noexcept {
  if (snapshot_.state != RuntimeSessionState::created) {
    return RuntimeSessionStatus::invalid_state;
  }
  if (config_.require_gpu && !gpu_backend_available) {
    snapshot_.state = RuntimeSessionState::failed;
    snapshot_.last_status = RuntimeSessionStatus::backend_unavailable;
    publish();
    return snapshot_.last_status;
  }
  snapshot_.state = RuntimeSessionState::preparing;
  snapshot_.last_status = RuntimeSessionStatus::ok;
  publish();
  return RuntimeSessionStatus::ok;
}

RuntimeSessionStatus ProductionRuntimeSession::start(
    std::uint64_t total_frames) noexcept {
  if (snapshot_.state != RuntimeSessionState::preparing || total_frames == 0u) {
    return RuntimeSessionStatus::invalid_state;
  }
  total_frames_ = total_frames;
  snapshot_.state = RuntimeSessionState::running;
  snapshot_.progress = 0.0;
  snapshot_.last_status = RuntimeSessionStatus::ok;
  publish();
  return RuntimeSessionStatus::ok;
}

RuntimeSessionStatus ProductionRuntimeSession::submit(
    const RuntimeFrameRequest& request,
    const RuntimeFrameExecutor& executor) noexcept {
  if (snapshot_.state == RuntimeSessionState::cancelling) {
    snapshot_.state = RuntimeSessionState::cancelled;
    snapshot_.last_status = RuntimeSessionStatus::cancelled;
    publish();
    return snapshot_.last_status;
  }
  if (snapshot_.state != RuntimeSessionState::running || !executor ||
      request.frame_index >= total_frames_ ||
      (has_last_frame_ && request.frame_index <= last_frame_index_)) {
    return RuntimeSessionStatus::invalid_state;
  }
  if (request.estimated_cpu_bytes > config_.budget.max_cpu_bytes ||
      request.estimated_gpu_bytes > config_.budget.max_gpu_bytes) {
    return fail(RuntimeSessionStatus::resource_budget_exceeded,
                request.frame_index);
  }

  ++snapshot_.submitted_frames;
  const RuntimeFrameResult result = executor(request);
  if (result.status != RuntimeSessionStatus::ok) {
    return fail(result.status, request.frame_index);
  }
  if (result.cpu_bytes_used > config_.budget.max_cpu_bytes ||
      result.gpu_bytes_used > config_.budget.max_gpu_bytes) {
    return fail(RuntimeSessionStatus::resource_budget_exceeded,
                request.frame_index);
  }
  if (config_.enforce_preview_export_parity &&
      request.expected_parity_digest != 0u &&
      result.output_digest != request.expected_parity_digest) {
    return fail(RuntimeSessionStatus::parity_mismatch, request.frame_index);
  }

  has_last_frame_ = true;
  last_frame_index_ = request.frame_index;
  ++snapshot_.completed_frames;
  snapshot_.progress = std::min(
      1.0, static_cast<double>(snapshot_.completed_frames) /
               static_cast<double>(total_frames_));
  snapshot_.last_status = RuntimeSessionStatus::ok;
  publish();
  return RuntimeSessionStatus::ok;
}

RuntimeSessionStatus ProductionRuntimeSession::request_cancel() noexcept {
  if (terminal(snapshot_.state)) {
    return RuntimeSessionStatus::invalid_state;
  }
  if (snapshot_.state == RuntimeSessionState::created ||
      snapshot_.state == RuntimeSessionState::preparing) {
    snapshot_.state = RuntimeSessionState::cancelled;
    snapshot_.last_status = RuntimeSessionStatus::cancelled;
  } else if (snapshot_.state == RuntimeSessionState::running) {
    snapshot_.state = RuntimeSessionState::cancelling;
    snapshot_.last_status = RuntimeSessionStatus::cancelled;
  }
  publish();
  return RuntimeSessionStatus::cancelled;
}

RuntimeSessionStatus ProductionRuntimeSession::finish() noexcept {
  if (snapshot_.state == RuntimeSessionState::cancelling) {
    snapshot_.state = RuntimeSessionState::cancelled;
    snapshot_.last_status = RuntimeSessionStatus::cancelled;
    publish();
    return snapshot_.last_status;
  }
  if (snapshot_.state != RuntimeSessionState::running ||
      snapshot_.completed_frames != total_frames_) {
    return RuntimeSessionStatus::invalid_state;
  }
  snapshot_.state = RuntimeSessionState::completed;
  snapshot_.last_status = RuntimeSessionStatus::ok;
  snapshot_.progress = 1.0;
  publish();
  return RuntimeSessionStatus::ok;
}

RuntimeSessionStatus ProductionRuntimeSession::fail(
    RuntimeSessionStatus status, std::uint64_t frame_index) noexcept {
  if (status == RuntimeSessionStatus::ok || terminal(snapshot_.state)) {
    return RuntimeSessionStatus::invalid_state;
  }
  snapshot_.state = RuntimeSessionState::failed;
  snapshot_.last_status = status;
  snapshot_.failed_frame_index = frame_index;
  publish();
  return status;
}

void ProductionRuntimeSession::set_progress_callback(
    RuntimeProgressCallback callback) {
  progress_callback_ = std::move(callback);
}

RuntimeSessionSnapshot ProductionRuntimeSession::snapshot() const noexcept {
  return snapshot_;
}

const RuntimeSessionConfig& ProductionRuntimeSession::config() const noexcept {
  return config_;
}

void ProductionRuntimeSession::publish() noexcept {
  if (progress_callback_) {
    try {
      progress_callback_(snapshot_);
    } catch (...) {
      snapshot_.state = RuntimeSessionState::failed;
      snapshot_.last_status = RuntimeSessionStatus::stage_failed;
    }
  }
}

}  // namespace digitor

extern "C" DigitorRuntimeHandle digitor_runtime_session_create(
    std::uint32_t kind, const DigitorRuntimeBudget* budget,
    std::uint32_t require_gpu, std::uint32_t enforce_parity) {
  if (!budget || kind > 1u) {
    return nullptr;
  }
  digitor::RuntimeSessionConfig config;
  config.kind = static_cast<digitor::RuntimeSessionKind>(kind);
  config.budget.max_cpu_bytes = budget->max_cpu_bytes;
  config.budget.max_gpu_bytes = budget->max_gpu_bytes;
  config.budget.max_in_flight_frames = budget->max_in_flight_frames;
  config.require_gpu = require_gpu != 0u;
  config.enforce_preview_export_parity = enforce_parity != 0u;
  try {
    return new digitor::ProductionRuntimeSession(config);
  } catch (...) {
    return nullptr;
  }
}

extern "C" std::uint32_t digitor_runtime_session_prepare(
    DigitorRuntimeHandle handle, std::uint32_t gpu_available) {
  if (!handle) {
    return static_cast<std::uint32_t>(
        digitor::RuntimeSessionStatus::invalid_argument);
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRuntimeSession*>(handle)->prepare(
          gpu_available != 0u));
}

extern "C" std::uint32_t digitor_runtime_session_start(
    DigitorRuntimeHandle handle, std::uint64_t total_frames) {
  if (!handle) {
    return static_cast<std::uint32_t>(
        digitor::RuntimeSessionStatus::invalid_argument);
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRuntimeSession*>(handle)->start(
          total_frames));
}

extern "C" std::uint32_t digitor_runtime_session_cancel(
    DigitorRuntimeHandle handle) {
  if (!handle) {
    return static_cast<std::uint32_t>(
        digitor::RuntimeSessionStatus::invalid_argument);
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRuntimeSession*>(handle)->request_cancel());
}

extern "C" std::uint32_t digitor_runtime_session_finish(
    DigitorRuntimeHandle handle) {
  if (!handle) {
    return static_cast<std::uint32_t>(
        digitor::RuntimeSessionStatus::invalid_argument);
  }
  return static_cast<std::uint32_t>(
      static_cast<digitor::ProductionRuntimeSession*>(handle)->finish());
}

extern "C" std::uint32_t digitor_runtime_session_snapshot(
    DigitorRuntimeHandle handle, DigitorRuntimeSnapshot* output) {
  if (!handle || !output) {
    return static_cast<std::uint32_t>(
        digitor::RuntimeSessionStatus::invalid_argument);
  }
  const auto snapshot =
      static_cast<digitor::ProductionRuntimeSession*>(handle)->snapshot();
  output->state = static_cast<std::uint32_t>(snapshot.state);
  output->last_status = static_cast<std::uint32_t>(snapshot.last_status);
  output->submitted_frames = snapshot.submitted_frames;
  output->completed_frames = snapshot.completed_frames;
  output->failed_frame_index = snapshot.failed_frame_index;
  output->progress = snapshot.progress;
  return static_cast<std::uint32_t>(digitor::RuntimeSessionStatus::ok);
}

extern "C" void digitor_runtime_session_destroy(DigitorRuntimeHandle handle) {
  delete static_cast<digitor::ProductionRuntimeSession*>(handle);
}