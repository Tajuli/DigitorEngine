#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace digitor {

enum class RuntimeSessionKind : std::uint32_t { preview = 0, export_render = 1 };
enum class RuntimeSessionState : std::uint32_t {
  created = 0,
  preparing,
  running,
  cancelling,
  completed,
  cancelled,
  failed
};
enum class RuntimeSessionStatus : std::uint32_t {
  ok = 0,
  invalid_argument,
  invalid_state,
  resource_budget_exceeded,
  backend_unavailable,
  stage_failed,
  parity_mismatch,
  cancelled
};

struct RuntimeResourceBudget final {
  std::uint64_t max_cpu_bytes{512ull * 1024ull * 1024ull};
  std::uint64_t max_gpu_bytes{512ull * 1024ull * 1024ull};
  std::uint32_t max_in_flight_frames{3u};
};

struct RuntimeSessionConfig final {
  RuntimeSessionKind kind{RuntimeSessionKind::preview};
  RuntimeResourceBudget budget{};
  bool require_gpu{true};
  bool enforce_preview_export_parity{true};
};

struct RuntimeFrameRequest final {
  std::uint64_t frame_index{};
  std::uint64_t timeline_time_ns{};
  std::uint64_t estimated_cpu_bytes{};
  std::uint64_t estimated_gpu_bytes{};
  std::uint64_t expected_parity_digest{};
};

struct RuntimeFrameResult final {
  RuntimeSessionStatus status{RuntimeSessionStatus::invalid_argument};
  std::uint64_t output_digest{};
  std::uint64_t cpu_bytes_used{};
  std::uint64_t gpu_bytes_used{};
  std::uint32_t completed_stage{};
};

struct RuntimeSessionSnapshot final {
  RuntimeSessionState state{RuntimeSessionState::created};
  RuntimeSessionStatus last_status{RuntimeSessionStatus::ok};
  std::uint64_t submitted_frames{};
  std::uint64_t completed_frames{};
  std::uint64_t failed_frame_index{};
  double progress{};
};

using RuntimeFrameExecutor = std::function<RuntimeFrameResult(const RuntimeFrameRequest&)>;
using RuntimeProgressCallback = std::function<void(const RuntimeSessionSnapshot&)>;

class ProductionRuntimeSession final {
 public:
  explicit ProductionRuntimeSession(RuntimeSessionConfig config) noexcept;

  RuntimeSessionStatus prepare(bool gpu_backend_available) noexcept;
  RuntimeSessionStatus start(std::uint64_t total_frames) noexcept;
  RuntimeSessionStatus submit(const RuntimeFrameRequest& request,
                              const RuntimeFrameExecutor& executor) noexcept;
  RuntimeSessionStatus request_cancel() noexcept;
  RuntimeSessionStatus finish() noexcept;
  RuntimeSessionStatus fail(RuntimeSessionStatus status,
                            std::uint64_t frame_index) noexcept;

  void set_progress_callback(RuntimeProgressCallback callback);
  [[nodiscard]] RuntimeSessionSnapshot snapshot() const noexcept;
  [[nodiscard]] const RuntimeSessionConfig& config() const noexcept;

 private:
  void publish() noexcept;
  RuntimeSessionConfig config_{};
  RuntimeSessionSnapshot snapshot_{};
  RuntimeProgressCallback progress_callback_{};
  std::uint64_t total_frames_{};
  std::uint64_t last_frame_index_{};
  bool has_last_frame_{};
};

}  // namespace digitor

extern "C" {

struct DigitorRuntimeBudget {
  std::uint64_t max_cpu_bytes;
  std::uint64_t max_gpu_bytes;
  std::uint32_t max_in_flight_frames;
};

struct DigitorRuntimeSnapshot {
  std::uint32_t state;
  std::uint32_t last_status;
  std::uint64_t submitted_frames;
  std::uint64_t completed_frames;
  std::uint64_t failed_frame_index;
  double progress;
};

using DigitorRuntimeHandle = void*;

DigitorRuntimeHandle digitor_runtime_session_create(
    std::uint32_t kind,
    const DigitorRuntimeBudget* budget,
    std::uint32_t require_gpu,
    std::uint32_t enforce_parity);
std::uint32_t digitor_runtime_session_prepare(DigitorRuntimeHandle handle,
                                              std::uint32_t gpu_available);
std::uint32_t digitor_runtime_session_start(DigitorRuntimeHandle handle,
                                            std::uint64_t total_frames);
std::uint32_t digitor_runtime_session_cancel(DigitorRuntimeHandle handle);
std::uint32_t digitor_runtime_session_finish(DigitorRuntimeHandle handle);
std::uint32_t digitor_runtime_session_snapshot(DigitorRuntimeHandle handle,
                                               DigitorRuntimeSnapshot* output);
void digitor_runtime_session_destroy(DigitorRuntimeHandle handle);

}