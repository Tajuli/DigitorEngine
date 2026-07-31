#include "digitor/windows_zero_copy_production.hpp"

#include <mutex>
#include <new>
#include <utility>

namespace digitor {

struct WindowsZeroCopyProductionPipeline::Impl {
  WindowsZeroCopyProductionConfig config;
  mutable std::mutex mutex;
  WindowsZeroCopyProductionTelemetry telemetry;
  std::unique_ptr<WindowsD3D12P010Dispatch> dispatch;
  std::unique_ptr<WindowsD3D12P010Converter> converter;
  std::unique_ptr<WindowsMediaFoundationHardwareEncoder> encoder;
  std::unique_ptr<WindowsZeroCopyNativePipeline> pipeline;
};

WindowsZeroCopyProductionPipeline::WindowsZeroCopyProductionPipeline(
    WindowsZeroCopyProductionConfig config)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
}

WindowsZeroCopyProductionPipeline::~WindowsZeroCopyProductionPipeline() = default;

DigitorResult WindowsZeroCopyProductionPipeline::validate_evidence(
    const WindowsZeroCopyProductionConfig& c, std::string& diagnostic) noexcept {
  const auto& e = c.evidence;
  if (!e.production_ready || !e.strict_gpu_first) {
    diagnostic = "qualification evidence is not production-ready and strict GPU-first";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (!e.nv12_pass || !e.p010_pass || !e.preview_export_identity_pass ||
      !e.per_pixel_accuracy_pass || !e.sustained_4k_pass || !e.leak_test_pass ||
      !e.hevc_main10_pass) {
    diagnostic = "one or more mandatory Windows zero-copy qualification gates failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (!e.adapter_luid || e.adapter_luid != c.current_adapter_luid) {
    diagnostic = "qualification adapter LUID does not match the active adapter";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (!e.driver_version || e.driver_version != c.current_driver_version) {
    diagnostic = "qualification driver version does not match the active driver";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.engine_commit.empty() || e.engine_commit != c.current_engine_commit) {
    diagnostic = "qualification engine commit does not match the running engine";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.qualification_id.empty()) {
    diagnostic = "qualification identity is missing";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (e.expires_unix_seconds <= c.current_unix_seconds) {
    diagnostic = "qualification evidence has expired";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.measured_fps < e.minimum_fps) {
    diagnostic = "sustained 4K throughput is below the qualified minimum";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.measured_mean_error > e.maximum_mean_error) {
    diagnostic = "per-pixel mean error exceeds the qualified threshold";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.measured_resource_delta > e.maximum_resource_delta) {
    diagnostic = "resource delta exceeds the qualified leak budget";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  diagnostic = "Windows zero-copy production evidence accepted";
  return DIGITOR_RESULT_OK;
}

DigitorResult WindowsZeroCopyProductionPipeline::initialize() noexcept {
  try {
    auto& i = *impl_;
    std::string diagnostic;
    auto result = validate_evidence(i.config, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.activation_failures;
      i.telemetry.diagnostic = std::move(diagnostic);
      return result;
    }
    if (!i.config.enable_preview && !i.config.enable_export) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.activation_failures;
      i.telemetry.diagnostic = "both preview and export are disabled";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    i.dispatch = std::make_unique<WindowsD3D12P010Dispatch>(i.config.p010_dispatch);
    result = i.dispatch->initialize();
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.activation_failures;
      i.telemetry.diagnostic = "D3D12 P010 dispatch initialization failed";
      return result;
    }

    i.config.p010_conversion.gpu_dispatch = i.dispatch->callback();
    i.converter = std::make_unique<WindowsD3D12P010Converter>(i.config.p010_conversion);
    result = i.converter->initialize();
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.activation_failures;
      i.telemetry.diagnostic = "shared P010 converter initialization failed";
      return result;
    }

    i.encoder = std::make_unique<WindowsMediaFoundationHardwareEncoder>(
        i.config.encoder, i.converter->callback());
    if (i.config.enable_export) {
      result = i.encoder->initialize();
      if (result != DIGITOR_RESULT_OK) {
        std::scoped_lock lock(i.mutex);
        ++i.telemetry.activation_failures;
        i.telemetry.diagnostic = "Media Foundation hardware encoder initialization failed";
        return result;
      }
      i.config.native_binding.encoder_submit = i.encoder->callback();
    }

    i.pipeline = std::make_unique<WindowsZeroCopyNativePipeline>(
        i.config.runtime, i.config.native_binding);
    result = i.pipeline->initialize();
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(i.mutex);
      ++i.telemetry.activation_failures;
      i.telemetry.diagnostic = "evidence-gated native zero-copy runtime initialization failed";
      return result;
    }

    std::scoped_lock lock(i.mutex);
    i.telemetry.initialized = true;
    i.telemetry.diagnostic = "Windows zero-copy production pipeline initialized";
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult WindowsZeroCopyProductionPipeline::preview(std::int64_t timestamp_us) noexcept {
  auto& i = *impl_;
  {
    std::scoped_lock lock(i.mutex);
    if (!i.telemetry.initialized || i.telemetry.quarantined || !i.config.enable_preview)
      return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  const auto result = i.pipeline->preview(timestamp_us);
  std::scoped_lock lock(i.mutex);
  if (result == DIGITOR_RESULT_OK) ++i.telemetry.preview_frames;
  else { ++i.telemetry.runtime_failures; i.telemetry.diagnostic = "preview consumer failed"; }
  return result;
}

DigitorResult WindowsZeroCopyProductionPipeline::export_frame(std::int64_t timestamp_us) noexcept {
  auto& i = *impl_;
  {
    std::scoped_lock lock(i.mutex);
    if (!i.telemetry.initialized || i.telemetry.quarantined || !i.config.enable_export)
      return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  const auto result = i.pipeline->export_frame(timestamp_us);
  std::scoped_lock lock(i.mutex);
  if (result == DIGITOR_RESULT_OK) ++i.telemetry.exported_frames;
  else { ++i.telemetry.runtime_failures; i.telemetry.diagnostic = "hardware export consumer failed"; }
  return result;
}

DigitorResult WindowsZeroCopyProductionPipeline::finalize_export() noexcept {
  auto& i = *impl_;
  if (!i.pipeline || !i.config.enable_export) return DIGITOR_RESULT_NOT_INITIALIZED;
  const auto result = i.pipeline->flush_export();
  if (result != DIGITOR_RESULT_OK) {
    std::scoped_lock lock(i.mutex);
    ++i.telemetry.runtime_failures;
    i.telemetry.diagnostic = "hardware export finalization failed";
  }
  return result;
}

void WindowsZeroCopyProductionPipeline::quarantine(std::string reason) noexcept {
  std::scoped_lock lock(impl_->mutex);
  impl_->telemetry.quarantined = true;
  impl_->telemetry.diagnostic = reason.empty() ? "production pipeline quarantined" : std::move(reason);
}

WindowsZeroCopyProductionTelemetry WindowsZeroCopyProductionPipeline::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  auto out = impl_->telemetry;
  if (impl_->converter) out.cpu_copies += impl_->converter->telemetry().cpu_copies;
  if (impl_->dispatch) out.cpu_copies += impl_->dispatch->telemetry().cpu_copies;
  if (impl_->pipeline) {
    const auto runtime = impl_->pipeline->runtime_telemetry();
    out.cpu_fallback_frames += runtime.cpu_fallback_frames;
  }
  return out;
}

} // namespace digitor
