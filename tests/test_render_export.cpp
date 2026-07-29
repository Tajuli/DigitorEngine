#include "digitor/renderer.hpp"
#include "digitor/render_policy.hpp"
#include "digitor/digitor.h"
#include "gpu/native_pipeline_cache.hpp"
#include "gpu/preview_consumer.hpp"
#include <cassert>
#include <array>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
void test_render_export(){
 digitor::VideoFrame a;a.width=2;a.height=1;a.pixels={{0,0,0,1},{1,1,1,1}};auto b=a;auto v=digitor::validate_pixels(a,b);assert(v.passed&&v.differing_pixels==0&&std::isinf(v.psnr)&&v.ssim==1);
 b.pixels[0].r=.1f;v=digitor::validate_pixels(a,b,20,.9);assert(v.differing_pixels==1&&v.psnr>20&&v.ssim>.9);
 int builds=0;digitor::SharedRenderer renderer([&](auto&g,const auto&r,auto&out){++builds;auto id=g.create_transient(4);g.add_pass({"one-graph",{},{{id,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.assign(size_t(r.width)*r.height,{.25f,.5f,.75f,1});});}});});assert(digitor::validate_preview_export(renderer,{0,2,2,{}}).passed);assert(builds==1);
 DigitorSdkSession*s=nullptr;assert(digitor_sdk_create(&s)==DIGITOR_RESULT_OK);std::mutex m;std::condition_variable cv;std::atomic_bool done=false;auto callback=[](DigitorResult r,void*u){assert(r==DIGITOR_RESULT_OK);auto*p=static_cast<std::pair<std::condition_variable*,std::atomic_bool*>*>(u);p->second->store(true);p->first->notify_one();};std::pair<std::condition_variable*,std::atomic_bool*> state{&cv,&done};assert(digitor_sdk_preview_async(s,1,4,4,callback,&state)==DIGITOR_RESULT_OK);{std::unique_lock lock(m);cv.wait(lock,[&]{return done.load();});}DigitorNativeTexture texture{};assert(digitor_sdk_get_native_texture(s,&texture)==DIGITOR_RESULT_OK&&texture.width==4&&texture.pixels);assert(digitor_sdk_destroy(s)==DIGITOR_RESULT_OK);

 // A C++ callback supplied through the C ABI must not be able to terminate the
 // process by unwinding out of an SDK worker thread.
 DigitorSdkSession* throwing=nullptr;assert(digitor_sdk_create(&throwing)==DIGITOR_RESULT_OK);
 auto throws=[](DigitorResult,void*){throw std::runtime_error("callback failure");};
 assert(digitor_sdk_seek_async(throwing,2,throws,nullptr)==DIGITOR_RESULT_OK);
 assert(digitor_sdk_destroy(throwing)==DIGITOR_RESULT_OK);

 DigitorSdkSession* reentrant=nullptr;assert(digitor_sdk_create(&reentrant)==DIGITOR_RESULT_OK);
 struct Reentry { DigitorSdkSession* session; std::atomic_bool done; } reentry{reentrant,false};
 auto reenter=[](DigitorResult result,void* data){auto& state=*static_cast<Reentry*>(data);
   assert(result==DIGITOR_RESULT_OK);
   assert(digitor_sdk_set_color(state.session,{0,1,1})==DIGITOR_RESULT_OK);
   assert(digitor_sdk_destroy(state.session)==DIGITOR_RESULT_RESOURCE_IN_USE);
   state.done.store(true);
 };
 assert(digitor_sdk_seek_async(reentrant,3,reenter,&reentry)==DIGITOR_RESULT_OK);
 while(!reentry.done.load())std::this_thread::yield();
 assert(digitor_sdk_destroy(reentrant)==DIGITOR_RESULT_OK);

 // Destruction and entry through another API may race, but the session object
 // must stay pinned until every in-flight API call has left it.
 DigitorSdkSession* raced=nullptr;assert(digitor_sdk_create(&raced)==DIGITOR_RESULT_OK);
 std::atomic_bool start{false};
 std::thread caller([&]{while(!start.load()){} for(int i=0;i<10000;++i){
   const auto result=digitor_sdk_set_color(raced,{0,1,1});
   assert(result==DIGITOR_RESULT_OK||result==DIGITOR_RESULT_INVALID_ARGUMENT);
 }});
 start.store(true);assert(digitor_sdk_destroy(raced)==DIGITOR_RESULT_OK);caller.join();

 // Source selection is explicit: preview may choose reduced/proxy media while
 // export can only choose the original source from the same media identity.
 using SC=digitor::MediaSourceClass;
 std::vector<digitor::MediaSourceDescriptor> sources{
   {SC::Original,"camera-original","media-7",3840,2160,digitor::RenderPrecision::Float32,"scene-linear"},
   {SC::Proxy,"proxy-720","media-7",1280,720,digitor::RenderPrecision::Float32,"scene-linear"},
   {SC::CompressedPreview,"compressed-540","media-7",960,540,digitor::RenderPrecision::Float32,"scene-linear"},
   {SC::DecodeScaled,"decode-half","media-7",1920,1080,digitor::RenderPrecision::Float32,"scene-linear"}};
 auto wheels=digitor::PrimaryWheelsParameters::create();
 digitor::RgbCurvesParameters curve_descriptor;
 auto curves=digitor::CompiledRgbCurves::compile(curve_descriptor);
 digitor::ColorGraphConfiguration color_graph{
   digitor::ColorOperationOrder::PrimaryWheelsThenRgbCurves,
   wheels->serialize(),curves->serialize(),digitor::RenderPrecision::Float32,
   "scene-linear",true,true};
 digitor::PreviewSourceConfiguration preview{SC::Proxy,1280,720,
   digitor::RenderPrecision::Float32,digitor::PreviewFrameRatePolicy{24,1}};
 const auto proxy_plan=digitor::build_color_render_plan(
   digitor::RenderPurpose::Preview,sources,preview,color_graph);
 const auto export_plan=digitor::build_color_render_plan(
   digitor::RenderPurpose::Export,sources,preview,color_graph);
 assert(proxy_plan.source.source_identity=="proxy-720");
 assert(export_plan.source.source_identity=="camera-original");
 assert(proxy_plan.graph.identity()==export_plan.graph.identity());
 assert(proxy_plan.graph.operation_sequence()==export_plan.graph.operation_sequence());
 assert(proxy_plan.graph.primary_wheels_serialization==export_plan.graph.primary_wheels_serialization);
 assert(proxy_plan.graph.rgb_curves_serialization==export_plan.graph.rgb_curves_serialization);
 assert(proxy_plan.source_cache_identity!=export_plan.source_cache_identity);
 preview.requested_class=SC::DecodeScaled;
 const auto scaled_plan=digitor::build_color_render_plan(
   digitor::RenderPurpose::Preview,sources,preview,color_graph);
 assert(scaled_plan.source.source_identity=="decode-half");
 assert(scaled_plan.graph.identity()==proxy_plan.graph.identity());
 preview.requested_class=SC::CompressedPreview;
 assert(digitor::build_color_render_plan(digitor::RenderPurpose::Preview,sources,
   preview,color_graph).source.source_identity=="compressed-540");
 preview.requested_class=SC::DecodeScaled;
 preview.requested_width=640;preview.requested_height=360;
 assert(digitor::build_color_render_plan(digitor::RenderPurpose::Preview,sources,
   preview,color_graph).graph.identity()==proxy_plan.graph.identity());
 std::array proxy_only{sources[1]};
 bool export_rejected=false;
 try{(void)digitor::build_color_render_plan(digitor::RenderPurpose::Export,
   proxy_only,preview,color_graph);}catch(const std::invalid_argument&){export_rejected=true;}
 assert(export_rejected&&proxy_plan.source.source_identity=="proxy-720");
 digitor::SharedRenderer planned_renderer;
 planned_renderer.set_media_sources(sources);
 planned_renderer.set_preview_source_configuration({SC::Proxy,1280,720,
   digitor::RenderPrecision::Float32,std::nullopt});
 planned_renderer.set_primary_wheels(wheels);planned_renderer.set_rgb_curves(curves);
 const auto renderer_preview=planned_renderer.color_render_plan(digitor::RenderPurpose::Preview);
 const auto renderer_export=planned_renderer.color_render_plan(digitor::RenderPurpose::Export);
 assert(renderer_preview.source.source_class==SC::Proxy&&renderer_export.source.source_class==SC::Original);
 assert(renderer_preview.graph.identity()==renderer_export.graph.identity());
 std::cerr<<"SOURCE_POLICY preview=proxy export=original decode_scaled=pass compressed_preview=pass export_proxy_rejection=pass graph_parity=pass\n";

 // Registered consumer owns its destination and liveness independently of the
 // processed frame. This interface test does not claim native GPU execution.
 int context=0;auto frame_ready=std::make_shared<std::atomic_bool>(true);
 auto native_frame=std::make_shared<int>(1);
 auto processed=std::make_shared<digitor::ProcessedGpuFrame>(&context,
   DIGITOR_RENDERER_VULKAN,digitor::GpuFrameMetadata{2,2},41,native_frame,
   frame_ready,false);
 auto destination=std::make_shared<int>(2);auto consumer_live=std::make_shared<std::atomic_bool>(true);
 std::uint64_t native_submits=0;
 digitor::PreviewConsumerDestination consumer(
   {DIGITOR_RENDERER_VULKAN,&context,2,2,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
    digitor::GpuPrecisionMode::Float32},91,destination,consumer_live,
   [&](const auto&,const auto& owned){assert(owned==destination);++native_submits;return DIGITOR_RESULT_OK;});
 assert(consumer.submit(processed)==DIGITOR_RESULT_OK&&native_submits==1&&consumer.submission_count()==1);
 assert(processed->release(&context)==DIGITOR_RESULT_INVALID_ARGUMENT);
 digitor::PreviewConsumerDestination throwing_consumer(
   {DIGITOR_RENDERER_VULKAN,&context,2,2,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
    digitor::GpuPrecisionMode::Float32},92,std::make_shared<int>(3),consumer_live,
   [](const auto&,const auto&)->DigitorResult{throw std::runtime_error("native submission");});
 assert(throwing_consumer.submit(processed)==DIGITOR_RESULT_INTERNAL_ERROR&&
        processed->release(&context)==DIGITOR_RESULT_INVALID_ARGUMENT);
 consumer.retire();assert(consumer.submit(processed)==DIGITOR_RESULT_NOT_INITIALIZED);
 std::cerr<<"PREVIEW_CONSUMER interface=pass submissions="<<native_submits
          <<" processed_readbacks=0 retired_rejection=pass native_runtime=not-run\n";

 // Immutable pipeline cache has deterministic bounded FIFO reuse and cannot be
 // poisoned by a failed native object creation.
 digitor::NativePipelineCache pipeline_cache(2);std::uint64_t creations=0;
 digitor::NativePipelineCacheKey key{DIGITOR_RENDERER_VULKAN,7,"primary-wheels-v1",1,
   digitor::GpuPrecisionMode::Float32,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
 auto create_pipeline=[&]{++creations;return std::static_pointer_cast<void>(std::make_shared<int>(3));};
 assert(pipeline_cache.get_or_create(key,create_pipeline));
 assert(pipeline_cache.get_or_create(key,create_pipeline)&&creations==1);
 auto other_device=key;other_device.device_context_identity=8;
 assert(pipeline_cache.get_or_create(other_device,create_pipeline)&&creations==2);
 auto other_precision=key;other_precision.precision=digitor::GpuPrecisionMode::Float16;
 assert(pipeline_cache.get_or_create(other_precision,create_pipeline)&&creations==3&&pipeline_cache.size()==2);
 pipeline_cache.invalidate_device(DIGITOR_RENDERER_VULKAN,8);
 auto failed=key;failed.shader_operation_identity="injected-pipeline-failure";
 assert(!pipeline_cache.get_or_create(failed,[] { return std::shared_ptr<void>{}; }));
 assert(pipeline_cache.get_or_create(failed,create_pipeline));
 const auto cache_counts=pipeline_cache.counters();
 assert(cache_counts.lookups==6&&cache_counts.hits==1&&cache_counts.misses==5&&
   cache_counts.creations==4&&cache_counts.evictions==1&&
   cache_counts.invalidations==1&&cache_counts.creation_failures==1);
 std::cerr<<"PIPELINE_CACHE interface=pass lookups="<<cache_counts.lookups
          <<" misses="<<cache_counts.misses<<" hits="<<cache_counts.hits
          <<" creations="<<cache_counts.creations<<" evictions="<<cache_counts.evictions
          <<" invalidations="<<cache_counts.invalidations
          <<" creation_failures="<<cache_counts.creation_failures
          <<" native_adoption=pending\n";
}
