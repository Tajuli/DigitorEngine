#include "digitor/flutter_production_c_api.h"
#include "digitor/flutter_production_plugin_bootstrap.hpp"
#include "platform/android/android_engine_production.hpp"
#include "platform/apple/apple_engine_production.hpp"
#include "platform/windows/windows_engine_production.hpp"

#include <cassert>
#include <string>

using namespace digitor;

int main() {
  int context = 1;
  int device = 2;
  int queue = 3;
  int instance = 4;
  int physical = 5;
  int registrar = 6;

  BackendProductionCapability windows{};
  windows.backend = DIGITOR_RENDERER_D3D12;
  windows.context_identity = 101;
  windows.frame_context_identity = &context;
  windows.resources = D3D12ProductionResources{&device, &queue};
  windows.native_media_import =
      [](const ZeroCopyImportRequest&, ProcessedGpuFramePtr& frame) {
        frame.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  windows.native_preview_descriptor = [](
                                          const ProcessedGpuFramePtr&,
                                          std::uint64_t,
                                          DigitorNativeGpuTextureDescriptor&,
                                          std::string&) {
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  };
  windows.native_preview_capabilities.struct_size =
      sizeof(windows.native_preview_capabilities);
  windows.native_preview_capabilities.api_version =
      DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
  windows.native_preview_capabilities.native_gpu_preview_available = 1;
  windows.native_preview_capabilities.true_shared_resource_zero_copy = 1;
  windows.native_preview_capabilities.gpu_to_gpu_copy = 1;
  windows.native_preview_capabilities.sdr_supported = 1;
  windows.native_preview_capabilities.backend =
      DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
  windows.native_preview_capabilities.handle_type =
      DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE;
  windows.native_preview_capabilities.selected_mode =
      DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT;
  assert(windows.valid());

  FlutterProductionPluginAttachment preview_attachment{};
  preview_attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  preview_attachment.flutter_texture_registrar = &registrar;
  preview_attachment.implementation_identity = "test.flutter.windows.preview";
  std::string preview_diagnostic;
  assert(validate_windows_d3d12_preview_build_inputs(
      windows, preview_attachment, preview_diagnostic));
  assert(preview_diagnostic.empty());

  const auto expect_preview_failure = [&](BackendProductionCapability value,
                                          const char* expected) {
    preview_diagnostic.clear();
    assert(!validate_windows_d3d12_preview_build_inputs(
        value, preview_attachment, preview_diagnostic));
    assert(preview_diagnostic == expected);
  };
  auto missing = windows;
  missing.native_media_import = {};
  expect_preview_failure(missing,
                         "D3D12 native_media_import callback is missing");
  missing = windows;
  missing.native_preview_descriptor = {};
  expect_preview_failure(
      missing, "D3D12 native_preview_descriptor callback is missing");
  missing = windows;
  missing.native_preview_capabilities.native_gpu_preview_available = 0;
  expect_preview_failure(
      missing, "D3D12 native GPU preview capability is disabled");
  missing = windows;
  missing.native_preview_capabilities.backend =
      DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN;
  expect_preview_failure(missing,
                         "D3D12 preview backend capability mismatch");
  missing = windows;
  missing.native_preview_capabilities.handle_type =
      DIGITOR_NATIVE_TEXTURE_HANDLE_D3D11_TEXTURE;
  expect_preview_failure(missing, "D3D12 preview handle type mismatch");
  missing = windows;
  missing.resources = D3D12ProductionResources{nullptr, &queue};
  expect_preview_failure(missing, "D3D12 device is unavailable");
  missing = windows;
  missing.resources = D3D12ProductionResources{&device, nullptr};
  expect_preview_failure(missing, "D3D12 command queue is unavailable");
  auto missing_registrar = preview_attachment;
  missing_registrar.flutter_texture_registrar = nullptr;
  preview_diagnostic.clear();
  assert(!validate_windows_d3d12_preview_build_inputs(
      windows, missing_registrar, preview_diagnostic));
  assert(preview_diagnostic ==
         "Flutter Windows texture registrar is unavailable");

  BackendProductionCapability cpu{};
  cpu.backend = DIGITOR_RENDERER_CPU;
  cpu.context_identity = 1;
  cpu.frame_context_identity = &context;
  assert(!cpu.valid());

  std::string diagnostic;

  // Installing engine-owned dependencies may happen before the Flutter texture
  // attachment exists. A deferred registration attempt must not roll the
  // factory back; only the explicit clear operation owns that lifecycle.
  auto deferred_factory = [](
                              const BackendProductionCapability&,
                              const FlutterProductionPluginAttachment&,
                              std::string& local)
      -> std::optional<WindowsEngineProductionDependencies> {
    local = "test dependency factory invoked";
    return std::nullopt;
  };
  diagnostic.clear();
  assert(install_windows_engine_production_dependencies_factory(
             deferred_factory, &diagnostic) == DIGITOR_RESULT_OK);
  diagnostic.clear();
  assert(install_windows_engine_production_dependencies_factory(
             deferred_factory, &diagnostic) == DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(!diagnostic.empty());
  assert(clear_windows_engine_production_dependencies_factory() ==
         DIGITOR_RESULT_OK);

  // CPU and incomplete native capabilities must never install a nominal
  // production runtime or provider builder.
  auto cpu_runtime = install_windows_engine_production_runtime(cpu, &diagnostic);
  assert(!cpu_runtime);
  assert(!diagnostic.empty());
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));

  BackendProductionCapability incomplete_windows{};
  incomplete_windows.backend = DIGITOR_RENDERER_D3D12;
  incomplete_windows.context_identity = 102;
  incomplete_windows.frame_context_identity = &context;
  incomplete_windows.resources = D3D12ProductionResources{nullptr, &queue};
  assert(incomplete_windows.valid());
  diagnostic.clear();
  auto incomplete_runtime =
      install_windows_engine_production_runtime(incomplete_windows, &diagnostic);
  assert(!incomplete_runtime);
  assert(!diagnostic.empty());
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));

  // With no optional full Windows dependency package installed, a selected
  // D3D12 renderer that owns strict decode/import and native preview descriptor
  // bindings must still be able to consume the real Flutter attachment and
  // register the production preview host. Export remains fail-closed because
  // this preview-only build intentionally contains no encoder factory.
  diagnostic.clear();
  auto wr = install_windows_engine_production_runtime(windows, &diagnostic);
  assert(wr && wr->active());
  assert(diagnostic.empty());
  assert(flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));

  DigitorFlutterProductionPluginAttachment windows_attachment{};
  windows_attachment.struct_size = sizeof(windows_attachment);
  windows_attachment.api_version =
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION;
  windows_attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  windows_attachment.flutter_texture_registrar = &registrar;
  windows_attachment.implementation_identity = "test.flutter.windows.preview";
  assert(digitor_flutter_production_plugin_attach(&windows_attachment) ==
         DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attached() == 1);
  assert(digitor_flutter_production_host_registered() == 1);
  assert(digitor_flutter_production_plugin_detach(&registrar) ==
         DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attached() == 0);
  assert(digitor_flutter_production_host_registered() == 0);

  assert(wr->shutdown() == DIGITOR_RESULT_OK);
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));

  // Windows Vulkan production is only valid when the selected renderer owns
  // the native DXGI import callback. A nominal Vulkan device/queue without
  // import ownership must fail closed instead of falling back to D3D12/CPU.
  BackendProductionCapability windows_vulkan{};
  windows_vulkan.backend = DIGITOR_RENDERER_VULKAN;
  windows_vulkan.context_identity = 103;
  windows_vulkan.frame_context_identity = &context;
  windows_vulkan.resources =
      VulkanProductionResources{&instance, &physical, &device, &queue, 0};
  diagnostic.clear();
  auto missing_vulkan_import =
      install_windows_engine_production_runtime(windows_vulkan, &diagnostic);
  assert(!missing_vulkan_import);
  assert(!diagnostic.empty());

  windows_vulkan.native_media_import =
      [](const ZeroCopyImportRequest&, ProcessedGpuFramePtr& frame) {
        frame.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  diagnostic.clear();
  auto windows_vulkan_runtime =
      install_windows_engine_production_runtime(windows_vulkan, &diagnostic);
  assert(windows_vulkan_runtime && windows_vulkan_runtime->active());
  assert(diagnostic.empty());
  assert(windows_vulkan_runtime->shutdown() == DIGITOR_RESULT_OK);

  // Android production decode is owned by the exact selected renderer. A
  // Vulkan device/queue without an AHardwareBuffer import callback must not
  // install a nominal Flutter production builder.
  BackendProductionCapability android{};
  android.backend = DIGITOR_RENDERER_VULKAN;
  android.context_identity = 202;
  android.frame_context_identity = &context;
  android.resources =
      VulkanProductionResources{&instance, &physical, &device, &queue, 0};
  diagnostic.clear();
  auto missing_android_import =
      install_android_engine_production_runtime(android, &diagnostic);
  assert(!missing_android_import);
  assert(!diagnostic.empty());
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID));

  android.native_media_import =
      [](const ZeroCopyImportRequest&, ProcessedGpuFramePtr& frame) {
        frame.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  diagnostic.clear();
  auto ar = install_android_engine_production_runtime(android, &diagnostic);
  assert(ar && ar->active());
  assert(diagnostic.empty());
  assert(flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID));
  assert(ar->shutdown() == DIGITOR_RESULT_OK);
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID));

  BackendProductionCapability apple{};
  apple.backend = DIGITOR_RENDERER_METAL;
  apple.context_identity = 303;
  apple.frame_context_identity = &context;
  apple.resources = MetalProductionResources{&device};
  auto mr = install_apple_engine_production_runtime(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS, apple, &diagnostic);
  assert(mr && mr->active());
  assert(mr->shutdown() == DIGITOR_RESULT_OK);

  // A live capability from the wrong platform/backend family must also fail
  // closed rather than installing a Windows builder around foreign resources.
  diagnostic.clear();
  auto wrong_platform =
      install_windows_engine_production_runtime(apple, &diagnostic);
  assert(!wrong_platform);
  assert(!diagnostic.empty());
  assert(!flutter_production_provider_builder_installed(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));

  FlutterProductionPluginAttachment attachment{};
  attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  attachment.flutter_texture_registrar = &registrar;
  attachment.implementation_identity = "test.flutter.windows";
  auto wi = assemble_windows_engine_production_build(
      windows, attachment, WindowsEngineProductionDependencies{});
  assert(wi.result == DIGITOR_RESULT_NOT_INITIALIZED);

  attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID;
  auto ai = assemble_android_engine_production_build(
      android, attachment, AndroidEngineProductionDependencies{});
  assert(ai.result == DIGITOR_RESULT_NOT_INITIALIZED);

  attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS;
  auto mi = assemble_apple_engine_production_build(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS, apple, attachment,
      AppleEngineProductionDependencies{});
  assert(mi.result == DIGITOR_RESULT_NOT_INITIALIZED);
  return 0;
}
