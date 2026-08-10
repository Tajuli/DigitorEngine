#include "digitor/production_integration_runtime.hpp"

#include <utility>

namespace digitor {
namespace {
std::atomic_uint64_t next_generation{1};
}

struct ProductionIntegrationRuntime::State {
  std::atomic_bool active{true};
  std::uint64_t generation{next_generation.fetch_add(1)};
  FlutterProductionProviderBuildFactory factory;
};

ProductionIntegrationRuntime::ProductionIntegrationRuntime(
    DigitorFlutterProductionPluginPlatform platform, std::shared_ptr<State> state)
    : platform_(platform), state_(std::move(state)) {}

std::unique_ptr<ProductionIntegrationRuntime> ProductionIntegrationRuntime::install(
    DigitorFlutterProductionPluginPlatform platform,
    FlutterProductionProviderBuildFactory factory,
    std::string* diagnostic) noexcept {
  if (!factory) {
    if (diagnostic) *diagnostic = "production provider factory is required";
    return {};
  }
  try {
    auto state = std::make_shared<State>();
    state->factory = std::move(factory);
    std::weak_ptr<State> weak = state;
    const auto result = install_flutter_production_provider_builder(
        platform, [weak](const FlutterProductionPluginAttachment& attachment,
                         std::string& local)
            -> std::optional<FlutterProductionProviderBuild> {
          const auto state = weak.lock();
          if (!state || !state->active.load(std::memory_order_acquire)) {
            local = "engine production runtime generation is no longer active";
            return std::nullopt;
          }
          return state->factory(attachment, local);
        }, diagnostic);
    if (result != DIGITOR_RESULT_OK) return {};
    return std::unique_ptr<ProductionIntegrationRuntime>(
        new ProductionIntegrationRuntime(platform, std::move(state)));
  } catch (...) {
    if (diagnostic) *diagnostic = "failed to allocate production runtime lease";
    return {};
  }
}

ProductionIntegrationRuntime::~ProductionIntegrationRuntime() { (void)shutdown(); }

std::uint64_t ProductionIntegrationRuntime::generation() const noexcept {
  return state_ ? state_->generation : 0;
}

bool ProductionIntegrationRuntime::active() const noexcept {
  return state_ && state_->active.load(std::memory_order_acquire);
}

DigitorResult ProductionIntegrationRuntime::shutdown() noexcept {
  if (!state_) return DIGITOR_RESULT_OK;
  state_->active.store(false, std::memory_order_release);
  const auto result = uninstall_flutter_production_provider_builder(platform_);
  if (result == DIGITOR_RESULT_OK) state_.reset();
  return result;
}

}  // namespace digitor
