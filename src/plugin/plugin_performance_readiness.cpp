#include "digitor/plugin_performance_readiness.hpp"
#include <algorithm>
#include <cmath>
namespace digitor {
namespace {
bool finite_non_negative(double value) { return std::isfinite(value) && value >= 0.0; }
}
PluginPerformanceReport evaluate_plugin_performance(const PluginPerformanceBudget& budget, const PluginPerformanceSample& sample) {
  PluginPerformanceReport report;
  if (sample.plugin_id.empty() || sample.version.empty() || sample.package_sha256.size() != 64 ||
      !finite_non_negative(sample.cold_compile_ms) || !finite_non_negative(sample.warm_pipeline_ms) ||
      !finite_non_negative(sample.average_frame_ms) || !finite_non_negative(sample.p95_frame_ms)) {
    report.diagnostics.push_back("invalid plugin performance measurement");
    return report;
  }
  const auto cache_total = sample.cache_hits + sample.cache_misses;
  report.cache_hit_ratio = cache_total == 0 ? 0.0 : static_cast<double>(sample.cache_hits) / static_cast<double>(cache_total);
  const double frame_budget = sample.surface == PluginPerformanceSurface::preview ? budget.preview_frame_ms : budget.export_frame_ms;
  if (!sample.deterministic_output) report.diagnostics.push_back("plugin output is not deterministic");
  if (!sample.preview_export_contract_match) report.diagnostics.push_back("preview and export plugin contracts differ");
  if (sample.cold_compile_ms > budget.cold_compile_ms) report.diagnostics.push_back("cold compile budget exceeded");
  if (sample.warm_pipeline_ms > budget.warm_pipeline_ms) report.diagnostics.push_back("pipeline warmup budget exceeded");
  if (sample.average_frame_ms > frame_budget || sample.p95_frame_ms > frame_budget * 1.5) report.diagnostics.push_back("frame-time budget exceeded");
  if (sample.peak_transient_bytes > budget.transient_bytes) report.diagnostics.push_back("transient memory budget exceeded");
  report.cache_ready = cache_total > 0 && report.cache_hit_ratio >= budget.minimum_cache_hit_ratio;
  report.warmup_required = !report.cache_ready || sample.warm_pipeline_ms > budget.warm_pipeline_ms;
  if (!report.diagnostics.empty()) {
    report.decision = PluginPerformanceDecision::budget_exceeded;
  } else if (report.warmup_required) {
    report.decision = PluginPerformanceDecision::warmup_required;
  } else {
    report.decision = PluginPerformanceDecision::ready;
  }
  return report;
}
}
