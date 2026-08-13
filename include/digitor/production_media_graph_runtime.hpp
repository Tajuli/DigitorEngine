#pragma once

#include "digitor/production_hardware_decode.hpp"
#include "digitor/production_hardware_encode.hpp"
#include "digitor/production_node_graph.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace digitor {

// The only platform-specific work left outside the engine is presenting an
// already-rendered native GPU frame to the Flutter texture registry. No pixel
// processing, node execution, readback, or export encoding belongs there.
using ProductionPreviewPresenter = std::function<DigitorResult(
    const ProcessedGpuFramePtr& frame, std::string& diagnostic)>;
using ProductionExportProgress =
    std::function<void(std::uint64_t completed_frames, std::uint64_t total_frames)>;

struct ProductionMediaGraphRuntimeTelemetry {
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t cpu_readbacks{};
  bool export_running{};
  bool cancelled{};
  std::string graph_identity;
};

// Engine-owned coordinator shared by production preview and export.
//
// A runtime is pinned to one immutable ProductionNodeGraph recipe. Both preview
// and export decode into GPU-resident ProcessedGpuFrame objects, execute that
// exact graph through Engine::execute_native_node_graph(), and never offer a CPU
// fallback. Export submits the graph result directly to
// ProductionHardwareEncodeSession.
class ProductionMediaGraphRuntime final {
 public:
  ProductionMediaGraphRuntime(
      std::unique_ptr<ProductionHardwareDecodeSession> decoder,
      const ProductionNodeGraph& graph,
      ProductionPreviewPresenter presenter,
      HardwareEncoderCallbacks encoder_callbacks = {});
  ~ProductionMediaGraphRuntime();

  ProductionMediaGraphRuntime(const ProductionMediaGraphRuntime&) = delete;
  ProductionMediaGraphRuntime& operator=(const ProductionMediaGraphRuntime&) = delete;

  [[nodiscard]] DigitorResult preview(
      FrameNumber frame_number,
      ProcessedGpuFramePtr* out_frame = nullptr,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult preview_at_timestamp(
      std::int64_t timestamp_us, ProcessedGpuFramePtr* out_frame = nullptr,
      std::string* diagnostic = nullptr) noexcept;

  // Encodes the requested source frames in order. The caller determines the
  // frame range/timeline sampling, while decode, graph processing and encoding
  // remain entirely inside DigitorEngine.
  [[nodiscard]] DigitorResult export_frames(
      std::span<const FrameNumber> frame_numbers,
      HardwareEncodeConfig config,
      std::string* diagnostic = nullptr,
      ProductionExportProgress progress = {}) noexcept;

  // Export-time encoder override used by the Flutter V2 frozen-snapshot path.
  // Preview runtimes can therefore be created without encoder callbacks and
  // receive a snapshot-bound hardware adapter only when export actually starts.
  [[nodiscard]] DigitorResult export_frames_with_encoder(
      std::span<const FrameNumber> frame_numbers,
      HardwareEncodeConfig config,
      HardwareEncoderCallbacks encoder_callbacks,
      std::string* diagnostic = nullptr,
      ProductionExportProgress progress = {}) noexcept;

  void cancel() noexcept;
  [[nodiscard]] ProductionMediaGraphRuntimeTelemetry telemetry() const;
  [[nodiscard]] const std::string& graph_identity() const noexcept {
    return graph_identity_;
  }

 private:
  struct RenderedFrame {
    ProcessedGpuFramePtr frame;
    std::int64_t pts_us{};
    std::int64_t duration_us{};
  };

  [[nodiscard]] DigitorResult validate_graph(std::string* diagnostic) const noexcept;
  [[nodiscard]] DigitorResult render_frame(
      FrameNumber frame_number,
      RenderedFrame& output,
      std::string* diagnostic) noexcept;
  [[nodiscard]] DigitorResult render_frame_at_timestamp(
      std::int64_t timestamp_us, RenderedFrame& output,
      std::string* diagnostic) noexcept;
  static void set_diagnostic(std::string* output, std::string value) noexcept;

  std::unique_ptr<ProductionHardwareDecodeSession> decoder_;
  const ProductionNodeGraph* graph_{};
  std::string graph_identity_;
  ProductionPreviewPresenter presenter_;
  HardwareEncoderCallbacks encoder_callbacks_;
  std::atomic_bool cancelled_{false};

  mutable std::mutex mutex_;
  ProductionHardwareEncodeSession* active_export_{};
  ProductionMediaGraphRuntimeTelemetry telemetry_{};
};

}  // namespace digitor
