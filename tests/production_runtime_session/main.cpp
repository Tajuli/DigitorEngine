#include "digitor/production_runtime_session.hpp"

#include <cstdint>
#include <vector>

namespace {

digitor::RuntimeFrameResult execute_ok(
    const digitor::RuntimeFrameRequest& request) {
  digitor::RuntimeFrameResult result;
  result.status = digitor::RuntimeSessionStatus::ok;
  result.output_digest = request.expected_parity_digest;
  result.cpu_bytes_used = request.estimated_cpu_bytes;
  result.gpu_bytes_used = request.estimated_gpu_bytes;
  result.completed_stage = 7u;
  return result;
}

}  // namespace

int main() {
  using namespace digitor;

  RuntimeSessionConfig config;
  config.kind = RuntimeSessionKind::export_render;
  config.budget.max_cpu_bytes = 64u * 1024u * 1024u;
  config.budget.max_gpu_bytes = 128u * 1024u * 1024u;
  config.budget.max_in_flight_frames = 3u;

  ProductionRuntimeSession session(config);
  std::vector<double> progress;
  session.set_progress_callback(
      [&progress](const RuntimeSessionSnapshot& snapshot) {
        progress.push_back(snapshot.progress);
      });

  if (session.prepare(true) != RuntimeSessionStatus::ok) return 1;
  if (session.start(120u) != RuntimeSessionStatus::ok) return 2;

  for (std::uint64_t frame = 0; frame < 120u; ++frame) {
    RuntimeFrameRequest request;
    request.frame_index = frame;
    request.timeline_time_ns = frame * 33333333u;
    request.estimated_cpu_bytes = 4u * 1024u * 1024u;
    request.estimated_gpu_bytes = 8u * 1024u * 1024u;
    request.expected_parity_digest = 1000u + frame;
    if (session.submit(request, execute_ok) != RuntimeSessionStatus::ok) {
      return 3;
    }
  }
  if (session.finish() != RuntimeSessionStatus::ok) return 4;
  const auto complete = session.snapshot();
  if (complete.state != RuntimeSessionState::completed ||
      complete.completed_frames != 120u || complete.progress != 1.0) {
    return 5;
  }
  for (std::size_t i = 1; i < progress.size(); ++i) {
    if (progress[i] < progress[i - 1]) return 6;
  }

  ProductionRuntimeSession no_gpu(config);
  if (no_gpu.prepare(false) != RuntimeSessionStatus::backend_unavailable ||
      no_gpu.snapshot().state != RuntimeSessionState::failed) {
    return 7;
  }

  ProductionRuntimeSession over_budget(config);
  if (over_budget.prepare(true) != RuntimeSessionStatus::ok ||
      over_budget.start(1u) != RuntimeSessionStatus::ok) {
    return 8;
  }
  RuntimeFrameRequest huge;
  huge.frame_index = 0u;
  huge.estimated_cpu_bytes = config.budget.max_cpu_bytes + 1u;
  huge.expected_parity_digest = 1u;
  if (over_budget.submit(huge, execute_ok) !=
      RuntimeSessionStatus::resource_budget_exceeded) {
    return 9;
  }

  ProductionRuntimeSession mismatch(config);
  mismatch.prepare(true);
  mismatch.start(1u);
  RuntimeFrameRequest parity;
  parity.frame_index = 0u;
  parity.expected_parity_digest = 99u;
  if (mismatch.submit(parity, [](const RuntimeFrameRequest&) {
        RuntimeFrameResult result;
        result.status = RuntimeSessionStatus::ok;
        result.output_digest = 100u;
        return result;
      }) != RuntimeSessionStatus::parity_mismatch) {
    return 10;
  }

  ProductionRuntimeSession cancelled(config);
  cancelled.prepare(true);
  cancelled.start(2u);
  if (cancelled.request_cancel() != RuntimeSessionStatus::cancelled) return 11;
  RuntimeFrameRequest cancelled_frame;
  cancelled_frame.frame_index = 0u;
  if (cancelled.submit(cancelled_frame, execute_ok) !=
          RuntimeSessionStatus::cancelled ||
      cancelled.snapshot().state != RuntimeSessionState::cancelled) {
    return 12;
  }

  DigitorRuntimeBudget c_budget{64u * 1024u * 1024u,
                                128u * 1024u * 1024u, 3u};
  DigitorRuntimeHandle handle = digitor_runtime_session_create(
      0u, &c_budget, 1u, 1u);
  if (!handle) return 13;
  if (digitor_runtime_session_prepare(handle, 1u) != 0u) return 14;
  if (digitor_runtime_session_start(handle, 1u) != 0u) return 15;
  DigitorRuntimeSnapshot snapshot{};
  if (digitor_runtime_session_snapshot(handle, &snapshot) != 0u ||
      snapshot.state != static_cast<std::uint32_t>(RuntimeSessionState::running)) {
    return 16;
  }
  digitor_runtime_session_destroy(handle);

  return 0;
}