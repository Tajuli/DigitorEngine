#pragma once

#include "digitor/color.hpp"
#include "digitor/digitor.h"
#include "digitor/professional_color_management.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

inline constexpr DigitorResult DIGITOR_RESULT_INVALID_STATE =
    DIGITOR_RESULT_NOT_INITIALIZED;

enum class OcioTransformKind {
  color_space,
  display_view,
  look,
  named_transform,
  file_transform
};

enum class OcioGpuLanguage {
  hlsl_d3d12,
  glsl_vulkan,
  glsl_gles,
  msl_metal
};

enum class OcioInterpolation { nearest, linear, tetrahedral, best };

struct OcioContextVariable {
  std::string name;
  std::string value;
};

struct OcioTransformRequest {
  OcioTransformKind kind{OcioTransformKind::color_space};
  std::string source;
  std::string destination;
  std::string display;
  std::string view;
  std::string look;
  std::string named_transform;
  std::string file_path;
  std::string inverse_named_transform;
  OcioInterpolation interpolation{OcioInterpolation::best};
  std::vector<OcioContextVariable> context;
  bool inverse{};
  bool bypass{};
  bool use_display_view_looks{true};
};

struct OcioDynamicProperties {
  bool exposure_enabled{};
  double exposure{};
  bool contrast_enabled{};
  double contrast{1.0};
  double contrast_pivot{0.18};
  bool gamma_enabled{};
  double gamma{1.0};
};

struct OcioGpuShader {
  OcioGpuLanguage language{OcioGpuLanguage::glsl_vulkan};
  std::string cache_id;
  std::string function_name;
  std::string source;
  std::vector<float> texture_1d;
  std::vector<float> texture_3d;
  std::uint32_t texture_1d_width{};
  std::uint32_t texture_3d_edge{};
  bool requires_half_domain{};
};

struct OcioConfigInventory {
  std::string name;
  std::string description;
  std::string family_separator;
  std::string default_display;
  std::string active_displays;
  std::string active_views;
  std::vector<std::string> color_spaces;
  std::vector<std::string> named_transforms;
  std::vector<std::string> looks;
  std::vector<std::string> displays;
  std::unordered_map<std::string, std::vector<std::string>> views_by_display;
  std::unordered_map<std::string, std::string> roles;
  bool valid{};
  std::string validation_error;
};

struct OcioProcessorTelemetry {
  std::uint64_t processor_compiles{};
  std::uint64_t processor_cache_hits{};
  std::uint64_t cpu_pixels{};
  std::uint64_t gpu_shader_compiles{};
  std::uint64_t invalidations{};
  std::string config_cache_id;
  std::string processor_cache_id;
  std::string last_error;
};

struct AdvancedColorPipelineConfig {
  bool enable_ocio{true};
  bool require_ocio{};
  bool prefer_gpu{true};
  bool strict_alpha_preservation{true};
  bool reject_non_finite{true};
  std::size_t processor_cache_capacity{64};
  std::string config_path;
  std::string config_text;
  std::string working_space_override;
  ColorManagementConfig native_fallback{};
};

class OcioColorPipeline {
 public:
  explicit OcioColorPipeline(AdvancedColorPipelineConfig config = {});
  ~OcioColorPipeline();
  OcioColorPipeline(OcioColorPipeline&&) noexcept;
  OcioColorPipeline& operator=(OcioColorPipeline&&) noexcept;
  OcioColorPipeline(const OcioColorPipeline&) = delete;
  OcioColorPipeline& operator=(const OcioColorPipeline&) = delete;

  static bool compiled_with_ocio() noexcept;
  static const char* compiled_ocio_version() noexcept;

  DigitorResult load(std::string* diagnostic = nullptr);
  DigitorResult reload(std::string* diagnostic = nullptr);
  bool loaded() const noexcept;
  const OcioConfigInventory& inventory() const noexcept;

  DigitorResult validate_request(const OcioTransformRequest& request,
                                 std::string* diagnostic = nullptr) const;
  DigitorResult transform_pixel(const OcioTransformRequest& request,
                                Color input,
                                Color& output,
                                const OcioDynamicProperties& dynamic = {},
                                std::string* diagnostic = nullptr);
  DigitorResult transform_image(const OcioTransformRequest& request,
                                std::span<const Color> source,
                                std::span<Color> destination,
                                const OcioDynamicProperties& dynamic = {},
                                std::string* diagnostic = nullptr);
  DigitorResult compile_gpu_shader(const OcioTransformRequest& request,
                                   OcioGpuLanguage language,
                                   OcioGpuShader& shader,
                                   const OcioDynamicProperties& dynamic = {},
                                   std::string* diagnostic = nullptr);

  void invalidate_processors() noexcept;
  OcioProcessorTelemetry telemetry() const;
  const AdvancedColorPipelineConfig& config() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitor
