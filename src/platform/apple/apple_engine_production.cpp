#include "platform/apple/apple_engine_production.hpp"
#include <cstdint>
#include <mutex>
#include <utility>
namespace digitor { namespace {
struct S{std::mutex m;AppleEngineProductionDependenciesFactory f;};S&st(){static S s;return s;}
bool encoder_ok(const AppleHardwareEncoderHost&h){return h.open&&h.submit&&h.drain&&h.finalize_atomic&&h.cancel&&h.qualification;}
bool deps_ok(const AppleEngineProductionDependencies&d){return d.timeline.create_target&&d.timeline.execute_effects&&d.timeline.composite_layer&&d.timeline.frame_evictable&&encoder_ok(d.encoder)&&d.decoder_factory&&d.frame_resolver&&d.texture_descriptor_builder&&d.preview_target_binder&&d.flutter_present&&d.fps_num>0&&d.fps_den>0&&d.video_bitrate>0&&!d.package_identity.empty()&&!d.build_identity.empty();}
bool cap_ok(const BackendProductionCapability&b,std::string&x){if(!b.valid()||b.backend!=DIGITOR_RENDERER_METAL){x="Apple production requires a live Metal backend generation";return false;}auto*p=std::get_if<MetalProductionResources>(&b.resources);if(!p||!p->device){x="Metal production capability is missing MTLDevice ownership";return false;}return true;}
ProductionPlatform prod(DigitorFlutterProductionPluginPlatform p){return p==DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS?ProductionPlatform::ios:ProductionPlatform::macos;}
}
AppleEngineProductionBuildResult assemble_apple_engine_production_build(DigitorFlutterProductionPluginPlatform p,const BackendProductionCapability&b,const FlutterProductionPluginAttachment&a,AppleEngineProductionDependencies d) noexcept{AppleEngineProductionBuildResult o;try{if((p!=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS&&p!=DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS)||a.platform!=p||!a.flutter_texture_registrar||a.implementation_identity.empty()){o.result=DIGITOR_RESULT_INVALID_ARGUMENT;o.diagnostic="valid Apple Flutter production attachment is required";return o;}if(!cap_ok(b,o.diagnostic)){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;return o;}const auto platform=prod(p);if(d.platform!=platform||!deps_ok(d)){o.result=DIGITOR_RESULT_NOT_INITIALIZED;o.diagnostic="engine-owned Apple production dependencies are incomplete or platform-mismatched";return o;}if(d.timeline.backend!=b.backend||d.timeline.context_identity!=b.frame_context_identity){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;o.diagnostic="Apple timeline is not bound to selected Metal generation";return o;}if(!d.preview_capabilities.native_gpu_preview_available||d.preview_capabilities.cpu_fallback_only){o.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;o.diagnostic="Apple preview is not native-GPU ready";return o;}
AppleNativeProviderBindings x{};x.platform=platform;x.timeline=d.timeline;x.flutter.flutter_texture_registrar=a.flutter_texture_registrar;x.flutter.implementation_identity=a.implementation_identity;x.flutter.attached=[] {return true;};x.flutter.present=[a,q=d.flutter_present](const ProcessedGpuFramePtr&f,std::uint64_t g) mutable {std::string e;return q(a,f,g,e);};x.encoder=d.encoder;x.capabilities=d.capabilities;x.device_identity=b.frame_context_identity;x.package_identity=d.package_identity;x.build_identity=d.build_identity;
#if !defined(__APPLE__)
o.result=DIGITOR_RESULT_UNSUPPORTED;o.diagnostic="Apple production assembly requires an Apple platform";return o;
#else
auto np=create_apple_native_provider(std::move(x));if(!np){o.result=np.result;o.diagnostic=np.diagnostic;return o;}FlutterProductionProviderBuild z{};z.provider=std::move(np.provider);z.platform_inputs.platform=platform;z.platform_inputs.timeline=d.timeline;z.decoder_factory=std::move(d.decoder_factory);z.frame_resolver=std::move(d.frame_resolver);z.texture_descriptor_builder=std::move(d.texture_descriptor_builder);z.preview_target_binder=std::move(d.preview_target_binder);z.preview_capabilities=d.preview_capabilities;z.encoder_backend=d.encoder_backend;z.fps_num=d.fps_num;z.fps_den=d.fps_den;z.video_bitrate=d.video_bitrate;z.required_device_identity=(std::uint64_t)(std::uintptr_t)b.frame_context_identity;z.required_context_identity=b.context_identity;o.build=std::move(z);o.result=DIGITOR_RESULT_OK;return o;
#endif
}catch(...){o.result=DIGITOR_RESULT_INTERNAL_ERROR;o.diagnostic="Apple production assembly failed";return o;}}
DigitorResult install_apple_engine_production_dependencies_factory(AppleEngineProductionDependenciesFactory f,std::string*d) noexcept{if(!f)return DIGITOR_RESULT_INVALID_ARGUMENT;try{auto&s=st();std::scoped_lock l(s.m);if(s.f)return DIGITOR_RESULT_RESOURCE_IN_USE;s.f=std::move(f);if(d)d->clear();return DIGITOR_RESULT_OK;}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}}
DigitorResult clear_apple_engine_production_dependencies_factory() noexcept{auto&s=st();std::scoped_lock l(s.m);s.f={};return DIGITOR_RESULT_OK;}
std::unique_ptr<ProductionIntegrationRuntime> install_apple_engine_production_runtime(DigitorFlutterProductionPluginPlatform p,const BackendProductionCapability&b,std::string*d) noexcept{std::string q;if(!cap_ok(b,q)){if(d)*d=q;return{};}return ProductionIntegrationRuntime::install(p,[p,b](const FlutterProductionPluginAttachment&a,std::string&l)->std::optional<FlutterProductionProviderBuild>{AppleEngineProductionDependenciesFactory f;{auto&s=st();std::scoped_lock g(s.m);f=s.f;}if(!f){l="engine-owned Apple production dependencies are not installed";return std::nullopt;}auto x=f(b,a,l);if(!x)return std::nullopt;auto z=assemble_apple_engine_production_build(p,b,a,std::move(*x));if(!z){l=z.diagnostic;return std::nullopt;}l.clear();return std::move(z.build);},d);}
} // namespace digitor

// Compile the concrete Apple provider with the engine-owned production assembly
// on Apple targets so the Metal/macOS/iOS path is link-complete in all hosts.
#if defined(__APPLE__)
#include "apple_native_provider.cpp"
#endif
