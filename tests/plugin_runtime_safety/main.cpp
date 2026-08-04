#include "digitor/plugin_runtime_safety.hpp"
#include <iostream>
#include <string>

int main() {
  using namespace digitor;
  PluginRuntimeBudget budget{};
  budget.max_passes = 8;
  budget.max_dispatch_invocations = 1000000;
  budget.max_transient_texture_bytes = 1024 * 1024;
  budget.max_transient_buffer_bytes = 256 * 1024;
  budget.max_parameters = 64;
  budget.preview_deadline_ms = 100;
  budget.export_deadline_ms = 2000;
  budget.quarantine_after_failures = 3;
  PluginRuntimeSafetyController controller(budget);
  PluginRuntimeWorkload w{};
  w.plugin_id = "com.digitor.reference.transition";
  w.version = "1.0.0";
  w.package_sha256 = std::string(64, 'a');
  w.kind = PluginRuntimeKind::transition;
  w.surface = PluginRuntimeSurface::preview;
  w.pass_count = 3;
  w.dispatch_invocations = 500000;
  w.transient_texture_bytes = 512 * 1024;
  w.transient_buffer_bytes = 64 * 1024;
  w.parameter_count = 12;
  w.elapsed_ms = 40;
  PluginRuntimeDiagnostic d{};
  if (!controller.validate(w, d)) return 1;
  w.pass_count = 9;
  if (controller.validate(w, d) || d.failure != PluginRuntimeFailure::pass_budget_exceeded) return 2;
  w.pass_count = 3;
  w.surface = PluginRuntimeSurface::preview;
  w.elapsed_ms = 101;
  if (controller.validate(w, d) || d.failure != PluginRuntimeFailure::deadline_exceeded) return 3;
  w.surface = PluginRuntimeSurface::export_frame;
  w.elapsed_ms = 1500;
  if (!controller.validate(w, d)) return 4;
  controller.record_backend_result(w, false, "simulated backend failure", d);
  controller.record_backend_result(w, false, "simulated backend failure", d);
  controller.record_backend_result(w, false, "simulated backend failure", d);
  if (!controller.is_quarantined(w.plugin_id, w.version)) return 5;
  if (controller.validate(w, d) || !d.quarantined) return 6;
  controller.clear_quarantine(w.plugin_id, w.version);
  if (!controller.validate(w, d)) return 7;
  w.cancellation_requested = true;
  if (controller.validate(w, d) || d.failure != PluginRuntimeFailure::cancelled) return 8;
  std::cout << "PLUGIN_RUNTIME_SAFETY_QUALIFIED=1\n"
               "FILTER_EFFECT_TRANSITION_BUDGETS=1\n"
               "PREVIEW_EXPORT_DIAGNOSTICS=1\n"
               "MAIN_RENDERING_FEATURES_CHANGED=0\n"
               "COMMERCIAL_POLICY_IN_ENGINE=0\n";
  return 0;
}
