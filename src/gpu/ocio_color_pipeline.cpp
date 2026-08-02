#include "digitor/ocio_color_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef DIGITOR_HAS_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

namespace digitor {
namespace {

void set_diagnostic(std::string* diagnostic, std::string value) {
  if (diagnostic) *diagnostic = std::move(value);
}

bool finite(Color value) {
  return std::isfinite(value.r) && std::isfinite(value.g) &&
         std::isfinite(value.b) && std::isfinite(value.a);
}

std::string request_key(const OcioTransformRequest& request,
                        const OcioDynamicProperties& dynamic) {
  std::ostringstream key;
  key << static_cast<int>(request.kind) << '|' << request.source << '|'
      << request.destination << '|' << request.display << '|' << request.view
      << '|' << request.look << '|' << request.named_transform << '|'
      << request.file_path << '|' << request.inverse_named_transform << '|'
      << static_cast<int>(request.interpolation) << '|' << request.inverse << '|'
      << request.bypass << '|' << request.use_display_view_looks;
  for (const auto& variable : request.context) {
    key << '|' << variable.name << '=' << variable.value;
  }
  key << "|e:" << dynamic.exposure_enabled << ':' << dynamic.exposure
      << "|c:" << dynamic.contrast_enabled << ':' << dynamic.contrast << ':'
      << dynamic.contrast_pivot << "|g:" << dynamic.gamma_enabled << ':'
      << dynamic.gamma;
  return key.str();
}

#ifdef DIGITOR_HAS_OCIO
OCIO::TransformDirection direction(bool inverse) {
  return inverse ? OCIO::TRANSFORM_DIR_INVERSE : OCIO::TRANSFORM_DIR_FORWARD;
}

OCIO::Interpolation interpolation(OcioInterpolation value) {
  switch (value) {
    case OcioInterpolation::nearest: return OCIO::INTERP_NEAREST;
    case OcioInterpolation::linear: return OCIO::INTERP_LINEAR;
    case OcioInterpolation::tetrahedral: return OCIO::INTERP_TETRAHEDRAL;
    case OcioInterpolation::best: return OCIO::INTERP_BEST;
  }
  return OCIO::INTERP_BEST;
}

OCIO::GpuLanguage gpu_language(OcioGpuLanguage value) {
  switch (value) {
    case OcioGpuLanguage::hlsl_d3d12: return OCIO::GPU_LANGUAGE_HLSL_DX11;
    case OcioGpuLanguage::glsl_vulkan: return OCIO::GPU_LANGUAGE_GLSL_4_0;
    case OcioGpuLanguage::glsl_gles: return OCIO::GPU_LANGUAGE_GLSL_ES_3_0;
    case OcioGpuLanguage::msl_metal: return OCIO::GPU_LANGUAGE_MSL_2_0;
  }
  return OCIO::GPU_LANGUAGE_GLSL_4_0;
}
#endif

}  // namespace

struct OcioColorPipeline::Impl {
  explicit Impl(AdvancedColorPipelineConfig value)
      : config(std::move(value)), native(config.native_fallback) {}

  AdvancedColorPipelineConfig config;
  OcioConfigInventory inventory;
  OcioProcessorTelemetry telemetry;
  ProfessionalColorManagement native;
  mutable std::mutex mutex;
  bool is_loaded{};

#ifdef DIGITOR_HAS_OCIO
  OCIO::ConstConfigRcPtr ocio_config;
  struct CacheEntry {
    OCIO::ConstCPUProcessorRcPtr cpu;
    OCIO::ConstProcessorRcPtr processor;
    std::list<std::string>::iterator lru;
  };
  std::unordered_map<std::string, CacheEntry> processors;
  std::list<std::string> lru;

  OCIO::ConstContextRcPtr context_for(const OcioTransformRequest& request) const {
    auto context = ocio_config->getCurrentContext()->createEditableCopy();
    for (const auto& variable : request.context) {
      context->setStringVar(variable.name.c_str(), variable.value.c_str());
    }
    return context;
  }

  OCIO::ConstProcessorRcPtr build_processor(const OcioTransformRequest& request) const {
    const auto context = context_for(request);
    OCIO::ConstProcessorRcPtr processor;
    switch (request.kind) {
      case OcioTransformKind::color_space:
        processor = ocio_config->getProcessor(
            context, request.source.c_str(), request.destination.c_str());
        break;
      case OcioTransformKind::display_view: {
        auto transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc(request.source.c_str());
        transform->setDisplay(request.display.c_str());
        transform->setView(request.view.c_str());
        transform->setLooksBypass(!request.use_display_view_looks);
        transform->setDirection(direction(request.inverse));
        processor = ocio_config->getProcessor(context, transform);
        break;
      }
      case OcioTransformKind::look: {
        auto transform = OCIO::LookTransform::Create();
        transform->setSrc(request.source.c_str());
        transform->setDst(request.destination.c_str());
        transform->setLooks(request.look.c_str());
        transform->setDirection(direction(request.inverse));
        processor = ocio_config->getProcessor(context, transform);
        break;
      }
      case OcioTransformKind::named_transform: {
        auto transform = OCIO::NamedTransform::Create();
        transform->setName(request.named_transform.c_str());
        if (!request.inverse_named_transform.empty()) {
          transform->setInverseTransform(
              ocio_config->getNamedTransform(request.inverse_named_transform.c_str())
                  ->getTransform(OCIO::TRANSFORM_DIR_FORWARD));
        }
        processor = ocio_config->getProcessor(
            context, transform, direction(request.inverse));
        break;
      }
      case OcioTransformKind::file_transform: {
        auto transform = OCIO::FileTransform::Create();
        transform->setSrc(request.file_path.c_str());
        transform->setInterpolation(interpolation(request.interpolation));
        transform->setDirection(direction(request.inverse));
        processor = ocio_config->getProcessor(context, transform);
        break;
      }
    }
    return processor;
  }

  CacheEntry& processor_for(const OcioTransformRequest& request,
                            const OcioDynamicProperties& dynamic) {
    const std::string key = request_key(request, dynamic);
    if (auto found = processors.find(key); found != processors.end()) {
      lru.splice(lru.begin(), lru, found->second.lru);
      ++telemetry.processor_cache_hits;
      return found->second;
    }

    auto processor = build_processor(request);
    auto cpu = processor->getDefaultCPUProcessor();
    lru.push_front(key);
    auto [inserted, _] = processors.emplace(
        key, CacheEntry{cpu, processor, lru.begin()});
    ++telemetry.processor_compiles;
    const std::size_t capacity = std::max<std::size_t>(1, config.processor_cache_capacity);
    while (processors.size() > capacity) {
      const auto victim = lru.back();
      lru.pop_back();
      processors.erase(victim);
    }
    return inserted->second;
  }
#endif
};

OcioColorPipeline::OcioColorPipeline(AdvancedColorPipelineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
OcioColorPipeline::~OcioColorPipeline() = default;
OcioColorPipeline::OcioColorPipeline(OcioColorPipeline&&) noexcept = default;
OcioColorPipeline& OcioColorPipeline::operator=(OcioColorPipeline&&) noexcept = default;

bool OcioColorPipeline::compiled_with_ocio() noexcept {
#ifdef DIGITOR_HAS_OCIO
  return true;
#else
  return false;
#endif
}

const char* OcioColorPipeline::compiled_ocio_version() noexcept {
#ifdef DIGITOR_HAS_OCIO
  return OCIO_VERSION_STR;
#else
  return "unavailable";
#endif
}

DigitorResult OcioColorPipeline::load(std::string* diagnostic) {
  std::scoped_lock lock(impl_->mutex);
  impl_->telemetry.last_error.clear();
  impl_->inventory = {};
#ifdef DIGITOR_HAS_OCIO
  try {
    if (!impl_->config.enable_ocio) {
      impl_->is_loaded = true;
      impl_->inventory.name = "Digitor native color pipeline";
      impl_->inventory.valid = true;
      set_diagnostic(diagnostic, {});
      return DIGITOR_RESULT_OK;
    }
    if (!impl_->config.config_text.empty()) {
      std::istringstream input(impl_->config.config_text);
      impl_->ocio_config = OCIO::Config::CreateFromStream(input);
    } else if (!impl_->config.config_path.empty()) {
      impl_->ocio_config = OCIO::Config::CreateFromFile(impl_->config.config_path.c_str());
    } else {
      impl_->ocio_config = OCIO::GetCurrentConfig();
    }
    impl_->ocio_config->sanityCheck();
    impl_->inventory.name = impl_->ocio_config->getName();
    impl_->inventory.description = impl_->ocio_config->getDescription();
    impl_->inventory.family_separator = impl_->ocio_config->getFamilySeparator();
    impl_->inventory.default_display = impl_->ocio_config->getDefaultDisplay();
    impl_->inventory.active_displays = impl_->ocio_config->getActiveDisplays();
    impl_->inventory.active_views = impl_->ocio_config->getActiveViews();
    for (int i = 0; i < impl_->ocio_config->getNumColorSpaces(); ++i) {
      impl_->inventory.color_spaces.emplace_back(
          impl_->ocio_config->getColorSpaceNameByIndex(i));
    }
    for (int i = 0; i < impl_->ocio_config->getNumNamedTransforms(); ++i) {
      impl_->inventory.named_transforms.emplace_back(
          impl_->ocio_config->getNamedTransformNameByIndex(i));
    }
    for (int i = 0; i < impl_->ocio_config->getNumLooks(); ++i) {
      impl_->inventory.looks.emplace_back(impl_->ocio_config->getLookNameByIndex(i));
    }
    for (int i = 0; i < impl_->ocio_config->getNumDisplays(); ++i) {
      const std::string display = impl_->ocio_config->getDisplay(i);
      impl_->inventory.displays.push_back(display);
      auto& views = impl_->inventory.views_by_display[display];
      for (int j = 0; j < impl_->ocio_config->getNumViews(display.c_str()); ++j) {
        views.emplace_back(impl_->ocio_config->getView(display.c_str(), j));
      }
    }
    static constexpr const char* roles[] = {
        OCIO::ROLE_DEFAULT, OCIO::ROLE_REFERENCE, OCIO::ROLE_SCENE_LINEAR,
        OCIO::ROLE_COMPOSITING_LOG, OCIO::ROLE_COLOR_PICKING,
        OCIO::ROLE_DATA, OCIO::ROLE_MATTE_PAINT, OCIO::ROLE_TEXTURE_PAINT};
    for (const char* role : roles) {
      if (impl_->ocio_config->hasRole(role)) {
        impl_->inventory.roles.emplace(role,
                                       impl_->ocio_config->getColorSpace(role)->getName());
      }
    }
    impl_->inventory.valid = true;
    impl_->is_loaded = true;
    impl_->telemetry.config_cache_id = impl_->ocio_config->getCacheID();
    impl_->processors.clear();
    impl_->lru.clear();
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    impl_->is_loaded = false;
    impl_->inventory.validation_error = error.what();
    impl_->telemetry.last_error = error.what();
    set_diagnostic(diagnostic, error.what());
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
#else
  if (!impl_->config.enable_ocio) {
    impl_->is_loaded = true;
    impl_->inventory.name = "Digitor native color pipeline";
    impl_->inventory.valid = true;
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }
  const std::string message = "OpenColorIO 2.x was not found when DigitorEngine was built";
  impl_->telemetry.last_error = message;
  impl_->inventory.validation_error = message;
  set_diagnostic(diagnostic, message);
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
#endif
}

DigitorResult OcioColorPipeline::reload(std::string* diagnostic) {
  invalidate_processors();
  return load(diagnostic);
}

bool OcioColorPipeline::loaded() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->is_loaded;
}

const OcioConfigInventory& OcioColorPipeline::inventory() const noexcept {
  return impl_->inventory;
}

DigitorResult OcioColorPipeline::validate_request(
    const OcioTransformRequest& request, std::string* diagnostic) const {
  if (request.bypass) {
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }
  std::string error;
  switch (request.kind) {
    case OcioTransformKind::color_space:
      if (request.source.empty() || request.destination.empty())
        error = "color-space transforms require source and destination";
      break;
    case OcioTransformKind::display_view:
      if (request.source.empty() || request.display.empty() || request.view.empty())
        error = "display/view transforms require source, display and view";
      break;
    case OcioTransformKind::look:
      if (request.source.empty() || request.destination.empty() || request.look.empty())
        error = "look transforms require source, destination and look";
      break;
    case OcioTransformKind::named_transform:
      if (request.named_transform.empty())
        error = "named transforms require a transform name";
      break;
    case OcioTransformKind::file_transform:
      if (request.file_path.empty()) error = "file transforms require a path";
      break;
  }
  if (error.empty()) {
    for (const auto& variable : request.context) {
      if (variable.name.empty()) {
        error = "OCIO context variable names must not be empty";
        break;
      }
    }
  }
  set_diagnostic(diagnostic, error);
  return error.empty() ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
}

DigitorResult OcioColorPipeline::transform_pixel(
    const OcioTransformRequest& request, Color input, Color& output,
    const OcioDynamicProperties& dynamic, std::string* diagnostic) {
  return transform_image(request, std::span<const Color>(&input, 1),
                         std::span<Color>(&output, 1), dynamic, diagnostic);
}

DigitorResult OcioColorPipeline::transform_image(
    const OcioTransformRequest& request, std::span<const Color> source,
    std::span<Color> destination, const OcioDynamicProperties& dynamic,
    std::string* diagnostic) {
  if (source.size() != destination.size() || source.empty()) {
    set_diagnostic(diagnostic, "source and destination spans must be non-empty and equal");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (validate_request(request, diagnostic) != DIGITOR_RESULT_OK)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  for (const auto& pixel : source) {
    if (impl_->config.reject_non_finite && !finite(pixel)) {
      set_diagnostic(diagnostic, "non-finite input pixel");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }
  if (request.bypass) {
    std::copy(source.begin(), source.end(), destination.begin());
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }

  std::scoped_lock lock(impl_->mutex);
  if (!impl_->is_loaded) {
    set_diagnostic(diagnostic, "color pipeline is not loaded");
    return DIGITOR_RESULT_INVALID_STATE;
  }
#ifdef DIGITOR_HAS_OCIO
  if (impl_->config.enable_ocio) {
    try {
      auto& entry = impl_->processor_for(request, dynamic);
      std::vector<float> rgba(source.size() * 4u);
      for (std::size_t i = 0; i < source.size(); ++i) {
        rgba[i * 4u + 0u] = source[i].r;
        rgba[i * 4u + 1u] = source[i].g;
        rgba[i * 4u + 2u] = source[i].b;
        rgba[i * 4u + 3u] = source[i].a;
      }
      OCIO::PackedImageDesc image(rgba.data(), static_cast<long>(source.size()), 1, 4);
      entry.cpu->apply(image);
      for (std::size_t i = 0; i < destination.size(); ++i) {
        destination[i] = {rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u],
                          impl_->config.strict_alpha_preservation ? source[i].a
                                                                  : rgba[i * 4u + 3u]};
      }
      impl_->telemetry.cpu_pixels += source.size();
      impl_->telemetry.processor_cache_id = entry.processor->getCacheID();
      set_diagnostic(diagnostic, {});
      return DIGITOR_RESULT_OK;
    } catch (const std::exception& error) {
      impl_->telemetry.last_error = error.what();
      set_diagnostic(diagnostic, error.what());
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }
#endif
  for (std::size_t i = 0; i < source.size(); ++i) {
    destination[i] = impl_->native.transform(source[i]).output;
    if (impl_->config.strict_alpha_preservation) destination[i].a = source[i].a;
  }
  impl_->telemetry.cpu_pixels += source.size();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult OcioColorPipeline::compile_gpu_shader(
    const OcioTransformRequest& request, OcioGpuLanguage language,
    OcioGpuShader& shader, const OcioDynamicProperties& dynamic,
    std::string* diagnostic) {
  if (validate_request(request, diagnostic) != DIGITOR_RESULT_OK)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lock(impl_->mutex);
#ifdef DIGITOR_HAS_OCIO
  if (!impl_->is_loaded || !impl_->config.enable_ocio) {
    set_diagnostic(diagnostic, "OCIO GPU shader compilation requires a loaded OCIO config");
    return DIGITOR_RESULT_INVALID_STATE;
  }
  try {
    auto& entry = impl_->processor_for(request, dynamic);
    auto gpu = entry.processor->getDefaultGPUProcessor();
    auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
    desc->setLanguage(gpu_language(language));
    desc->setFunctionName("digitor_ocio_transform");
    desc->setResourcePrefix("digitor_ocio_");
    gpu->extractGpuShaderInfo(desc);
    shader = {};
    shader.language = language;
    shader.cache_id = desc->getCacheID();
    shader.function_name = desc->getFunctionName();
    shader.source = desc->getShaderText();
    ++impl_->telemetry.gpu_shader_compiles;
    impl_->telemetry.processor_cache_id = entry.processor->getCacheID();
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    impl_->telemetry.last_error = error.what();
    set_diagnostic(diagnostic, error.what());
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
#else
  (void)language;
  (void)shader;
  (void)dynamic;
  const std::string message = "OpenColorIO 2.x GPU processor is unavailable";
  impl_->telemetry.last_error = message;
  set_diagnostic(diagnostic, message);
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
#endif
}

void OcioColorPipeline::invalidate_processors() noexcept {
  std::scoped_lock lock(impl_->mutex);
#ifdef DIGITOR_HAS_OCIO
  impl_->processors.clear();
  impl_->lru.clear();
#endif
  ++impl_->telemetry.invalidations;
  impl_->telemetry.processor_cache_id.clear();
}

OcioProcessorTelemetry OcioColorPipeline::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

const AdvancedColorPipelineConfig& OcioColorPipeline::config() const noexcept {
  return impl_->config;
}

}  // namespace digitor
