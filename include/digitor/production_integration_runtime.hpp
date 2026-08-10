#pragma once

#include "digitor/flutter_production_provider_builder.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace digitor {

// Engine-owned lease for the process-wide Flutter provider builder.  The
// installed callback captures only weak state: even if orderly uninstallation
// is prevented by a live Flutter session, it can never dereference a destroyed
// backend/runtime.
class ProductionIntegrationRuntime final {
 public:
  static std::unique_ptr<ProductionIntegrationRuntime> install(
      DigitorFlutterProductionPluginPlatform platform,
      FlutterProductionProviderBuildFactory factory,
      std::string* diagnostic = nullptr) noexcept;
  ~ProductionIntegrationRuntime();

  ProductionIntegrationRuntime(const ProductionIntegrationRuntime&) = delete;
  ProductionIntegrationRuntime& operator=(const ProductionIntegrationRuntime&) = delete;

  [[nodiscard]] std::uint64_t generation() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  DigitorResult shutdown() noexcept;

 private:
  struct State;
  ProductionIntegrationRuntime(DigitorFlutterProductionPluginPlatform,
                               std::shared_ptr<State>);
  DigitorFlutterProductionPluginPlatform platform_;
  std::shared_ptr<State> state_;
};

}  // namespace digitor
