from pathlib import Path

root = Path(__file__).resolve().parents[1]


def replace_once(rel: str, old: str, new: str) -> None:
    p = root / rel
    s = p.read_text()
    if old not in s:
        raise RuntimeError(f"anchor missing in {rel}: {old[:100]!r}")
    p.write_text(s.replace(old, new, 1))


# Exact renderer generation identity.
for rel in [
    "src/gpu/d3d12_backend.cpp",
    "src/gpu/vulkan_backend.cpp",
    "src/gpu/gles_backend.cpp",
    "src/gpu/metal_backend.mm",
]:
    p = root / rel
    s = p.read_text()
    anchor = "    out.context_identity = backend_context_identity();\n"
    if "out.frame_context_identity = this;" not in s:
        if anchor not in s:
            raise RuntimeError(f"production capability anchor missing in {rel}")
        p.write_text(s.replace(anchor, anchor + "    out.frame_context_identity = this;\n", 1))

# Windows decode/import uses the same context identity as backend-created frames.
replace_once(
    "include/digitor/windows_d3d12_yuv_converter.hpp",
    "  explicit WindowsD3D12YuvConverter(void* d3d12_device);",
    "  explicit WindowsD3D12YuvConverter(\n      void* d3d12_device, const void* frame_context_identity = nullptr);",
)
replace_once(
    "src/gpu/windows_d3d12_yuv_converter.cpp",
    "struct WindowsD3D12YuvConverter::Impl {\n#ifdef _WIN32",
    "struct WindowsD3D12YuvConverter::Impl {\n const void* frame_context_identity{};\n#ifdef _WIN32",
)
replace_once(
    "src/gpu/windows_d3d12_yuv_converter.cpp",
    "WindowsD3D12YuvConverter::WindowsD3D12YuvConverter(void* raw):impl_(std::make_shared<Impl>()){\n if(!raw)throw std::invalid_argument(\"D3D12 device is required\");",
    "WindowsD3D12YuvConverter::WindowsD3D12YuvConverter(\n    void* raw, const void* frame_context_identity):impl_(std::make_shared<Impl>()){\n if(!raw)throw std::invalid_argument(\"D3D12 device is required\");\n impl_->frame_context_identity = frame_context_identity ? frame_context_identity : raw;",
)
replace_once(
    "src/gpu/windows_d3d12_yuv_converter.cpp",
    "  out=std::make_shared<ProcessedGpuFrame>(impl_->device.Get(),DIGITOR_RENDERER_D3D12,std::move(m),id,owner,std::make_shared<std::atomic_bool>(true),false);",
    "  out=std::make_shared<ProcessedGpuFrame>(impl_->frame_context_identity,DIGITOR_RENDERER_D3D12,std::move(m),id,owner,std::make_shared<std::atomic_bool>(true),false);",
)
replace_once(
    "include/digitor/ffmpeg_d3d11va_zero_copy_decoder.hpp",
    "                               LegacyCpuFallbackCallback legacy = {});",
    "                               LegacyCpuFallbackCallback legacy = {},\n                               const void* frame_context_identity = nullptr);",
)
replace_once(
    "src/media/ffmpeg_d3d11va_zero_copy_decoder.cpp",
    "    LegacyCpuFallbackCallback legacy)\n    : options_(options), legacy_(std::move(legacy)),\n      converter_(std::make_unique<WindowsD3D12YuvConverter>(d3d12_device)),",
    "    LegacyCpuFallbackCallback legacy,\n    const void* frame_context_identity)\n    : options_(options), legacy_(std::move(legacy)),\n      converter_(std::make_unique<WindowsD3D12YuvConverter>(\n          d3d12_device, frame_context_identity)),",
)

# Correct hardware encoder identities.
p = root / "src/platform/windows/windows_engine_production.hpp"
p.write_text(p.read_text().replace(
    "EncoderBackend encoder_backend{EncoderBackend::software};",
    "EncoderBackend encoder_backend{EncoderBackend::quick_sync};",
))
p = root / "src/platform/android/android_engine_production.hpp"
p.write_text(p.read_text().replace(
    "EncoderBackend encoder_backend{EncoderBackend::quick_sync};",
    "EncoderBackend encoder_backend{EncoderBackend::media_codec};",
))

(root / "src/core/engine_production_runtime.hpp").write_text(
    "#pragma once\n\n"
    "#include \"digitor/production_integration_runtime.hpp\"\n"
    "#include \"gpu/backend_production_capability.hpp\"\n\n"
    "#include <memory>\n#include <string>\n\n"
    "namespace digitor {\n"
    "[[nodiscard]] bool engine_production_runtime_supported_platform() noexcept;\n"
    "[[nodiscard]] std::unique_ptr<ProductionIntegrationRuntime>\n"
    "install_engine_production_runtime(const BackendProductionCapability& capability,\n"
    "                                  std::string* diagnostic = nullptr) noexcept;\n"
    "}  // namespace digitor\n"
)

(root / "src/core/engine_production_runtime.cpp").write_text(
    "#include \"core/engine_production_runtime.hpp\"\n\n"
    "#include \"platform/android/android_engine_production.hpp\"\n"
    "#include \"platform/apple/apple_engine_production.hpp\"\n"
    "#include \"platform/windows/windows_engine_production.hpp\"\n\n"
    "#if defined(__APPLE__)\n#include <TargetConditionals.h>\n#endif\n\n"
    "namespace digitor {\n"
    "bool engine_production_runtime_supported_platform() noexcept {\n"
    "#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)\n"
    "  return true;\n#else\n  return false;\n#endif\n}\n\n"
    "std::unique_ptr<ProductionIntegrationRuntime> install_engine_production_runtime(\n"
    "    const BackendProductionCapability& capability, std::string* diagnostic) noexcept {\n"
    "  if (!capability.valid()) {\n"
    "    if (diagnostic) *diagnostic = \"selected backend has no production GPU capability\";\n"
    "    return {};\n  }\n"
    "#if defined(_WIN32)\n"
    "  return install_windows_engine_production_runtime(capability, diagnostic);\n"
    "#elif defined(__ANDROID__)\n"
    "  return install_android_engine_production_runtime(capability, diagnostic);\n"
    "#elif defined(__APPLE__)\n"
    "  #if TARGET_OS_IPHONE\n"
    "  return install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS, capability, diagnostic);\n"
    "  #else\n"
    "  return install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS, capability, diagnostic);\n"
    "  #endif\n"
    "#else\n"
    "  if (diagnostic) *diagnostic = \"Flutter production runtime is not required on this host\";\n"
    "  return {};\n"
    "#endif\n}\n"
    "}  // namespace digitor\n"
)

# Engine owns runtime and shuts it down before native GPU resources.
replace_once(
    "src/core/engine.hpp",
    "#include \"digitor/native_node_executor.hpp\"\n",
    "#include \"digitor/native_node_executor.hpp\"\n#include \"digitor/production_integration_runtime.hpp\"\n",
)
replace_once(
    "src/core/engine.hpp",
    "  Engine() = default;\n  mutable std::mutex mutex_;",
    "  Engine() = default;\n  DigitorResult finish_backend_initialization_locked();\n  mutable std::mutex mutex_;",
)
replace_once(
    "src/core/engine.hpp",
    "  std::unique_ptr<IRenderBackend> backend_;\n  std::unordered_set<RenderContext *> contexts_;",
    "  std::unique_ptr<IRenderBackend> backend_;\n  std::unique_ptr<ProductionIntegrationRuntime> production_runtime_;\n  std::unordered_set<RenderContext *> contexts_;",
)
replace_once(
    "src/core/engine.cpp",
    "#include \"core/engine.hpp\"\n",
    "#include \"core/engine.hpp\"\n#include \"core/engine_production_runtime.hpp\"\n",
)
replace_once(
    "src/core/engine.cpp",
    "Engine &Engine::instance() { static Engine engine; return engine; }\n\nDigitorResult Engine::initialize",
    "Engine &Engine::instance() { static Engine engine; return engine; }\n\n"
    "DigitorResult Engine::finish_backend_initialization_locked() {\n"
    "    if (!backend_) return DIGITOR_RESULT_NOT_INITIALIZED;\n"
    "    const auto capability = backend_->production_capability();\n"
    "    if (capability.valid() && engine_production_runtime_supported_platform()) {\n"
    "        std::string diagnostic;\n"
    "        production_runtime_ = install_engine_production_runtime(capability, &diagnostic);\n"
    "        if (!production_runtime_) {\n"
    "            backend_->shutdown();\n"
    "            backend_.reset();\n"
    "            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;\n"
    "        }\n"
    "    }\n"
    "    initialized_ = true;\n"
    "    return DIGITOR_RESULT_OK;\n"
    "}\n\n"
    "DigitorResult Engine::initialize",
)
replace_once(
    "src/core/engine.cpp",
    "    if (backend_ && backend_->initialize(config.enable_validation != 0)) {\n        initialized_ = true;\n        return DIGITOR_RESULT_OK;\n    }",
    "    if (backend_ && backend_->initialize(config.enable_validation != 0)) {\n        return finish_backend_initialization_locked();\n    }",
)
replace_once(
    "src/core/engine.cpp",
    "        backend_ = std::move(cpu);\n        initialized_ = true;\n        return DIGITOR_RESULT_OK;",
    "        backend_ = std::move(cpu);\n        return finish_backend_initialization_locked();",
)
replace_once(
    "src/core/engine.cpp",
    "DigitorResult Engine::shutdown(){std::scoped_lock lock(mutex_);if(!initialized_)return DIGITOR_RESULT_NOT_INITIALIZED;if(!contexts_.empty())return DIGITOR_RESULT_RESOURCE_IN_USE;if(backend_){backend_->shutdown();backend_.reset();}initialized_=false;return DIGITOR_RESULT_OK;}",
    "DigitorResult Engine::shutdown(){std::scoped_lock lock(mutex_);if(!initialized_)return DIGITOR_RESULT_NOT_INITIALIZED;if(!contexts_.empty())return DIGITOR_RESULT_RESOURCE_IN_USE;if(production_runtime_){const auto r=production_runtime_->shutdown();if(r!=DIGITOR_RESULT_OK)return r;production_runtime_.reset();}if(backend_){backend_->shutdown();backend_.reset();}initialized_=false;return DIGITOR_RESULT_OK;}",
)

# Flutter lifecycle order: sessions -> detach -> engine/runtime -> GPU backend.
replace_once(
    "dart/digitor_engine_ffi/lib/src/editor_workspace.dart",
    "  Future<void> close() async {\n    if (_closed) return;\n    _closed = true;\n    _productionSession?.dispose();\n    _productionSession = null;\n    final previewTexture = _previewTexture;\n    _previewTexture = null;\n    if (previewTexture != null) {\n      await _platformHost.disposeTexture(previewTexture);\n    }\n    _timeline.dispose();",
    "  Future<void> releaseProductionSession() async {\n    if (_closed) return;\n    _productionSession?.dispose();\n    _productionSession = null;\n    final previewTexture = _previewTexture;\n    _previewTexture = null;\n    if (previewTexture != null) {\n      await _platformHost.disposeTexture(previewTexture);\n    }\n  }\n\n  Future<void> close() async {\n    if (_closed) return;\n    await releaseProductionSession();\n    _closed = true;\n    _timeline.dispose();",
)
replace_once(
    "dart/digitor_engine_ffi/lib/src/editor_controller.dart",
    "    try {\n      await _workspace.close();\n      await _productionBootstrap.detach();\n    } finally {",
    "    try {\n      await _workspace.releaseProductionSession();\n      await _productionBootstrap.detach();\n      await _workspace.close();\n    } finally {",
)
replace_once(
    "dart/digitor_engine_ffi/lib/src/editor_controller.dart",
    "    unawaited(() async {\n      await _workspace.close();\n      await _productionBootstrap.detach();\n    }());",
    "    unawaited(() async {\n      await _workspace.releaseProductionSession();\n      await _productionBootstrap.detach();\n      await _workspace.close();\n    }());",
)

# Build and regression target.
replace_once(
    "CMakeLists.txt",
    "    src/core/production_integration_runtime.cpp\n",
    "    src/core/production_integration_runtime.cpp\n"
    "    src/core/engine_production_runtime.cpp\n"
    "    src/platform/windows/windows_engine_production.cpp\n"
    "    src/platform/android/android_engine_production.cpp\n"
    "    src/platform/apple/apple_engine_production.cpp\n",
)
replace_once(
    "CMakeLists.txt",
    "    add_executable(digitor_production_integration_runtime_test tests/test_production_integration_runtime.cpp)\n",
    "    add_executable(digitor_production_integration_runtime_test tests/test_production_integration_runtime.cpp)\n"
    "    add_executable(digitor_engine_owned_platform_production_test tests/test_engine_owned_platform_production.cpp)\n",
)
replace_once(
    "CMakeLists.txt",
    "    foreach(target digitor_production_integration_runtime_test ",
    "    foreach(target digitor_engine_owned_platform_production_test digitor_production_integration_runtime_test ",
)
replace_once(
    "CMakeLists.txt",
    "    add_test(NAME digitor_production_integration_runtime COMMAND digitor_production_integration_runtime_test)\n",
    "    add_test(NAME digitor_production_integration_runtime COMMAND digitor_production_integration_runtime_test)\n"
    "    add_test(NAME digitor_engine_owned_platform_production COMMAND digitor_engine_owned_platform_production_test)\n",
)

(root / "tests/test_engine_owned_platform_production.cpp").write_text(
    "#include \"platform/android/android_engine_production.hpp\"\n"
    "#include \"platform/apple/apple_engine_production.hpp\"\n"
    "#include \"platform/windows/windows_engine_production.hpp\"\n\n"
    "#include <cassert>\n#include <string>\n\n"
    "using namespace digitor;\n\n"
    "int main() {\n"
    "  int context=1, device=2, queue=3, instance=4, physical=5, registrar=6;\n"
    "  BackendProductionCapability windows{}; windows.backend=DIGITOR_RENDERER_D3D12; windows.context_identity=101; windows.frame_context_identity=&context; windows.resources=D3D12ProductionResources{&device,&queue}; assert(windows.valid());\n"
    "  BackendProductionCapability cpu{}; cpu.backend=DIGITOR_RENDERER_CPU; cpu.context_identity=1; cpu.frame_context_identity=&context; assert(!cpu.valid());\n"
    "  std::string diagnostic;\n"
    "  auto wr=install_windows_engine_production_runtime(windows,&diagnostic); assert(wr&&wr->active()); assert(flutter_production_provider_builder_installed(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS)); assert(wr->shutdown()==DIGITOR_RESULT_OK); assert(!flutter_production_provider_builder_installed(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));\n"
    "  BackendProductionCapability android{}; android.backend=DIGITOR_RENDERER_VULKAN; android.context_identity=202; android.frame_context_identity=&context; android.resources=VulkanProductionResources{&instance,&physical,&device,&queue,0}; auto ar=install_android_engine_production_runtime(android,&diagnostic); assert(ar&&ar->active()); assert(ar->shutdown()==DIGITOR_RESULT_OK);\n"
    "  BackendProductionCapability apple{}; apple.backend=DIGITOR_RENDERER_METAL; apple.context_identity=303; apple.frame_context_identity=&context; apple.resources=MetalProductionResources{&device}; auto mr=install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS,apple,&diagnostic); assert(mr&&mr->active()); assert(mr->shutdown()==DIGITOR_RESULT_OK);\n"
    "  FlutterProductionPluginAttachment attachment{}; attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS; attachment.flutter_texture_registrar=&registrar; attachment.implementation_identity=\"test.flutter.windows\"; auto wi=assemble_windows_engine_production_build(windows,attachment,WindowsEngineProductionDependencies{}); assert(wi.result==DIGITOR_RESULT_NOT_INITIALIZED);\n"
    "  attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID; auto ai=assemble_android_engine_production_build(android,attachment,AndroidEngineProductionDependencies{}); assert(ai.result==DIGITOR_RESULT_NOT_INITIALIZED);\n"
    "  attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS; auto mi=assemble_apple_engine_production_build(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS,apple,attachment,AppleEngineProductionDependencies{}); assert(mi.result==DIGITOR_RESULT_NOT_INITIALIZED);\n"
    "  return 0;\n}\n"
)

# Remove one-shot helper/workflow from the final branch snapshot.
(root / ".github/workflows/pr348-finish-blockers34.yml").unlink(missing_ok=True)
Path(__file__).unlink()
