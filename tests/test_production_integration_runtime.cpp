#include "digitor/production_integration_runtime.hpp"
#include <cassert>
int main() {
  using namespace digitor;
  const auto platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  assert(!flutter_production_provider_builder_installed(platform));
  auto first = ProductionIntegrationRuntime::install(platform,
      [](const FlutterProductionPluginAttachment&, std::string& local)
          -> std::optional<FlutterProductionProviderBuild> {
        local = "fixture unavailable"; return std::nullopt;
      });
  assert(first && first->active());
  const auto generation = first->generation();
  assert(flutter_production_provider_builder_installed(platform));
  assert(first->shutdown() == DIGITOR_RESULT_OK);
  assert(!flutter_production_provider_builder_installed(platform));
  auto second = ProductionIntegrationRuntime::install(platform,
      [](const FlutterProductionPluginAttachment&, std::string&)
          -> std::optional<FlutterProductionProviderBuild> { return std::nullopt; });
  assert(second && second->generation() > generation);
  assert(second->shutdown() == DIGITOR_RESULT_OK);
  return 0;
}
