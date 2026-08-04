#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace digitor {

enum class PluginRuntimeKind : std::uint32_t { filter, effect, transition };
enum class PluginRuntimeSurface : std::uint32_t { preview, export_frame };
enum class PluginRuntimeFailure : std::uint32_t {
  none,
  invalid_request,
  pass_budget_exceeded,
  dispatch_budget_exceeded,
  texture_budget_exceeded,
  buffer_budget_exceeded,
  parameter_budget_exceeded,
  deadline_exceeded,
  cancelled,
  backend_failure,
  quarantined
};

struct PluginRuntimeBudget {
  std::uint32_t max_passes = 32;
  std::uint64_t max_dispatch_invocations = 268435456;
  std::uint64_t max_transient_texture_bytes = 536870912;
  std::uint64_t max_transient_buffer_bytes = 67108864;
  std::uint32_t max_parameters = 256;
  std::uint32_t preview_deadline_ms = 250;
  std::uint32_t export_deadline_ms = 5000;
  std::uint32_t quarantine_after_failures = 3;
};

struct PluginRuntimeWorkload {
  std::string plugin_id;
  std::string version;
  std::string package_sha256;
  PluginRuntimeKind kind = PluginRuntimeKind::filter;
  PluginRuntimeSurface surface = PluginRuntimeSurface::preview;
  std::uint32_t pass_count = 0;
  std::uint64_t dispatch_invocations = 0;
  std::uint64_t transient_texture_bytes = 0;
  std::uint64_t transient_buffer_bytes = 0;
  std::uint32_t parameter_count = 0;
  std::uint32_t elapsed_ms = 0;
  bool cancellation_requested = false;
};

struct PluginRuntimeDiagnostic {
  PluginRuntimeFailure failure = PluginRuntimeFailure::none;
  std::string plugin_id;
  std::string version;
  PluginRuntimeSurface surface = PluginRuntimeSurface::preview;
  std::string code;
  std::string message;
  bool retryable = false;
  bool quarantined = false;
};

class PluginRuntimeSafetyController {
 public:
  explicit PluginRuntimeSafetyController(PluginRuntimeBudget budget = {});

  bool validate(const PluginRuntimeWorkload& workload,
                PluginRuntimeDiagnostic& diagnostic) const;
  void record_backend_result(const PluginRuntimeWorkload& workload,
                             bool success,
                             const std::string& backend_message,
                             PluginRuntimeDiagnostic& diagnostic);
  bool is_quarantined(const std::string& plugin_id,
                      const std::string& version) const;
  void clear_quarantine(const std::string& plugin_id,
                        const std::string& version);
  const PluginRuntimeBudget& budget() const noexcept { return budget_; }

 private:
  static std::string key(const std::string& plugin_id,
                         const std::string& version);
  PluginRuntimeBudget budget_;
  std::unordered_map<std::string, std::uint32_t> consecutive_failures_;
  std::unordered_map<std::string, bool> quarantined_;
};

}  // namespace digitor
