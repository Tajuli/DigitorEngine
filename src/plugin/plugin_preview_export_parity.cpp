#include "digitor/plugin_preview_export_parity.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>

namespace digitor {
namespace {

bool valid_token(std::string_view value) noexcept {
  if (value.empty() || value.size() > 160) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool valid_sha256(std::string_view value) noexcept {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
         });
}

std::string canonical(const PluginParityInvocation& value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << static_cast<std::uint32_t>(value.kind) << '\n'
      << value.plugin_id << '\n' << value.plugin_version << '\n'
      << value.package_sha256 << '\n' << value.input_color_digest << '\n'
      << value.output_color_digest << '\n' << value.timeline_time_ns << '\n'
      << std::setprecision(17) << value.transition_progress << '\n';
  for (const auto& parameter : value.parameters) {
    out << parameter.id;
    for (double component : parameter.values) out << '|' << component;
    out << '\n';
  }
  return out.str();
}

bool valid(const PluginParityInvocation& value, std::string& diagnostic) {
  if (!valid_token(value.plugin_id) || !valid_token(value.plugin_version) ||
      !valid_sha256(value.package_sha256) || value.input_color_digest.empty() ||
      value.output_color_digest.empty() || value.parameters.size() > 128) {
    diagnostic = "plugin parity invocation metadata is invalid";
    return false;
  }
  if (value.kind == RemotePluginKind::transition &&
      (value.transition_progress < 0.0 || value.transition_progress > 1.0)) {
    diagnostic = "transition progress is outside the normalized range";
    return false;
  }
  for (const auto& parameter : value.parameters) {
    if (!valid_token(parameter.id) || parameter.values.empty() ||
        parameter.values.size() > 16) {
      diagnostic = "plugin parity parameter metadata is invalid";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

}  // namespace

PluginParityResult qualify_plugin_preview_export_parity(
    const PluginParityInvocation& preview,
    const PluginParityInvocation& export_frame) noexcept {
  PluginParityResult out{};
  try {
    if (!valid(preview, out.diagnostic) || !valid(export_frame, out.diagnostic))
      return out;
    const auto preview_digest = canonical(preview);
    const auto export_digest = canonical(export_frame);
    if (preview_digest != export_digest) {
      out.result = DIGITOR_RESULT_UNSUPPORTED;
      out.diagnostic =
          "preview and export plugin invocations are not pipeline-identical";
      return out;
    }
    out.result = DIGITOR_RESULT_OK;
    out.canonical_digest = preview_digest;
    out.diagnostic.clear();
    return out;
  } catch (...) {
    out.result = DIGITOR_RESULT_INTERNAL_ERROR;
    out.diagnostic = "plugin parity qualification failed at exception boundary";
    return out;
  }
}

}  // namespace digitor
