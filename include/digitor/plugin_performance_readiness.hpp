#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace digitor {
enum class PluginPerformanceSurface : std::uint32_t { preview, export_frame };
enum class PluginPerformanceDecision : std::uint32_t { ready, warmup_required, budget_exceeded, invalid_measurement };
struct PluginPerformanceBudget {
  double cold_compile_ms = 500.0;
  double warm_pipeline_ms = 50.0;
  double preview_frame_ms = 16.667;
  double export_frame_ms = 33.333;
  std::uint64_t transient_bytes = 256ull * 1024ull * 1024ull;
  double minimum_cache_hit_ratio = 0.90;
};
struct PluginPerformanceSample {
  std::string plugin_id;
  std::string version;
  std::string package_sha256;
  PluginPerformanceSurface surface = PluginPerformanceSurface::preview;
  double cold_compile_ms = 0.0;
  double warm_pipeline_ms = 0.0;
  double average_frame_ms = 0.0;
  double p95_frame_ms = 0.0;
  std::uint64_t peak_transient_bytes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t cache_misses = 0;
  bool deterministic_output = true;
  bool preview_export_contract_match = true;
};
struct PluginPerformanceReport {
  PluginPerformanceDecision decision = PluginPerformanceDecision::invalid_measurement;
  bool cache_ready = false;
  bool warmup_required = false;
  double cache_hit_ratio = 0.0;
  std::vector<std::string> diagnostics;
};
PluginPerformanceReport evaluate_plugin_performance(const PluginPerformanceBudget&, const PluginPerformanceSample&);
}
