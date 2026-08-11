#include "platform/android/android_engine_production.hpp"
#include "platform/apple/apple_engine_production.hpp"
#include "platform/windows/windows_engine_production.hpp"

#include <cassert>
#include <string>

using namespace digitor;

int main() {
  int context=1, device=2, queue=3, instance=4, physical=5, registrar=6;
  BackendProductionCapability windows{}; windows.backend=DIGITOR_RENDERER_D3D12; windows.context_identity=101; windows.frame_context_identity=&context; windows.resources=D3D12ProductionResources{&device,&queue}; assert(windows.valid());
  BackendProductionCapability cpu{}; cpu.backend=DIGITOR_RENDERER_CPU; cpu.context_identity=1; cpu.frame_context_identity=&context; assert(!cpu.valid());
  std::string diagnostic;
  auto wr=install_windows_engine_production_runtime(windows,&diagnostic); assert(wr&&wr->active()); assert(flutter_production_provider_builder_installed(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS)); assert(wr->shutdown()==DIGITOR_RESULT_OK); assert(!flutter_production_provider_builder_installed(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS));
  BackendProductionCapability android{}; android.backend=DIGITOR_RENDERER_VULKAN; android.context_identity=202; android.frame_context_identity=&context; android.resources=VulkanProductionResources{&instance,&physical,&device,&queue,0}; auto ar=install_android_engine_production_runtime(android,&diagnostic); assert(ar&&ar->active()); assert(ar->shutdown()==DIGITOR_RESULT_OK);
  BackendProductionCapability apple{}; apple.backend=DIGITOR_RENDERER_METAL; apple.context_identity=303; apple.frame_context_identity=&context; apple.resources=MetalProductionResources{&device}; auto mr=install_apple_engine_production_runtime(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS,apple,&diagnostic); assert(mr&&mr->active()); assert(mr->shutdown()==DIGITOR_RESULT_OK);
  FlutterProductionPluginAttachment attachment{}; attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS; attachment.flutter_texture_registrar=&registrar; attachment.implementation_identity="test.flutter.windows"; auto wi=assemble_windows_engine_production_build(windows,attachment,WindowsEngineProductionDependencies{}); assert(wi.result==DIGITOR_RESULT_NOT_INITIALIZED);
  attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID; auto ai=assemble_android_engine_production_build(android,attachment,AndroidEngineProductionDependencies{}); assert(ai.result==DIGITOR_RESULT_NOT_INITIALIZED);
  attachment.platform=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS; auto mi=assemble_apple_engine_production_build(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS,apple,attachment,AppleEngineProductionDependencies{}); assert(mi.result==DIGITOR_RESULT_NOT_INITIALIZED);
  return 0;
}
