#pragma once

#include "digitor/production_encoder_factory.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct DigitorNativeGpuTextureDescriptor;

namespace digitor {
namespace windows_unified_export_detail {

using TextureDescriptorBuilder = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, std::uint64_t,
    DigitorNativeGpuTextureDescriptor&, std::string&)>;

class MediaSourcePathState final {
 public:
  void set(std::string value);
  [[nodiscard]] std::string get() const;

 private:
  mutable std::mutex mutex_;
  std::string path_;
};

}  // namespace windows_unified_export_detail

[[nodiscard]] ProductionEncoderFactory make_windows_unified_export_factory(
    windows_unified_export_detail::TextureDescriptorBuilder descriptor_builder,
    std::function<std::string()> source_media_path_getter);

}  // namespace digitor
