#pragma once

#include "digitor/plugin_zero_copy_frame.hpp"

#include <cstdint>
#include <string>

namespace digitor {

enum class ImportedPluginQualificationState : std::uint32_t {
  unqualified,
  passed,
  failed
};

struct ImportedPluginQualificationEvidence final {
  std::string plugin_id;
  std::string plugin_version;
  std::string package_identity;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  PluginPixelFormat format{PluginPixelFormat::rgba8_unorm};
  bool physical_gpu{};
  bool software_adapter{};
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t compared_pixels{};
  double rmse{};
  double max_absolute_error{};
  std::uint64_t alpha_mismatches{};
  std::uint64_t preview_export_stack_mismatches{};
  std::uint64_t package_identity_mismatches{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t fallback_dispatches{};
  std::uint64_t device_loss_cycles{};
  std::uint64_t soak_frames{};
  std::string preview_visual_stack_digest;
  std::string export_visual_stack_digest;
};

struct ImportedPluginQualificationResult final {
  ImportedPluginQualificationState state{ImportedPluginQualificationState::unqualified};
  std::string diagnostic;
};

[[nodiscard]] ImportedPluginQualificationResult
qualify_imported_plugin_release(
    const ImportedPluginQualificationEvidence& evidence) noexcept;

}  // namespace digitor
