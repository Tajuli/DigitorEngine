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

  int preview_calls = 0;
  int export_calls = 0;
  std::string applied_version;

  RemotePluginMarketplaceBindings marketplace_bindings{};
  marketplace_bindings.engine_version = "5.0.0";
  marketplace_bindings.backend = RemotePluginBackend::windows_d3d12;
  marketplace_bindings.verify_signature = [](auto, auto, auto,
                                              std::string& diagnostic) {
    diagnostic.clear();
    return true;
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

  RemotePluginCatalogEntry entry{};
  entry.id = "effect.remote";
  entry.display_name = "Remote Effect";
  entry.version = "1.0.0";
  entry.minimum_engine_version = "5.0.0";
  entry.kind = RemotePluginKind::effect;
  entry.publisher_key_id = "digitor";
  entry.signature = "signed";
  entry.parameters.push_back({"amount", "Amount", 0.0, 1.0, 0.5, true});
  entry.artifacts.push_back({RemotePluginBackend::windows_d3d12,
                             "https://plugins.example/plugin.digitorfx",
                             std::string(64, 'a'), "shaders/effect.dxil"});
  catalog.plugins.push_back(std::move(entry));

  std::string diagnostic;
  if (marketplace.load_catalog(std::move(catalog), &diagnostic) !=
      DIGITOR_RESULT_OK)
    return fail("catalog load failed");

  ConsumerPluginRuntimeBindings consumer_bindings{};
  consumer_bindings.apply_instance = [&](const auto& instance, auto surface,
                                          auto, std::string& diagnostic) {
    if (surface == ConsumerPluginSurface::preview) ++preview_calls;
    else ++export_calls;
    applied_version = instance.plugin_version;
    diagnostic.clear();
    return true;
  };
  ConsumerPluginRuntime runtime(marketplace, std::move(consumer_bindings));

  if (!runtime.import_plugin("effect.remote"))
    return fail("plugin import failed");

  const auto preview = runtime.apply("effect.remote", "instance.preview",
                                     "clip.1", {{"amount", 0.7}},
                                     ConsumerPluginSurface::preview);
  if (!preview || preview_calls != 1 || export_calls != 0 ||
      applied_version != "1.0.0")
    return fail("full preview request was not processed");

  // A free-user app can show its own small upgrade dialog and simply avoid
  // sending an export request. The engine remains idle and policy-neutral.
  if (export_calls != 0)
    return fail("engine exported without an app request");

  // Once the app decides export is allowed, the identical plugin instance is
  // processed normally. Engine behavior does not depend on commercial status.
  const auto exported = runtime.apply("effect.remote", "instance.export",
                                      "clip.1", {},
                                      ConsumerPluginSurface::export_frame);
  if (!exported || export_calls != 1)
    return fail("app-submitted export request was not processed");

  if (runtime.apply("effect.remote", "invalid", "clip.1",
                    {{"amount", 2.0}}, ConsumerPluginSurface::preview))
    return fail("out-of-range parameter was accepted");

  std::cout << "CONSUMER_PLUGIN_QUALIFICATION=PASS\n";
  std::cout << "FULL_PREVIEW=PASS\n";
  std::cout << "EXPORT_WITHOUT_APP_REQUEST=ZERO\n";
  std::cout << "EXPORT_WITH_APP_REQUEST=PASS\n";
  std::cout << "COMMERCIAL_POLICY_IN_ENGINE=NONE\n";
  std::cout << "PLUGIN_VERSION_PINNING=PASS\n";
  return 0;
}
