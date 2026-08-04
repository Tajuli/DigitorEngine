#include "digitor/plugin_runtime_safety.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace digitor {
namespace {

bool valid_token(const std::string& value) {
  if (value.empty() || value.size() > 160) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '-' || c == '_';
  });
}

bool valid_sha256(const std::string& value) {
  if (value.size() != 64) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

void fail(const PluginRuntimeWorkload& workload,
          PluginRuntimeFailure failure,
          const std::string& code,
          const std::string& message,
          bool retryable,
          bool quarantined,
          PluginRuntimeDiagnostic& diagnostic) {
  diagnostic.failure = failure;
  diagnostic.plugin_id = workload.plugin_id;
  diagnostic.version = workload.version;
  diagnostic.surface = workload.surface;
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.retryable = retryable;
  diagnostic.quarantined = quarantined;
}

}  // namespace

PluginRuntimeSafetyController::PluginRuntimeSafetyController(PluginRuntimeBudget budget)
    : budget_(std::move(budget)) {
  budget_.max_passes = std::max<std::uint32_t>(1, budget_.max_passes);
  budget_.max_dispatch_invocations = std::max<std::uint64_t>(1, budget_.max_dispatch_invocations);
  budget_.max_transient_texture_bytes = std::max<std::uint64_t>(1, budget_.max_transient_texture_bytes);
  budget_.max_transient_buffer_bytes = std::max<std::uint64_t>(1, budget_.max_transient_buffer_bytes);
  budget_.max_parameters = std::max<std::uint32_t>(1, budget_.max_parameters);
  budget_.preview_deadline_ms = std::max<std::uint32_t>(1, budget_.preview_deadline_ms);
  budget_.export_deadline_ms = std::max<std::uint32_t>(1, budget_.export_deadline_ms);
  budget_.quarantine_after_failures = std::max<std::uint32_t>(1, budget_.quarantine_after_failures);
}

std::string PluginRuntimeSafetyController::key(const std::string& plugin_id,
                                                const std::string& version) {
  return plugin_id + "@" + version;
}

bool PluginRuntimeSafetyController::validate(const PluginRuntimeWorkload& workload,
                                             PluginRuntimeDiagnostic& diagnostic) const {
  diagnostic = {};
  if (is_quarantined(workload.plugin_id, workload.version)) {
    fail(workload, PluginRuntimeFailure::quarantined, "PLUGIN_QUARANTINED",
         "Plugin version is disabled after repeated execution failures.", false, true, diagnostic);
    return false;
  }
  if (!valid_token(workload.plugin_id) || !valid_token(workload.version) ||
      !valid_sha256(workload.package_sha256) || workload.pass_count == 0) {
    fail(workload, PluginRuntimeFailure::invalid_request, "INVALID_PLUGIN_WORKLOAD",
         "Plugin identity, package hash or workload shape is invalid.", false, false, diagnostic);
    return false;
  }
  if (workload.pass_count > budget_.max_passes) {
    fail(workload, PluginRuntimeFailure::pass_budget_exceeded, "PASS_BUDGET_EXCEEDED",
         "Plugin pass count exceeds the configured budget.", false, false, diagnostic);
    return false;
  }
  if (workload.dispatch_invocations > budget_.max_dispatch_invocations) {
    fail(workload, PluginRuntimeFailure::dispatch_budget_exceeded, "DISPATCH_BUDGET_EXCEEDED",
         "Plugin dispatch size exceeds the configured budget.", false, false, diagnostic);
    return false;
  }
  if (workload.transient_texture_bytes > budget_.max_transient_texture_bytes) {
    fail(workload, PluginRuntimeFailure::texture_budget_exceeded, "TEXTURE_BUDGET_EXCEEDED",
         "Plugin transient texture use exceeds the configured budget.", true, false, diagnostic);
    return false;
  }
  if (workload.transient_buffer_bytes > budget_.max_transient_buffer_bytes) {
    fail(workload, PluginRuntimeFailure::buffer_budget_exceeded, "BUFFER_BUDGET_EXCEEDED",
         "Plugin transient buffer use exceeds the configured budget.", true, false, diagnostic);
    return false;
  }
  if (workload.parameter_count > budget_.max_parameters) {
    fail(workload, PluginRuntimeFailure::parameter_budget_exceeded, "PARAMETER_BUDGET_EXCEEDED",
         "Plugin parameter count exceeds the configured budget.", false, false, diagnostic);
    return false;
  }
  if (workload.cancellation_requested) {
    fail(workload, PluginRuntimeFailure::cancelled, "PLUGIN_EXECUTION_CANCELLED",
         "Plugin execution was cancelled by the host app.", true, false, diagnostic);
    return false;
  }
  const auto deadline = workload.surface == PluginRuntimeSurface::preview
                            ? budget_.preview_deadline_ms
                            : budget_.export_deadline_ms;
  if (workload.elapsed_ms > deadline) {
    fail(workload, PluginRuntimeFailure::deadline_exceeded, "PLUGIN_DEADLINE_EXCEEDED",
         "Plugin execution exceeded the configured surface deadline.", true, false, diagnostic);
    return false;
  }
  return true;
}

void PluginRuntimeSafetyController::record_backend_result(
    const PluginRuntimeWorkload& workload,
    bool success,
    const std::string& backend_message,
    PluginRuntimeDiagnostic& diagnostic) {
  diagnostic = {};
  const auto runtime_key = key(workload.plugin_id, workload.version);
  if (success) {
    consecutive_failures_.erase(runtime_key);
    quarantined_.erase(runtime_key);
    return;
  }
  const auto count = ++consecutive_failures_[runtime_key];
  const bool disabled = count >= budget_.quarantine_after_failures;
  if (disabled) quarantined_[runtime_key] = true;
  fail(workload,
       disabled ? PluginRuntimeFailure::quarantined : PluginRuntimeFailure::backend_failure,
       disabled ? "PLUGIN_QUARANTINED" : "PLUGIN_BACKEND_FAILURE",
       backend_message.empty() ? "Plugin backend execution failed." : backend_message,
       !disabled, disabled, diagnostic);
}

bool PluginRuntimeSafetyController::is_quarantined(const std::string& plugin_id,
                                                    const std::string& version) const {
  const auto found = quarantined_.find(key(plugin_id, version));
  return found != quarantined_.end() && found->second;
}

void PluginRuntimeSafetyController::clear_quarantine(const std::string& plugin_id,
                                                      const std::string& version) {
  const auto runtime_key = key(plugin_id, version);
  quarantined_.erase(runtime_key);
  consecutive_failures_.erase(runtime_key);
}

}  // namespace digitor
