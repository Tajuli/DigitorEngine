#include "digitor/imported_plugin_physical_qualification.hpp"

#include <iomanip>
#include <sstream>

namespace digitor {
namespace {

const char* state_name(ImportedPluginQualificationState state) noexcept {
  switch (state) {
    case ImportedPluginQualificationState::unqualified: return "UNQUALIFIED";
    case ImportedPluginQualificationState::passed: return "PASSED";
    case ImportedPluginQualificationState::failed: return "FAILED";
  }
  return "UNQUALIFIED";
}

bool safe_field(std::string_view value) noexcept {
  return !value.empty() && value.size() <= 512 &&
         value.find('\n') == std::string_view::npos &&
         value.find('\r') == std::string_view::npos;
}

}  // namespace

bool validate_imported_plugin_physical_metadata(
    const ImportedPluginPhysicalQualificationMetadata& metadata,
    std::string* diagnostic) noexcept {
  if (!safe_field(metadata.engine_version) ||
      !safe_field(metadata.operating_system) ||
      !safe_field(metadata.device_name) ||
      !safe_field(metadata.driver_version) ||
      !safe_field(metadata.run_id)) {
    if (diagnostic) *diagnostic = "physical qualification metadata is incomplete or unsafe";
    return false;
  }
  if (diagnostic) diagnostic->clear();
  return true;
}

bool imported_plugin_physical_report_is_releasable(
    const ImportedPluginPhysicalQualificationReport& report,
    std::string* diagnostic) noexcept {
  std::string local;
  if (!validate_imported_plugin_physical_metadata(report.metadata, &local)) {
    if (diagnostic) *diagnostic = local;
    return false;
  }
  const auto evaluated = qualify_imported_plugin_release(report.evidence);
  if (evaluated.state != ImportedPluginQualificationState::passed ||
      report.result.state != ImportedPluginQualificationState::passed) {
    if (diagnostic) *diagnostic = evaluated.diagnostic.empty()
        ? "physical imported-plugin evidence is not release-qualified"
        : evaluated.diagnostic;
    return false;
  }
  if (diagnostic) diagnostic->clear();
  return true;
}

std::string serialize_imported_plugin_physical_report(
    const ImportedPluginPhysicalQualificationReport& report) {
  std::ostringstream out;
  out << std::setprecision(17);
  out << "DIGITOR_IMPORTED_PLUGIN_QUALIFICATION_V1\n";
  out << "engine_version=" << report.metadata.engine_version << '\n';
  out << "operating_system=" << report.metadata.operating_system << '\n';
  out << "device_name=" << report.metadata.device_name << '\n';
  out << "driver_version=" << report.metadata.driver_version << '\n';
  out << "run_id=" << report.metadata.run_id << '\n';
  out << "plugin_id=" << report.evidence.plugin_id << '\n';
  out << "plugin_version=" << report.evidence.plugin_version << '\n';
  out << "package_identity=" << report.evidence.package_identity << '\n';
  out << "backend=" << static_cast<unsigned>(report.evidence.backend) << '\n';
  out << "format=" << static_cast<unsigned>(report.evidence.format) << '\n';
  out << "physical_gpu=" << (report.evidence.physical_gpu ? 1 : 0) << '\n';
  out << "software_adapter=" << (report.evidence.software_adapter ? 1 : 0) << '\n';
  out << "preview_frames=" << report.evidence.preview_frames << '\n';
  out << "export_frames=" << report.evidence.export_frames << '\n';
  out << "compared_pixels=" << report.evidence.compared_pixels << '\n';
  out << "rmse=" << report.evidence.rmse << '\n';
  out << "max_absolute_error=" << report.evidence.max_absolute_error << '\n';
  out << "alpha_mismatches=" << report.evidence.alpha_mismatches << '\n';
  out << "stack_mismatches=" << report.evidence.preview_export_stack_mismatches << '\n';
  out << "package_mismatches=" << report.evidence.package_identity_mismatches << '\n';
  out << "cpu_readbacks=" << report.evidence.cpu_readbacks << '\n';
  out << "cpu_uploads=" << report.evidence.cpu_uploads << '\n';
  out << "fallback_dispatches=" << report.evidence.fallback_dispatches << '\n';
  out << "device_loss_cycles=" << report.evidence.device_loss_cycles << '\n';
  out << "soak_frames=" << report.evidence.soak_frames << '\n';
  out << "preview_digest=" << report.evidence.preview_visual_stack_digest << '\n';
  out << "export_digest=" << report.evidence.export_visual_stack_digest << '\n';
  out << "state=" << state_name(report.result.state) << '\n';
  out << "diagnostic=" << report.result.diagnostic << '\n';
  return out.str();
}

}  // namespace digitor
