#pragma once

#include "digitor/color.hpp"
#include "digitor/digitor.h"
#include "digitor/lut.hpp"
#include "digitor/professional_color_management.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace digitor {

enum class ProfessionalLutFormat { cube, three_dl, csp, spi1d, spi3d, unknown };
enum class LutPrecision { fp16, fp32 };
enum class LutGpuBackend { d3d12, vulkan, metal, opengl_es };

struct ProfessionalLutMetadata {
  std::string id;
  std::string title;
  ProfessionalLutFormat format{ProfessionalLutFormat::unknown};
  std::uint32_t edge_size{};
  bool one_dimensional{};
  Color domain_min{0, 0, 0, 1};
  Color domain_max{1, 1, 1, 1};
  std::uint64_t content_hash{};
};

struct ProfessionalLut {
  ProfessionalLutMetadata metadata;
  std::variant<Lut1D, Lut3D> data;
};

struct LutGpuResource {
  LutGpuBackend backend{LutGpuBackend::vulkan};
  LutPrecision precision{LutPrecision::fp16};
  std::string cache_key;
  std::uintptr_t native_handle{};
  std::uint32_t edge_size{};
};

struct ProfessionalLutCallbacks {
  std::function<DigitorResult(const ProfessionalLut&, LutInterpolation,
                              LutPrecision, LutGpuResource&, std::string&)>
      upload_gpu;
  std::function<DigitorResult(const LutGpuResource&, std::span<const Color>,
                              std::span<Color>, std::string&)>
      apply_gpu;
  std::function<void(LutGpuResource&)> release_gpu;
};

struct LutScopeTelemetry {
  std::uint64_t loaded_luts{};
  std::uint64_t rejected_luts{};
  std::uint64_t cpu_pixels{};
  std::uint64_t gpu_pixels{};
  std::uint64_t gpu_uploads{};
  std::uint64_t cache_hits{};
  std::uint64_t scope_dispatches{};
  std::uint64_t scope_fallbacks{};
  std::string last_error;
};

class ProfessionalLutEngine {
 public:
  explicit ProfessionalLutEngine(ProfessionalLutCallbacks callbacks = {},
                                 std::size_t gpu_cache_capacity = 16);
  ~ProfessionalLutEngine();

  DigitorResult load_text(std::string id, ProfessionalLutFormat format,
                          std::string_view text, std::string* diagnostic = nullptr);
  DigitorResult remove(const std::string& id);
  const ProfessionalLut* find(const std::string& id) const noexcept;
  std::vector<ProfessionalLutMetadata> inventory() const;

  DigitorResult apply(const std::string& id, std::span<const Color> source,
                      std::span<Color> destination,
                      LutInterpolation interpolation = LutInterpolation::tetrahedral,
                      bool require_gpu = false,
                      LutGpuBackend backend = LutGpuBackend::vulkan,
                      LutPrecision precision = LutPrecision::fp16,
                      std::string* diagnostic = nullptr);
  void clear_gpu_cache() noexcept;
  LutScopeTelemetry telemetry() const { return telemetry_; }

 private:
  struct CachedGpu { LutGpuResource resource; std::uint64_t use_stamp{}; };
  ProfessionalLutCallbacks callbacks_;
  std::size_t cache_capacity_;
  std::uint64_t stamp_{};
  std::unordered_map<std::string, ProfessionalLut> luts_;
  std::unordered_map<std::string, CachedGpu> gpu_cache_;
  LutScopeTelemetry telemetry_;
};

struct GpuScopeCallbacks {
  std::function<DigitorResult(std::span<const Color>, std::uint32_t,
                              std::uint32_t, const ScopeConfig&, ScopeResult&,
                              std::string&)> dispatch;
};

class ProductionGpuScopes {
 public:
  explicit ProductionGpuScopes(GpuScopeCallbacks callbacks = {},
                               bool allow_cpu_reference = true);
  DigitorResult generate(const ProfessionalColorManagement& color,
                         std::span<const Color> working_pixels,
                         std::uint32_t width, std::uint32_t height,
                         const ScopeConfig& config, ScopeResult& result,
                         bool require_gpu = false,
                         std::string* diagnostic = nullptr);
  LutScopeTelemetry telemetry() const { return telemetry_; }

 private:
  GpuScopeCallbacks callbacks_;
  bool allow_cpu_reference_;
  LutScopeTelemetry telemetry_;
};

}  // namespace digitor
