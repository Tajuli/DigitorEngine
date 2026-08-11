#pragma once

#include "digitor/production_integration_runtime.hpp"
#include "gpu/backend_production_capability.hpp"

#include <memory>
#include <string>

namespace digitor {
[[nodiscard]] bool engine_production_runtime_supported_platform() noexcept;
[[nodiscard]] std::unique_ptr<ProductionIntegrationRuntime>
install_engine_production_runtime(const BackendProductionCapability& capability,
                                  std::string* diagnostic = nullptr) noexcept;
}  // namespace digitor
