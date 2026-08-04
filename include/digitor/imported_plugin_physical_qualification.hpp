#pragma once

#include "digitor/imported_plugin_qualification_runner.hpp"

#include <string>
#include <string_view>

namespace digitor {

struct ImportedPluginPhysicalQualificationMetadata final {
  std::string engine_version;
  std::string operating_system;
  std::string device_name;
  std::string driver_version;
  std::string run_id;
};

struct ImportedPluginPhysicalQualificationReport final {
  ImportedPluginPhysicalQualificationMetadata metadata;
  ImportedPluginQualificationEvidence evidence;
  ImportedPluginQualificationResult result;
};

[[nodiscard]] bool validate_imported_plugin_physical_metadata(
    const ImportedPluginPhysicalQualificationMetadata& metadata,
    std::string* diagnostic = nullptr) noexcept;

[[nodiscard]] std::string serialize_imported_plugin_physical_report(
    const ImportedPluginPhysicalQualificationReport& report);

[[nodiscard]] bool imported_plugin_physical_report_is_releasable(
    const ImportedPluginPhysicalQualificationReport& report,
    std::string* diagnostic = nullptr) noexcept;

}  // namespace digitor
