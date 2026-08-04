#pragma once

#include "digitor/digitor.h"
#include "digitor/remote_plugin_marketplace.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

struct PluginParityParameter final {
  std::string id;
  std::vector<double> values;
};

struct PluginParityInvocation final {
  RemotePluginKind kind{RemotePluginKind::effect};
  std::string plugin_id;
  std::string plugin_version;
  std::string package_sha256;
  std::vector<PluginParityParameter> parameters;
  std::string input_color_digest;
  std::string output_color_digest;
  std::uint64_t timeline_time_ns{};
  double transition_progress{};
};

struct PluginParityResult final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  std::string canonical_digest;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] PluginParityResult qualify_plugin_preview_export_parity(
    const PluginParityInvocation& preview,
    const PluginParityInvocation& export_frame) noexcept;

}  // namespace digitor
