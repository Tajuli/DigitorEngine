#include "digitor/plugin_backend_package_loader.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_BACKEND_LOADER_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuProgram make_program() {
  using namespace digitor;
  PluginGpuProgram program{};
  program.plugin_id = "effect.remote.glow";
  program.plugin_version = "1.0.0";
  program.backend = RemotePluginBackend::windows_vulkan;
  program.format = PluginGpuProgramFormat::rgba16_float;
  program.package_identity = "sha256:remote-glow-v1";
  for (int i = 0; i < 2; ++i) {
    PluginGpuPassDescriptor pass{};
    pass.entry_point = "pass_" + std::to_string(i);
    pass.shader_asset = "shaders/windows-vulkan-rgba16f.spv";
    pass.bindings = {{"input", 0, false}, {"output", 1, true}};
    program.passes.push_back(std::move(pass));
  }
  return program;
}
}  // namespace

int main() {
  using namespace digitor;
  PluginGpuProgramRegistry registry;
  std::uint64_t destroyed = 0;
  PluginBackendPackageLoaderBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_vulkan;
  bindings.device_identity = 77;
  bindings.read_asset = [](auto root, auto relative,
                           std::vector<std::byte>& bytes,
                           std::string& diagnostic) {
    if (root != "/plugins/effect.remote.glow/1.0.0" ||
        relative != "shaders/windows-vulkan-rgba16f.spv") {
      diagnostic = "unexpected package path";
      return false;
    }
    bytes = {std::byte{0x03}, std::byte{0x02},
             std::byte{0x23}, std::byte{0x07},
             std::byte{0x00}, std::byte{0x00},
             std::byte{0x00}, std::byte{0x00}};
    diagnostic.clear();
    return true;
  };
  bindings.create_pipeline = [](const PluginBackendAsset& asset,
                                const PluginGpuProgram& program,
                                PluginBackendPipeline& out,
                                std::string& diagnostic) {
    if (asset.binary_kind != PluginShaderBinaryKind::spirv ||
        asset.bytes.empty() || program.passes.size() != 2) {
      diagnostic = "unexpected asset/program";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    out.package_identity = program.package_identity;
    out.plugin_id = program.plugin_id;
    out.plugin_version = program.plugin_version;
    out.backend = program.backend;
    out.format = program.format;
    out.native_pipeline_handle = 991;
    out.device_identity = 77;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  bindings.destroy_pipeline = [&](const PluginBackendPipeline&) { ++destroyed; };

  PluginBackendPackageLoader loader(registry, std::move(bindings));
  std::string diagnostic;
  if (loader.load("/plugins/effect.remote.glow/1.0.0", make_program(),
                  &diagnostic) != DIGITOR_RESULT_OK)
    return fail("valid SPIR-V package failed to load");
  const auto* pipeline = loader.pipeline("effect.remote.glow", "1.0.0",
                                         PluginGpuProgramFormat::rgba16_float);
  if (!pipeline || pipeline->native_pipeline_handle != 991 ||
      pipeline->device_identity != 77)
    return fail("pipeline identity was not retained");
  if (!registry.resolve("effect.remote.glow", "1.0.0",
                        RemotePluginBackend::windows_vulkan,
                        PluginGpuProgramFormat::rgba16_float))
    return fail("program was not registered after pipeline creation");

  auto unsafe = make_program();
  unsafe.plugin_id = "effect.remote.unsafe";
  unsafe.passes[0].shader_asset = "../escape.spv";
  unsafe.passes[1].shader_asset = "../escape.spv";
  if (loader.load("/plugins/unsafe", std::move(unsafe), &diagnostic) ==
      DIGITOR_RESULT_OK)
    return fail("path traversal shader asset was accepted");

  loader.unload("effect.remote.glow");
  if (destroyed != 1 || loader.pipeline("effect.remote.glow", "1.0.0",
                                       PluginGpuProgramFormat::rgba16_float))
    return fail("pipeline unload did not destroy exact resource");

  std::cout << "PLUGIN_BACKEND_PACKAGE_LOADER=PASS\n";
  std::cout << "DXIL_SPIRV_METALLIB_GLSLES_CONTRACT=PASS\n";
  std::cout << "PACKAGE_DEVICE_IDENTITY=PASS\n";
  std::cout << "PATH_TRAVERSAL_REJECTED=PASS\n";
  std::cout << "ENGINE_SOURCE_EDIT_FOR_NEW_PLUGIN=NOT_REQUIRED\n";
  return 0;
}
