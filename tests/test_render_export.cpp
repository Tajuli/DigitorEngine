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
 // ProductionNodeGraph is authoritative for renderer output when configured.
 auto production_graph=std::make_shared<digitor::ProductionNodeGraph>();
 const auto grade_node=production_graph->add_serial_after(production_graph->input_node());
 production_graph->select_node(grade_node);
 digitor::CorrectionSettings correction_settings;correction_settings.exposure=1.0f;
 production_graph->add_operation_to_selected(digitor::make_correction_operation(digitor::CorrectionParameters::create(correction_settings)));
 digitor::SharedRenderer graph_renderer([&](auto&g,const auto&r,auto&out){auto id=g.create_transient(4);g.add_pass({"graph-source",{},{{id,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.assign(size_t(r.width)*r.height,{.25f,.25f,.25f,1});});}});});
 graph_renderer.set_production_node_graph(production_graph);
 const auto graph_frame=graph_renderer.render({3,2,1,{}});
 const std::vector<digitor::Color> graph_source(2,{.25f,.25f,.25f,1});
 const auto graph_expected=production_graph->render(graph_source,2,1,3);
 digitor::VideoFrame expected_frame;expected_frame.width=2;expected_frame.height=1;expected_frame.pixels=graph_expected;
 assert(graph_renderer.production_node_graph()==production_graph&&digitor::validate_pixels(graph_frame,expected_frame).passed);
 // CPU smoke path remains single-render; native GPU parity independently renders preview and export.

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
 const auto full=digitor::resolve_preview_dimensions(1920,1080,digitor::PreviewQuality::Full,preview,&proxy_plan.source);
 const auto half=digitor::resolve_preview_dimensions(1920,1080,digitor::PreviewQuality::Half,preview,&proxy_plan.source);
 const auto quarter=digitor::resolve_preview_dimensions(1920,1080,digitor::PreviewQuality::Quarter,preview,&proxy_plan.source);
 const auto adaptive=digitor::resolve_preview_dimensions(1920,1080,digitor::PreviewQuality::Adaptive,preview,&proxy_plan.source);
 preview.requested_width=1280;preview.requested_height=720;
 const auto proxy_dimensions=digitor::resolve_preview_dimensions(1920,1080,digitor::PreviewQuality::Proxy,preview,&proxy_plan.source);
 assert(full.width==1920&&full.height==1080);
 assert(half.width==960&&half.height==540);
 assert(quarter.width==480&&quarter.height==270);
 assert(adaptive.width==half.width&&adaptive.height==half.height);
 assert(proxy_dimensions.width==1280&&proxy_dimensions.height==720);
 const auto clamped_proxy=digitor::resolve_preview_dimensions(640,360,digitor::PreviewQuality::Proxy,preview,&proxy_plan.source);
 assert(clamped_proxy.width==640&&clamped_proxy.height==360);
 digitor::PreviewRenderer quality_renderer(planned_renderer);
 quality_renderer.set_quality(digitor::PreviewQuality::Quarter);
 const auto resolved=quality_renderer.resolved_dimensions(1920,1080);
 assert(resolved.width==480&&resolved.height==270);
 // Preview quality is execution-only: changing it cannot mutate export graph identity.
 assert(planned_renderer.color_render_plan(digitor::RenderPurpose::Export).graph.identity()==renderer_export.graph.identity());
 // Adaptive quality changes only execution dimensions after a stability window.
 digitor::PreviewRenderer adaptive_renderer(planned_renderer);
 adaptive_renderer.set_quality(digitor::PreviewQuality::Adaptive);
 assert(adaptive_renderer.effective_quality()==digitor::PreviewQuality::Half);
 for(int i=0;i<6;++i)adaptive_renderer.report_frame_time(60.0);
 assert(adaptive_renderer.effective_quality()==digitor::PreviewQuality::Quarter);
 for(int i=0;i<6;++i)adaptive_renderer.report_frame_time(10.0);
 assert(adaptive_renderer.effective_quality()==digitor::PreviewQuality::Half);
 adaptive_renderer.set_dropped_frame_policy(digitor::DroppedFramePolicy::DropLateFrames);
 assert(!adaptive_renderer.should_render_frame(10,11)&&adaptive_renderer.should_render_frame(11,11));
 adaptive_renderer.set_dropped_frame_policy(digitor::DroppedFramePolicy::Never);
 assert(adaptive_renderer.should_render_frame(10,11));
 // Qualifier eyedropper samples map to original-resolution pixel centers.
 const auto mapped=digitor::map_preview_pixel_to_source(319.5,179.5,{640,360},sources.front());
 assert(std::abs(mapped.x-1919.5)<1e-9&&std::abs(mapped.y-1079.5)<1e-9);
 const auto sample_request=planned_renderer.qualifier_sample_request(12,319.5,179.5,{640,360});
 assert(sample_request.original_source_identity=="camera-original"&&sample_request.frame==12);
 assert(sample_request.source_x==1920&&sample_request.source_y==1080);
 planned_renderer.set_original_pixel_sampler([](digitor::FrameNumber frame,std::uint32_t x,std::uint32_t y,const digitor::MediaSourceDescriptor& source){
   assert(frame==12&&x==1920&&y==1080&&source.source_identity=="camera-original");
   return digitor::Color{0.25f,0.5f,0.75f,1.0f};
 });
 const auto sampled=planned_renderer.sample_original_pixel(sample_request);
 assert(sampled.r==0.25f&&sampled.g==0.5f&&sampled.b==0.75f&&sampled.a==1.0f);
 // The production sampler can be bound directly to the authoritative decoder.
 struct FakeDecoder final:digitor::VideoDecoder {
   std::shared_ptr<digitor::VideoFrame> decode(digitor::FrameNumber frame)override{
     auto out=std::make_shared<digitor::VideoFrame>();out->number=frame;out->width=3840;out->height=2160;
     out->pixels.resize(static_cast<std::size_t>(out->width)*out->height,{0.1f,0.2f,0.3f,1.0f});
     out->pixels[static_cast<std::size_t>(1080)*out->width+1920]={0.6f,0.4f,0.2f,1.0f};return out;
   }
   void seek(std::int64_t)override{}
   digitor::DecoderInfo info()const override{return {};}
 } decoder;
 planned_renderer.set_original_pixel_sampler(digitor::make_original_decoder_sampler(decoder,sources.front()));
 const auto decoded_sample=planned_renderer.sample_original_pixel(sample_request);
 assert(decoded_sample.r==0.6f&&decoded_sample.g==0.4f&&decoded_sample.b==0.2f);
 // Playback scheduler rejects stale work before render/cache access.
 adaptive_renderer.set_dropped_frame_policy(digitor::DroppedFramePolicy::DropLateFrames);
 assert(!adaptive_renderer.playback_frame(10,11,1920,1080));
 digitor::ExportRenderer export_renderer(planned_renderer);
 digitor::ExportSettings export_settings;export_settings.width=3840;export_settings.height=2160;
 const auto contract=export_renderer.frame_contract(7,export_settings);
 assert(contract.frame==7&&contract.width==3840&&contract.height==2160);
 assert(contract.original_source_identity=="camera-original");
 assert(contract.color_graph_identity==renderer_export.graph.identity());
 assert(!contract.final_frame_identity.empty()&&contract.provenance_hash!=0);
 const auto contract_again=export_renderer.frame_contract(7,export_settings);
 assert(contract.final_frame_identity==contract_again.final_frame_identity);
 assert(contract.provenance_hash==contract_again.provenance_hash);
 auto gpu_ready=std::make_shared<std::atomic_bool>(true);
 auto export_gpu=std::make_shared<digitor::ProcessedGpuFrame>(&planned_renderer,DIGITOR_RENDERER_VULKAN,
   digitor::GpuFrameMetadata{3840,2160,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,digitor::GpuFrameAlpha::straight,7,"scene-linear"},
   7007,std::make_shared<int>(7),gpu_ready,true);
 const auto gpu_contract=export_renderer.frame_contract(7,export_settings,*export_gpu);
 assert(gpu_contract.backend==DIGITOR_RENDERER_VULKAN&&gpu_contract.gpu_frame_identity==7007);
 assert(gpu_contract.gpu_metadata_hash!=0&&digitor::export_frame_matches_contract(gpu_contract,*export_gpu));
 auto wrong_gpu=std::make_shared<digitor::ProcessedGpuFrame>(&planned_renderer,DIGITOR_RENDERER_VULKAN,
   digitor::GpuFrameMetadata{3840,2160,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,digitor::GpuFrameAlpha::straight,8,"scene-linear"},
   7008,std::make_shared<int>(8),gpu_ready,true);
 assert(!digitor::export_frame_matches_contract(gpu_contract,*wrong_gpu));
 const auto request1=adaptive_renderer.begin_request();
 const auto request2=adaptive_renderer.begin_request();
 assert(!adaptive_renderer.request_is_current(request1)&&adaptive_renderer.request_is_current(request2));
 adaptive_renderer.cancel_before(request2+1);
 assert(!adaptive_renderer.request_is_current(request2));
 assert(!adaptive_renderer.accept_completion(request2));
 const auto request3=adaptive_renderer.begin_request();
 assert(adaptive_renderer.accept_completion(request3));
 // Direct GPU encoder handoff validates the sealed contract and exposes an explicit completion fence.
 std::uint64_t encoder_submissions=0;
 digitor::GpuEncoderSubmission gpu_encoder([&](const digitor::ProcessedGpuFrame& submitted,const digitor::ExportFrameContract& submitted_contract,const std::shared_ptr<digitor::ExportCompletionFence>& fence){
   assert(submitted.identity()==7007&&submitted_contract.gpu_frame_identity==7007);
   ++encoder_submissions;fence->complete(DIGITOR_RESULT_OK);return DIGITOR_RESULT_OK;
 });
 const auto export_fence=gpu_encoder.submit(export_gpu,gpu_contract);
 assert(export_fence->wait()&&export_fence->state()==digitor::ExportSubmissionState::completed&&encoder_submissions==1);
 bool bad_submission_rejected=false;try{(void)gpu_encoder.submit(wrong_gpu,gpu_contract);}catch(const std::invalid_argument&){bad_submission_rejected=true;}
 assert(bad_submission_rejected);
 // Platform interop contracts reject backend/target mismatches and retain frames until encoder consumption completes.
 auto d3d_frame=std::make_shared<digitor::ProcessedGpuFrame>(&planned_renderer,DIGITOR_RENDERER_D3D12,
   digitor::GpuFrameMetadata{3840,2160,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,digitor::GpuFrameAlpha::straight,7,"scene-linear"},
   7107,std::make_shared<int>(17),gpu_ready,true);
 const auto d3d_contract=export_renderer.frame_contract(7,export_settings,*d3d_frame);
 digitor::HardwareEncoderTarget windows_target;windows_target.platform=digitor::HardwareEncoderPlatform::windows;
 windows_target.native_handle=0xD312;windows_target.generation=1;windows_target.windows_interop=digitor::WindowsEncoderInterop::d3d12_resource;
 std::shared_ptr<digitor::ExportCompletionFence> held_fence;
 digitor::HardwareEncoderAdapter windows_encoder(windows_target,[&](const auto& frame,const auto&,const auto& target,const auto& fence){
   assert(frame.backend()==DIGITOR_RENDERER_D3D12&&target.native_handle==0xD312);held_fence=fence;return DIGITOR_RESULT_OK;
 });
 const auto windows_fence=windows_encoder.submit(d3d_frame,d3d_contract);
 assert(windows_fence->state()==digitor::ExportSubmissionState::submitted);
 digitor::EncoderResourceRetirement retirement;retirement.retain(d3d_frame,windows_fence);
 assert(retirement.pending()==1&&retirement.collect_completed()==0);
 held_fence->complete(DIGITOR_RESULT_OK);
 assert(retirement.collect_completed()==1&&retirement.pending()==0&&windows_fence->wait());
 bool incompatible_target_rejected=false;try{(void)windows_encoder.submit(export_gpu,gpu_contract);}catch(const std::invalid_argument&){incompatible_target_rejected=true;}
 assert(incompatible_target_rejected);
 digitor::HardwareEncoderTarget android_target;android_target.platform=digitor::HardwareEncoderPlatform::android;
 android_target.native_handle=0xA11D;android_target.generation=4;
 digitor::HardwareEncoderAdapter android_encoder(android_target,[](const auto& frame,const auto&,const auto& target,const auto& fence){
   assert((frame.backend()==DIGITOR_RENDERER_VULKAN||frame.backend()==DIGITOR_RENDERER_OPENGL_ES)&&target.generation==4);
   fence->complete(DIGITOR_RESULT_OK);return DIGITOR_RESULT_OK;
 });
 assert(android_encoder.submit(export_gpu,gpu_contract)->wait());
 // Bounded export queue applies backpressure and deterministic drain/cancel behavior.
 digitor::ExportSubmissionQueue export_queue(1);
 auto queued_fence=std::make_shared<digitor::ExportCompletionFence>();queued_fence->mark_submitted();
 assert(export_queue.try_push(export_gpu,gpu_contract,queued_fence)==digitor::ExportQueuePushResult::accepted);
 auto overflow_fence=std::make_shared<digitor::ExportCompletionFence>();overflow_fence->mark_submitted();
 assert(export_queue.try_push(export_gpu,gpu_contract,overflow_fence)==digitor::ExportQueuePushResult::full);
 digitor::ProcessedGpuFramePtr popped_frame;digitor::ExportFrameContract popped_contract;std::shared_ptr<digitor::ExportCompletionFence> popped_fence;
 assert(export_queue.try_pop(popped_frame,popped_contract,popped_fence)&&popped_frame->identity()==7007);
 popped_fence->complete(DIGITOR_RESULT_OK);assert(export_queue.flush());export_queue.close();
 assert(export_queue.try_push(export_gpu,gpu_contract,overflow_fence)==digitor::ExportQueuePushResult::closed);
 // Export worker bounds in-flight submissions, drains orderly, and times out stalled encoders.
 digitor::ExportSubmissionQueue worker_queue(3);digitor::EncoderResourceRetirement worker_retirement;
 auto wf1=std::make_shared<digitor::ExportCompletionFence>();auto wf2=std::make_shared<digitor::ExportCompletionFence>();
 assert(worker_queue.try_push(export_gpu,gpu_contract,wf1)==digitor::ExportQueuePushResult::accepted);
 assert(worker_queue.try_push(export_gpu,gpu_contract,wf2)==digitor::ExportQueuePushResult::accepted);
 digitor::ExportWorker worker(worker_queue,worker_retirement,[](const auto&,const auto&,const auto& fence){fence->complete(DIGITOR_RESULT_OK);return DIGITOR_RESULT_OK;},{1,std::chrono::milliseconds(100),std::chrono::milliseconds(1)});
 worker.start();worker.request_drain();assert(worker.wait_for_stop(std::chrono::milliseconds(1000)));assert(worker.state()==digitor::ExportWorkerState::stopped&&worker.in_flight()==0);
 digitor::ExportSubmissionQueue timeout_queue(1);digitor::EncoderResourceRetirement timeout_retirement;auto timeout_fence=std::make_shared<digitor::ExportCompletionFence>();
 assert(timeout_queue.try_push(export_gpu,gpu_contract,timeout_fence)==digitor::ExportQueuePushResult::accepted);
 digitor::ExportWorker timeout_worker(timeout_queue,timeout_retirement,[](const auto&,const auto&,const auto&){return DIGITOR_RESULT_OK;},{1,std::chrono::milliseconds(10),std::chrono::milliseconds(1)});
 timeout_worker.start();timeout_worker.request_drain();assert(timeout_worker.wait_for_stop(std::chrono::milliseconds(1000)));assert(timeout_fence->state()==digitor::ExportSubmissionState::failed);
 digitor::HardwareEncoderSession android_session(android_target);assert(android_session.accepts(android_target));
 android_session.recreate(0xA11E);auto recreated_target=android_session.target();assert(recreated_target.generation==5&&!android_session.accepts(android_target)&&android_session.accepts(recreated_target));
 android_session.close();assert(!android_session.open()&&!android_session.accepts(recreated_target));
 // Hash qualification is exact for equal full-quality frames and tolerance-aware for encoded-preview comparisons.
 digitor::VideoFrame hash_a;hash_a.number=3;hash_a.width=2;hash_a.height=1;hash_a.pixels={{0.1f,0.2f,0.3f,1.0f},{0.4f,0.5f,0.6f,1.0f}};
 auto hash_b=hash_a;const auto exact_hash=digitor::qualify_preview_export_hashes(hash_a,hash_b);
 assert(exact_hash.passed&&exact_hash.preview_hash==exact_hash.export_hash);
 hash_b.pixels[0].r+=1.0f/1024.0f;const auto tolerant_hash=digitor::qualify_preview_export_hashes(hash_a,hash_b,40.0,0.99);
 assert(tolerant_hash.passed&&tolerant_hash.preview_hash!=tolerant_hash.export_hash);
 std::cerr<<"SOURCE_POLICY preview=proxy export=original decode_scaled=pass compressed_preview=pass export_proxy_rejection=pass graph_parity=pass quality_modes=pass adaptive_quality=pass dropped_frames=pass fullres_sampling=pass request_sequencing=pass decoder_sampling=pass export_provenance=pass gpu_provenance_crosscheck=pass export_contract=pass gpu_encoder_submission=pass export_fence=pass windows_encoder_interop=pass android_mediacodec_surface=pass resource_retirement=pass export_backpressure=pass encoder_session_generation=pass export_worker=pass encoder_timeout=pass orderly_drain=pass frame_hash_qualification=pass\n";

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
