#pragma once

#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"
#include "digitor/timeline_render_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace digitor {

struct ExportFrameTiming final {
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  bool force_keyframe{};
};

enum class ProductionGpuExportState : std::uint32_t {
  idle, running, draining, completed, cancelled, failed
};

struct ProductionGpuExportTelemetry final {
  ProductionGpuExportState state{ProductionGpuExportState::idle};
  std::uint64_t snapshot_identity{};
  std::uint64_t requested_frames{};
  std::uint64_t rendered_frames{};
  std::uint64_t encoded_frames{};
  std::uint64_t audio_blocks{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_staging_frames{};
  std::int64_t completed_us{};
  std::string diagnostic;
};

struct ProductionGpuExportCallbacks final {
  std::function<DigitorResult(const RenderAudioBlock&, std::int64_t,
                              std::string&)> submit_audio;
  std::function<void()> remove_partial_output;
  std::function<bool()> cancelled;
};

class ProductionGpuExportOrchestrator final {
 public:
  ProductionGpuExportOrchestrator(
      std::shared_ptr<const ExportRenderSnapshot> snapshot,
      TimelineRenderRuntime& timeline,
      HardwareEncoderCallbacks encoder_callbacks,
      ProductionGpuExportCallbacks callbacks = {});

  ProductionGpuExportOrchestrator(const ProductionGpuExportOrchestrator&) = delete;
  ProductionGpuExportOrchestrator& operator=(const ProductionGpuExportOrchestrator&) = delete;

  [[nodiscard]] DigitorResult execute(const std::vector<ExportFrameTiming>& schedule,
                                      std::string* diagnostic = nullptr) noexcept;
  void cancel() noexcept;
  [[nodiscard]] ProductionGpuExportTelemetry telemetry() const;

 private:
  [[nodiscard]] bool cancelled_locked() const;
  DigitorResult fail_locked(DigitorResult result, std::string diagnostic,
                            std::string* output) noexcept;
  void cleanup_partial_locked() noexcept;

  std::shared_ptr<const ExportRenderSnapshot> snapshot_;
  TimelineRenderRuntime& timeline_;
  HardwareEncoderCallbacks encoder_callbacks_;
  ProductionGpuExportCallbacks callbacks_;
  mutable std::mutex mutex_;
  ProductionGpuExportTelemetry telemetry_;
  std::atomic_bool cancel_requested_{false};
  bool cleanup_called_{};
};

}  // namespace digitor
