#include "digitor/professional_lut_scopes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace digitor {
namespace {

void set_diagnostic(std::string* diagnostic, std::string value) {
  if (diagnostic) *diagnostic = std::move(value);
}

bool finite(Color value) {
  return std::isfinite(value.r) && std::isfinite(value.g) &&
         std::isfinite(value.b) && std::isfinite(value.a);
}

std::uint64_t fnv1a(std::string_view text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char value : text) {
    hash ^= value;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string format_name(ProfessionalLutFormat format) {
  switch (format) {
    case ProfessionalLutFormat::cube: return "cube";
    case ProfessionalLutFormat::three_dl: return "3dl";
    case ProfessionalLutFormat::csp: return "csp";
    case ProfessionalLutFormat::spi1d: return "spi1d";
    case ProfessionalLutFormat::spi3d: return "spi3d";
    case ProfessionalLutFormat::unknown: return "unknown";
  }
  return "unknown";
}

ProfessionalLut parse_numeric_grid(std::string id, ProfessionalLutFormat format,
                                   std::string_view text) {
  std::istringstream input{std::string(text)};
  std::vector<Color> values;
  std::string line;
  std::uint32_t declared_size{};
  bool one_dimensional = format == ProfessionalLutFormat::spi1d;
  while (std::getline(input, line)) {
    const auto comment = line.find_first_of("#;");
    if (comment != std::string::npos) line.resize(comment);
    std::istringstream row(line);
    std::string first;
    if (!(row >> first)) continue;
    if (first == "SPILUT" || first == "CSPLUTV100" || first == "3DMESH" ||
        first == "Mesh" || first == "BEGIN" || first == "END") continue;
    if (first == "LUT_1D_SIZE" || first == "LUT_3D_SIZE" || first == "Components" ||
        first == "Length" || first == "Size") {
      row >> declared_size;
      if (first == "LUT_1D_SIZE" || first == "Length") one_dimensional = true;
      continue;
    }
    char* end = nullptr;
    const float r = std::strtof(first.c_str(), &end);
    if (!end || *end != '\0') continue;
    float g{}, b{};
    if (!(row >> g >> b)) {
      if (!one_dimensional) continue;
      g = b = r;
    }
    Color value{r, g, b, 1.0f};
    if (!finite(value)) throw std::runtime_error("LUT contains non-finite values");
    values.push_back(value);
  }
  if (values.size() < 2u) throw std::runtime_error("LUT contains insufficient samples");

  ProfessionalLut result;
  result.metadata.id = std::move(id);
  result.metadata.title = result.metadata.id;
  result.metadata.format = format;
  result.metadata.content_hash = fnv1a(text);
  if (one_dimensional) {
    if (declared_size != 0u && declared_size != values.size())
      throw std::runtime_error("1D LUT sample count does not match declared size");
    result.metadata.one_dimensional = true;
    result.metadata.edge_size = static_cast<std::uint32_t>(values.size());
    result.data = Lut1D(std::move(values));
    return result;
  }

  std::uint32_t edge = declared_size;
  if (edge == 0u) {
    const auto candidate = static_cast<std::uint32_t>(
        std::llround(std::cbrt(static_cast<double>(values.size()))));
    if (static_cast<std::size_t>(candidate) * candidate * candidate != values.size())
      throw std::runtime_error("3D LUT sample count is not a perfect cube");
    edge = candidate;
  }
  if (edge < 2u || static_cast<std::size_t>(edge) * edge * edge != values.size())
    throw std::runtime_error("3D LUT sample count does not match edge size");
  result.metadata.edge_size = edge;
  result.data = Lut3D(edge, std::move(values));
  return result;
}

std::string cache_key(const ProfessionalLut& lut, LutInterpolation interpolation,
                      LutGpuBackend backend, LutPrecision precision) {
  return lut.metadata.id + ':' + std::to_string(lut.metadata.content_hash) + ':' +
         std::to_string(static_cast<int>(interpolation)) + ':' +
         std::to_string(static_cast<int>(backend)) + ':' +
         std::to_string(static_cast<int>(precision));
}

}  // namespace

ProfessionalLutEngine::ProfessionalLutEngine(ProfessionalLutCallbacks callbacks,
                                             std::size_t gpu_cache_capacity)
    : callbacks_(std::move(callbacks)),
      cache_capacity_(std::max<std::size_t>(1, gpu_cache_capacity)) {}

ProfessionalLutEngine::~ProfessionalLutEngine() { clear_gpu_cache(); }

DigitorResult ProfessionalLutEngine::load_text(std::string id,
                                                ProfessionalLutFormat format,
                                                std::string_view text,
                                                std::string* diagnostic) {
  if (id.empty() || text.empty() || format == ProfessionalLutFormat::unknown) {
    telemetry_.last_error = "LUT id, format and content are required";
    ++telemetry_.rejected_luts;
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  try {
    ProfessionalLut lut;
    if (format == ProfessionalLutFormat::cube) {
      std::istringstream stream{std::string(text)};
      auto parsed = parse_cube(stream);
      lut.metadata.id = id;
      lut.metadata.title = id;
      lut.metadata.format = format;
      lut.metadata.content_hash = fnv1a(text);
      if (parsed.one_dimensional) {
        lut.metadata.one_dimensional = true;
        lut.metadata.edge_size = static_cast<std::uint32_t>(parsed.one_dimensional->values().size());
        lut.data = std::move(*parsed.one_dimensional);
      } else if (parsed.three_dimensional) {
        lut.metadata.edge_size = static_cast<std::uint32_t>(parsed.three_dimensional->size());
        lut.data = std::move(*parsed.three_dimensional);
      } else {
        throw std::runtime_error("Cube parser produced no LUT");
      }
    } else {
      lut = parse_numeric_grid(id, format, text);
    }
    remove(id);
    luts_.emplace(id, std::move(lut));
    ++telemetry_.loaded_luts;
    telemetry_.last_error.clear();
    set_diagnostic(diagnostic, {});
    return DIGITOR_RESULT_OK;
  } catch (const std::exception& error) {
    ++telemetry_.rejected_luts;
    telemetry_.last_error = format_name(format) + ": " + error.what();
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
}

DigitorResult ProfessionalLutEngine::remove(const std::string& id) {
  const auto erased = luts_.erase(id);
  for (auto it = gpu_cache_.begin(); it != gpu_cache_.end();) {
    if (it->first.rfind(id + ':', 0) == 0) {
      if (callbacks_.release_gpu) callbacks_.release_gpu(it->second.resource);
      it = gpu_cache_.erase(it);
    } else {
      ++it;
    }
  }
  return erased ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
}

const ProfessionalLut* ProfessionalLutEngine::find(const std::string& id) const noexcept {
  const auto found = luts_.find(id);
  return found == luts_.end() ? nullptr : &found->second;
}

std::vector<ProfessionalLutMetadata> ProfessionalLutEngine::inventory() const {
  std::vector<ProfessionalLutMetadata> result;
  result.reserve(luts_.size());
  for (const auto& [_, lut] : luts_) result.push_back(lut.metadata);
  return result;
}

DigitorResult ProfessionalLutEngine::apply(const std::string& id,
                                            std::span<const Color> source,
                                            std::span<Color> destination,
                                            LutInterpolation interpolation,
                                            bool require_gpu,
                                            LutGpuBackend backend,
                                            LutPrecision precision,
                                            std::string* diagnostic) {
  const auto* lut = find(id);
  if (!lut || source.empty() || source.size() != destination.size()) {
    telemetry_.last_error = "valid LUT and equal non-empty pixel spans are required";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  for (const auto& pixel : source) {
    if (!finite(pixel)) {
      telemetry_.last_error = "non-finite LUT input pixel";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }

  if (callbacks_.upload_gpu && callbacks_.apply_gpu) {
    const auto key = cache_key(*lut, interpolation, backend, precision);
    auto found = gpu_cache_.find(key);
    if (found == gpu_cache_.end()) {
      LutGpuResource resource;
      resource.backend = backend;
      resource.precision = precision;
      resource.cache_key = key;
      resource.edge_size = lut->metadata.edge_size;
      std::string error;
      const auto uploaded = callbacks_.upload_gpu(*lut, interpolation, precision, resource, error);
      if (uploaded == DIGITOR_RESULT_OK) {
        ++telemetry_.gpu_uploads;
        found = gpu_cache_.emplace(key, CachedGpu{std::move(resource), ++stamp_}).first;
        while (gpu_cache_.size() > cache_capacity_) {
          auto victim = std::min_element(gpu_cache_.begin(), gpu_cache_.end(),
              [](const auto& a, const auto& b) { return a.second.use_stamp < b.second.use_stamp; });
          if (callbacks_.release_gpu) callbacks_.release_gpu(victim->second.resource);
          gpu_cache_.erase(victim);
        }
      } else if (require_gpu) {
        telemetry_.last_error = error.empty() ? "GPU LUT upload failed" : error;
        set_diagnostic(diagnostic, telemetry_.last_error);
        return uploaded;
      }
    } else {
      ++telemetry_.cache_hits;
      found->second.use_stamp = ++stamp_;
    }
    if (found != gpu_cache_.end()) {
      std::string error;
      const auto applied = callbacks_.apply_gpu(found->second.resource, source, destination, error);
      if (applied == DIGITOR_RESULT_OK) {
        telemetry_.gpu_pixels += source.size();
        set_diagnostic(diagnostic, {});
        return DIGITOR_RESULT_OK;
      }
      if (require_gpu) {
        telemetry_.last_error = error.empty() ? "GPU LUT application failed" : error;
        set_diagnostic(diagnostic, telemetry_.last_error);
        return applied;
      }
    }
  } else if (require_gpu) {
    telemetry_.last_error = "GPU LUT callbacks are unavailable";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  if (std::holds_alternative<Lut1D>(lut->data))
    apply_lut_cpu(source.data(), destination.data(), source.size(),
                  std::get<Lut1D>(lut->data), interpolation);
  else
    apply_lut_cpu(source.data(), destination.data(), source.size(),
                  std::get<Lut3D>(lut->data), interpolation);
  telemetry_.cpu_pixels += source.size();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

void ProfessionalLutEngine::clear_gpu_cache() noexcept {
  if (callbacks_.release_gpu) {
    for (auto& [_, entry] : gpu_cache_) callbacks_.release_gpu(entry.resource);
  }
  gpu_cache_.clear();
}

ProductionGpuScopes::ProductionGpuScopes(GpuScopeCallbacks callbacks,
                                         bool allow_cpu_reference)
    : callbacks_(std::move(callbacks)),
      allow_cpu_reference_(allow_cpu_reference) {}

DigitorResult ProductionGpuScopes::generate(
    const ProfessionalColorManagement& color,
    std::span<const Color> working_pixels, std::uint32_t width,
    std::uint32_t height, const ScopeConfig& config, ScopeResult& result,
    bool require_gpu, std::string* diagnostic) {
  if (working_pixels.empty() || width == 0u || height == 0u ||
      static_cast<std::size_t>(width) * height != working_pixels.size()) {
    telemetry_.last_error = "scope dimensions must match the working pixel span";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (callbacks_.dispatch) {
    std::string error;
    const auto status = callbacks_.dispatch(working_pixels, width, height, config, result, error);
    if (status == DIGITOR_RESULT_OK) {
      ++telemetry_.scope_dispatches;
      set_diagnostic(diagnostic, {});
      return DIGITOR_RESULT_OK;
    }
    if (require_gpu || !allow_cpu_reference_) {
      telemetry_.last_error = error.empty() ? "GPU scope dispatch failed" : error;
      set_diagnostic(diagnostic, telemetry_.last_error);
      return status;
    }
  } else if (require_gpu || !allow_cpu_reference_) {
    telemetry_.last_error = "GPU scope backend is unavailable";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  ++telemetry_.scope_fallbacks;
  return color.generate_scopes(working_pixels, width, height, config, result, diagnostic);
}

}  // namespace digitor
