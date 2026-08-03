#include "digitor/consumer_plugin_runtime.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char* message) {
  std::cerr << "CONSUMER_PLUGIN_QUALIFICATION_FAILED=" << message << '\n';
  return 1;
}

}  // namespace

int main() {
  using namespace digitor;

  ConsumerPluginPlan plan = ConsumerPluginPlan::free;
  bool applied = false;
  std::string applied_version;

  RemotePluginMarketplaceBindings marketplace_bindings{};
  marketplace_bindings.engine_version = "5.0.0";
  marketplace_bindings.backend = RemotePluginBackend::windows_d3d12;
  marketplace_bindings.verify_signature = [](auto, auto, auto,
                                              std::string& diagnostic) {
    diagnostic.clear();
    return true;
  };
  marketplace_bindings.verify_entitlement = [](const auto& entry,
                                                const auto& entitlement,
                                                std::string& diagnostic) {
    const bool ok = entry.product_id == entitlement.product_id &&
                    !entitlement.token.empty();
    if (!ok) diagnostic = "entitlement mismatch";
    return ok;
  };
  marketplace_bindings.download = [](auto, std::vector<std::byte>& bytes,
                                      std::string& diagnostic) {
    bytes.assign(16, std::byte{0x2a});
    diagnostic.clear();
    return true;
  };
  marketplace_bindings.sha256 = [](const auto&) {
    return std::string(64, 'a');
  };
  marketplace_bindings.install_package = [](const auto& entry, const auto&,
                                             const auto&, std::string& path,
                                             std::string& diagnostic) {
    path = "/installed/" + entry.id;
    diagnostic.clear();
    return true;
  };
  marketplace_bindings.register_runtime = [](const auto&, auto,
                                               std::string& diagnostic) {
    diagnostic.clear();
    return true;
  };

  RemotePluginMarketplace marketplace(std::move(marketplace_bindings));
  RemotePluginCatalog catalog{};
  catalog.catalog_id = "digitor-official";
  catalog.generated_at = "2026-08-04T00:00:00Z";

  auto make_entry = [](std::string id, RemotePluginTier tier) {
    RemotePluginCatalogEntry entry{};
    entry.id = std::move(id);
    entry.display_name = entry.id;
    entry.version = "1.0.0";
    entry.minimum_engine_version = "5.0.0";
    entry.kind = RemotePluginKind::effect;
    entry.tier = tier;
    entry.product_id = tier == RemotePluginTier::paid ? "product.pro" : "";
    entry.publisher_key_id = "digitor";
    entry.signature = "signed";
    entry.parameters.push_back({"amount", "Amount", 0.0, 1.0, 0.5, true});
    entry.artifacts.push_back({RemotePluginBackend::windows_d3d12,
                               "https://plugins.example/plugin.digitorfx",
                               std::string(64, 'a'), "shaders/effect.dxil"});
    return entry;
  };
  catalog.plugins.push_back(make_entry("effect.free", RemotePluginTier::free));
  catalog.plugins.push_back(make_entry("effect.paid", RemotePluginTier::paid));
  std::string diagnostic;
  if (marketplace.load_catalog(std::move(catalog), &diagnostic) !=
      DIGITOR_RESULT_OK)
    return fail("catalog load failed");

  ConsumerPluginRuntimeBindings consumer_bindings{};
  consumer_bindings.current_plan = [&] { return plan; };
  consumer_bindings.entitlement_for = [](const auto& entry) {
    RemotePluginEntitlement value{};
    value.product_id = entry.product_id;
    value.user_id = "paid-user";
    value.token = "signed-entitlement";
    return std::optional<RemotePluginEntitlement>(value);
  };
  consumer_bindings.apply_instance = [&](const auto& instance, auto,
                                          auto, std::string& diagnostic) {
    applied = true;
    applied_version = instance.plugin_version;
    diagnostic.clear();
    return true;
  };
  ConsumerPluginRuntime runtime(marketplace, std::move(consumer_bindings));

  if (!runtime.import_plugin("effect.free"))
    return fail("free user could not import free plugin");
  if (runtime.import_plugin("effect.paid"))
    return fail("free user imported paid plugin");
  const auto free_apply = runtime.apply("effect.free", "instance.free",
                                        "clip.1", {{"amount", 0.7}},
                                        ConsumerPluginSurface::preview);
  if (!free_apply || !applied || applied_version != "1.0.0")
    return fail("free plugin did not apply through consumer runtime");

  plan = ConsumerPluginPlan::paid;
  if (!runtime.import_plugin("effect.paid"))
    return fail("paid user could not import paid plugin");
  applied = false;
  const auto paid_apply = runtime.apply("effect.paid", "instance.paid",
                                        "clip.1", {},
                                        ConsumerPluginSurface::export_frame);
  if (!paid_apply || !applied || paid_apply.instance->tier != RemotePluginTier::paid)
    return fail("paid plugin did not apply through export runtime");
  if (runtime.apply("effect.paid", "invalid", "clip.1",
                    {{"amount", 2.0}}, ConsumerPluginSurface::preview))
    return fail("out-of-range parameter was accepted");

  std::cout << "CONSUMER_PLUGIN_QUALIFICATION=PASS\n";
  std::cout << "FREE_USER=FREE_ONLY\n";
  std::cout << "PAID_USER=FREE_AND_PAID\n";
  std::cout << "PLUGIN_VERSION_PINNING=PASS\n";
  std::cout << "PREVIEW_EXPORT_BINDING=PASS\n";
  return 0;
}
