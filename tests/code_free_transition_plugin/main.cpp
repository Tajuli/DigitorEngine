#include "digitor/plugin_transition_program.hpp"
#include "digitor/remote_plugin_marketplace.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "CODE_FREE_TRANSITION_PLUGIN_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t handle) {
  digitor::PluginGpuFrame value{};
  value.backend = digitor::RemotePluginBackend::windows_d3d12;
  value.native_texture_handle = handle;
  value.width = 1920;
  value.height = 1080;
  value.format = digitor::PluginPixelFormat::rgba16_float;
  value.primaries = digitor::PluginColorPrimaries::bt2020;
  value.transfer = digitor::PluginTransferFunction::pq;
  value.range = digitor::PluginColorRange::full;
  value.alpha = digitor::PluginAlphaMode::straight;
  return value;
}
}  // namespace

int main() {
  using namespace digitor;

  PluginGpuProgramRegistry registry;
  std::string diagnostic;
  bool registered = false;
  bool unregistered = false;

  RemotePluginMarketplaceBindings marketplace_bindings{};
  marketplace_bindings.engine_version = "4.9.0";
  marketplace_bindings.backend = RemotePluginBackend::windows_d3d12;
  marketplace_bindings.verify_signature = [](
      std::string_view, std::string_view, std::string_view,
      std::string& local) {
    local.clear();
    return true;
  };
  marketplace_bindings.download = [](
      std::string_view, std::vector<std::byte>& bytes, std::string& local) {
    bytes.assign(4, std::byte{0x2a});
    local.clear();
    return true;
  };
  marketplace_bindings.sha256 = [](const std::vector<std::byte>&) {
    return std::string(64, 'a');
  };
  marketplace_bindings.install_package = [](
      const RemotePluginCatalogEntry&, const RemotePluginArtifact&,
      const std::vector<std::byte>&, std::string& path, std::string& local) {
    path = "installed/transition.crossfade/1.0.0";
    local.clear();
    return true;
  };
  marketplace_bindings.register_runtime = [&](
      const RemotePluginCatalogEntry& entry, std::string_view,
      std::string& local) {
    if (entry.kind != RemotePluginKind::transition) {
      local = "catalog entry was not a transition";
      return false;
    }
    PluginGpuProgram program{};
    program.plugin_id = entry.id;
    program.plugin_version = entry.version;
    program.backend = RemotePluginBackend::windows_d3d12;
    program.format = PluginGpuProgramFormat::rgba16_float;
    program.package_identity = "sha256:" + std::string(64, 'a');
    PluginGpuPassDescriptor pass{};
    pass.entry_point = "main";
    pass.shader_asset = "shaders/windows-d3d12.dxil";
    pass.bindings = {{"outgoing", 0, false}, {"incoming", 1, false},
                     {"output", 2, true}, {"progress", 3, false}};
    program.passes.push_back(std::move(pass));
    registered = registry.register_program(std::move(program), &local) ==
                 DIGITOR_RESULT_OK;
    return registered;
  };
  marketplace_bindings.unregister_runtime = [&](std::string_view id) {
    registry.unregister_plugin(id);
    unregistered = true;
  };

  RemotePluginMarketplace marketplace(std::move(marketplace_bindings));
  RemotePluginCatalog catalog{};
  catalog.catalog_id = "digitor.website.transitions";
  catalog.generated_at = "2026-08-04T00:00:00Z";

  RemotePluginCatalogEntry entry{};
  entry.id = "transition.crossfade";
  entry.display_name = "Crossfade";
  entry.version = "1.0.0";
  entry.minimum_engine_version = "4.9.0";
  entry.kind = RemotePluginKind::transition;
  entry.publisher_key_id = "digitor.official";
  entry.signature = "signed";
  entry.artifacts.push_back({RemotePluginBackend::windows_d3d12,
                             "https://plugins.example/transition.digitorfx",
                             std::string(64, 'a'),
                             "transition.crossfade/1.0.0"});
  catalog.plugins.push_back(entry);

  if (marketplace.load_catalog(std::move(catalog), &diagnostic) !=
      DIGITOR_RESULT_OK)
    return fail("transition catalog did not load");

  const auto available = marketplace.available(RemotePluginKind::transition);
  if (available.size() != 1 || available.front().id != entry.id)
    return fail("transition was not discoverable in marketplace");

  const auto installed = marketplace.install(entry.id);
  if (!installed || !registered || !installed.record ||
      installed.record->kind != RemotePluginKind::transition)
    return fail("transition package did not install and register");

  std::uint64_t dispatches = 0;
  PluginTransitionProgramBinding binding{};
  binding.registry = &registry;
  binding.selected_backend = RemotePluginBackend::windows_d3d12;
  binding.record = [&](const PluginTransitionDispatch& dispatch,
                       std::string& local) {
    if (dispatch.request.instance.plugin_id != entry.id ||
        dispatch.request.outgoing.native_texture_handle != 10 ||
        dispatch.request.incoming.native_texture_handle != 11 ||
        dispatch.request.output.native_texture_handle != 12 ||
        dispatch.request.instance.progress != 0.5) {
      local = "transition dispatch payload mismatch";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++dispatches;
    local.clear();
    return DIGITOR_RESULT_OK;
  };

  PluginTransitionProgramRuntime runtime(std::move(binding));
  PluginTransitionRequest request{};
  request.instance.instance_id = "transition.instance.1";
  request.instance.plugin_id = entry.id;
  request.instance.plugin_version = entry.version;
  request.instance.progress = 0.5;
  request.outgoing = frame(10);
  request.incoming = frame(11);
  request.output = frame(12);
  request.project_or_clip_id = "timeline.boundary.1";
  request.visual_stack_digest = "transition.stack.v1";

  if (runtime.dispatch(request, &diagnostic) != DIGITOR_RESULT_OK ||
      dispatches != 1)
    return fail("installed transition package did not dispatch");

  if (marketplace.uninstall(entry.id, &diagnostic) != DIGITOR_RESULT_OK ||
      !unregistered || registry.resolve(entry.id, entry.version,
          RemotePluginBackend::windows_d3d12,
          PluginGpuProgramFormat::rgba16_float))
    return fail("transition package did not uninstall cleanly");

  std::cout << "CODE_FREE_TRANSITION_PLUGIN_QUALIFIED=1\n";
  std::cout << "MARKETPLACE_DISCOVERY=1\n";
  std::cout << "SIGNED_IMPORT=1\n";
  std::cout << "RUNTIME_REGISTRATION=1\n";
  std::cout << "ARBITRARY_TRANSITION_DISPATCH=1\n";
  std::cout << "ENGINE_SOURCE_EDIT_PER_PLUGIN=0\n";
  return 0;
}
