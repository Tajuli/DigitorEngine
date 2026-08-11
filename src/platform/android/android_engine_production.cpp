#include "platform/android/android_engine_production.hpp"
#include <cstdint>
#include <mutex>
#include <utility>
namespace digitor { namespace {
struct S{std::mutex m;AndroidEngineProductionDependenciesFactory f;}; S& st(){static S s;return s;}
bool encoder_ok(const AndroidHardwareEncoderHost& h){return h.open&&h.submit&&h.drain&&h.finalize_mp4_atomic&&h.cancel&&h.qualification;}
bool deps_ok(const AndroidEngineProductionDependencies& d){return d.timeline.create_target&&d.timeline.execute_effects&&d.timeline.composite_layer&&d.timeline.frame_evictable&&encoder_ok(d.encoder)&&d.decoder_factory&&d.frame_resolver&&d.texture_descriptor_builder&&d.preview_target_binder&&d.fps_num>0&&d.fps_den>0&&d.video_bitrate>0&&!d.package_identity.empty()&&!d.build_identity.empty();}
bool cap_ok(const BackendProductionCapability& b,std::string& x){if(!b.valid()){x="selected Android backend has no live production capability";return false;}if(b.backend==DIGITOR_RENDERER_VULKAN){auto*p=std::get_if<VulkanProductionResources>(&b.resources);if(!p||!p->instance||!p->physical_device||!p->device||!p->queue){x="Android Vulkan production capability is incomplete";return false;}return true;}if(b.backend==DIGITOR_RENDERER_OPENGL_ES){auto*p=std::get_if<GlesProductionResources>(&b.resources);if(!p||!p->egl_display||!p->egl_context){x="Android GLES production capability is incomplete";return false;}return true;}x="Android production requires Vulkan or OpenGL ES";return false;}
}
AndroidEngineProductionBuildResult assemble_android_engine_production_build(const BackendProductionCapability& b,const FlutterProductionPluginAttachment&a,AndroidEngineProductionDependencies d) noexcept{AndroidEngineProductionBuildResult o;try{if(a.platform!=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID||!a.flutter_texture_registrar||a.implementation_identity.empty()){o.result=DIGITOR_RESULT_INVALID_ARGUMENT;o.diagnostic="valid Android Flutter production attachment is required";return o;}if(!cap_ok(b,o.diagnostic)){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;return o;}if(!deps_ok(d)){o.result=DIGITOR_RESULT_NOT_INITIALIZED;o.diagnostic="engine-owned Android production dependencies are incomplete";return o;}if(d.timeline.backend!=b.backend||d.timeline.context_identity!=b.frame_context_identity){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;o.diagnostic="Android timeline is not bound to selected backend generation";return o;}if(!d.preview_capabilities.native_gpu_preview_available||d.preview_capabilities.cpu_fallback_only){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;o.diagnostic="Android preview is not native-GPU ready";return o;}
AndroidNativeProviderBindings x{};x.timeline=d.timeline;x.flutter.flutter_texture_registrar=a.flutter_texture_registrar;x.flutter.implementation_identity=a.implementation_identity;x.flutter.attached=[] {return true;};x.flutter.present=[a,p=d.flutter_present](const ProcessedGpuFramePtr&f,std::uint64_t g) mutable {if(!p)return DIGITOR_RESULT_NOT_INITIALIZED;std::string q;return p(a,f,g,q);};x.encoder=d.encoder;x.capabilities=d.capabilities;x.device_identity=b.frame_context_identity;x.package_identity=d.package_identity;x.build_identity=d.build_identity;
#if !defined(__ANDROID__)
o.result=DIGITOR_RESULT_UNSUPPORTED;o.diagnostic="Android production assembly requires Android";return o;
#else
auto p=create_android_native_provider(std::move(x));if(!p){o.result=p.result;o.diagnostic=p.diagnostic;return o;}FlutterProductionProviderBuild z{};z.provider=std::move(p.provider);z.platform_inputs.platform=ProductionPlatform::android;z.platform_inputs.timeline=d.timeline;z.decoder_factory=std::move(d.decoder_factory);z.frame_resolver=std::move(d.frame_resolver);z.texture_descriptor_builder=std::move(d.texture_descriptor_builder);z.preview_target_binder=std::move(d.preview_target_binder);z.preview_capabilities=d.preview_capabilities;z.encoder_backend=d.encoder_backend;z.fps_num=d.fps_num;z.fps_den=d.fps_den;z.video_bitrate=d.video_bitrate;z.required_device_identity=(std::uint64_t)(std::uintptr_t)b.frame_context_identity;z.required_context_identity=b.context_identity;o.build=std::move(z);o.result=DIGITOR_RESULT_OK;return o;
#endif
}catch(...){o.result=DIGITOR_RESULT_INTERNAL_ERROR;o.diagnostic="Android production assembly failed";return o;}}
DigitorResult install_android_engine_production_dependencies_factory(
    AndroidEngineProductionDependenciesFactory f, std::string* d) noexcept {
  if (!f) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    auto& s = st();
    {
      std::scoped_lock l(s.m);
      if (s.f) return DIGITOR_RESULT_RESOURCE_IN_USE;
      s.f = std::move(f);
    }
    std::string retry_diagnostic;
    const auto retry = retry_flutter_production_host_registration(
        DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID, &retry_diagnostic);
    if (retry != DIGITOR_RESULT_OK) {
      std::scoped_lock l(s.m);
      s.f = {};
      if (d) *d = retry_diagnostic;
      return retry;
    }
    if (d) d->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}
DigitorResult clear_android_engine_production_dependencies_factory() noexcept{auto&s=st();std::scoped_lock l(s.m);s.f={};return DIGITOR_RESULT_OK;}
std::unique_ptr<ProductionIntegrationRuntime> install_android_engine_production_runtime(const BackendProductionCapability& b,std::string*d) noexcept{std::string q;if(!cap_ok(b,q)){if(d)*d=q;return{};}return ProductionIntegrationRuntime::install(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID,[b](const FlutterProductionPluginAttachment&a,std::string&l)->std::optional<FlutterProductionProviderBuild>{AndroidEngineProductionDependenciesFactory f;{auto&s=st();std::scoped_lock g(s.m);f=s.f;}if(!f){l="engine-owned Android production dependencies are not installed";return std::nullopt;}auto x=f(b,a,l);if(!x)return std::nullopt;auto z=assemble_android_engine_production_build(b,a,std::move(*x));if(!z){l=z.diagnostic;return std::nullopt;}l.clear();return std::move(z.build);},d);}
} // namespace digitor

#if defined(__ANDROID__)
#include "android_native_provider.cpp"
#endif
