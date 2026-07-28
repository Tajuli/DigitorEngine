#include "gpu/gpu_backend.hpp"

#include "core/environment.hpp"
#include "core/string_utils.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <atomic>
#include <string>
#include <vector>
#include <span>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define DIGITOR_WINDOWS_VULKAN 1
#endif
#elif defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace digitor {
namespace { std::atomic_uint64_t next_context_identity{1}; }

IRenderBackend::IRenderBackend()
    : context_lifetime_(std::make_shared<GpuContextLifetime>()),
      context_identity_(next_context_identity.fetch_add(1)) {}

IRenderBackend::~IRenderBackend() {
  if (context_lifetime_) context_lifetime_->retire();
}
namespace {
std::atomic<GpuFailurePoint> failure_point{GpuFailurePoint::None};
std::atomic<std::uint64_t> cpu_reference_count{0};
std::atomic<std::uint64_t> cpu_curve_count{0};
}
void set_gpu_failure_point(GpuFailurePoint point) noexcept { failure_point.store(point); }
GpuFailurePoint gpu_failure_point() noexcept { return failure_point.load(); }
std::span<const GpuFailurePoint> all_gpu_failure_points() noexcept {
  static constexpr GpuFailurePoint points[]{
    GpuFailurePoint::ShaderCompilation, GpuFailurePoint::LibraryCreation,
    GpuFailurePoint::ShaderFunctionLookup, GpuFailurePoint::VertexShaderCreation,
    GpuFailurePoint::VertexShaderCompilation, GpuFailurePoint::FragmentShaderCreation,
    GpuFailurePoint::FragmentShaderCompilation, GpuFailurePoint::ProgramCreation,
    GpuFailurePoint::ProgramLink, GpuFailurePoint::DescriptorSetLayoutCreation,
    GpuFailurePoint::RootSignatureSerialization, GpuFailurePoint::RootSignatureCreation,
    GpuFailurePoint::PipelineLayoutCreation, GpuFailurePoint::PipelineCreation,
    GpuFailurePoint::SourceResourceCreation, GpuFailurePoint::SourceMemoryAllocation,
    GpuFailurePoint::SourceMemoryBinding, GpuFailurePoint::OutputResourceCreation,
    GpuFailurePoint::OutputMemoryAllocation, GpuFailurePoint::OutputMemoryBinding,
    GpuFailurePoint::PreviewDestinationCreation, GpuFailurePoint::SourceResourceStorage,
    GpuFailurePoint::OutputResourceStorage, GpuFailurePoint::PreviewDestinationStorage,
    GpuFailurePoint::LutResourceCreation, GpuFailurePoint::LutUpload,
    GpuFailurePoint::ParameterResourceCreation, GpuFailurePoint::ParameterUpload,
    GpuFailurePoint::BufferMemoryAllocation, GpuFailurePoint::BufferMemoryBinding,
    GpuFailurePoint::DescriptorPoolCreation, GpuFailurePoint::DescriptorHeapCreation,
    GpuFailurePoint::DescriptorSetAllocation, GpuFailurePoint::ImageViewCreation,
    GpuFailurePoint::FramebufferCreation, GpuFailurePoint::FramebufferAttachment,
    GpuFailurePoint::FramebufferValidation, GpuFailurePoint::ShaderResourceViewCreation,
    GpuFailurePoint::CpuSourceShaderResourceViewCreation,
    GpuFailurePoint::GpuSourceShaderResourceViewCreation,
    GpuFailurePoint::LutShaderResourceViewCreation,
    GpuFailurePoint::UnorderedAccessViewCreation, GpuFailurePoint::SourceUpload,
    GpuFailurePoint::UniformLookup, GpuFailurePoint::ResourceBinding,
    GpuFailurePoint::DescriptorUpdate, GpuFailurePoint::SourceUploadRecording,
    GpuFailurePoint::LutUploadRecording, GpuFailurePoint::ParameterUploadRecording,
    GpuFailurePoint::DrawSetup, GpuFailurePoint::CommandPoolCreation,
    GpuFailurePoint::CommandQueueCreation,
    GpuFailurePoint::CommandAllocatorResetOrCreation,
    GpuFailurePoint::CommandBufferOrListAllocation,
    GpuFailurePoint::CommandBufferOrListBeginReset, GpuFailurePoint::CommandRecording,
    GpuFailurePoint::ComputeEncoderCreation,GpuFailurePoint::BlitEncoderCreation,
    GpuFailurePoint::SourceTextureBinding,GpuFailurePoint::OutputTextureBinding,
    GpuFailurePoint::BufferBinding,GpuFailurePoint::DispatchSetup,
    GpuFailurePoint::EncoderCompletion,
    GpuFailurePoint::SourceTransition,GpuFailurePoint::ConsumerDestinationTransition,
    GpuFailurePoint::ValidationTransition,
    GpuFailurePoint::DispatchOrDraw, GpuFailurePoint::Blit,
    GpuFailurePoint::ConsumerCopySubmission, GpuFailurePoint::Flush,
    GpuFailurePoint::CommandBufferOrListClose, GpuFailurePoint::QueueSubmission,
    GpuFailurePoint::FenceSignal,GpuFailurePoint::FenceCreation,
    GpuFailurePoint::EventCreation,GpuFailurePoint::EventSetup,
    GpuFailurePoint::SynchronizationWait,GpuFailurePoint::SynchronizationVerification,
    GpuFailurePoint::CommandStatusVerification,
    GpuFailurePoint::ProcessedFrameCreation, GpuFailurePoint::PreviewAcquisition,
    GpuFailurePoint::PreviewPresentation,
    GpuFailurePoint::ValidationReadbackResourceCreation,
    GpuFailurePoint::ValidationReadbackCopy, GpuFailurePoint::ValidationReadbackMap,
    GpuFailurePoint::CpuReadbackCopy,
    GpuFailurePoint::ValidationCpuCopy,
    GpuFailurePoint::DeterministicOutOfMemory, GpuFailurePoint::DeviceLost};
  return points;
}
const char* gpu_failure_point_name(GpuFailurePoint p) noexcept {
  switch(p) {
#define DIGITOR_STAGE(x) case GpuFailurePoint::x: return #x
    DIGITOR_STAGE(None); DIGITOR_STAGE(ShaderCompilation); DIGITOR_STAGE(LibraryCreation);
    DIGITOR_STAGE(ShaderFunctionLookup); DIGITOR_STAGE(DescriptorSetLayoutCreation);
    DIGITOR_STAGE(VertexShaderCreation); DIGITOR_STAGE(VertexShaderCompilation);
    DIGITOR_STAGE(FragmentShaderCreation); DIGITOR_STAGE(FragmentShaderCompilation);
    DIGITOR_STAGE(ProgramCreation); DIGITOR_STAGE(ProgramLink);
    DIGITOR_STAGE(RootSignatureSerialization); DIGITOR_STAGE(RootSignatureCreation);
    DIGITOR_STAGE(PipelineLayoutCreation); DIGITOR_STAGE(PipelineCreation);
    DIGITOR_STAGE(SourceResourceCreation); DIGITOR_STAGE(SourceMemoryAllocation);
    DIGITOR_STAGE(SourceMemoryBinding); DIGITOR_STAGE(OutputResourceCreation);
    DIGITOR_STAGE(OutputMemoryAllocation); DIGITOR_STAGE(OutputMemoryBinding);
    DIGITOR_STAGE(PreviewDestinationCreation); DIGITOR_STAGE(LutResourceCreation);
    DIGITOR_STAGE(SourceResourceStorage); DIGITOR_STAGE(OutputResourceStorage);
    DIGITOR_STAGE(PreviewDestinationStorage);
    DIGITOR_STAGE(LutUpload); DIGITOR_STAGE(ParameterResourceCreation);
    DIGITOR_STAGE(ParameterUpload); DIGITOR_STAGE(BufferMemoryAllocation);
    DIGITOR_STAGE(BufferMemoryBinding); DIGITOR_STAGE(DescriptorPoolCreation);
    DIGITOR_STAGE(DescriptorHeapCreation); DIGITOR_STAGE(DescriptorSetAllocation);
    DIGITOR_STAGE(ImageViewCreation); DIGITOR_STAGE(ShaderResourceViewCreation);
    DIGITOR_STAGE(CpuSourceShaderResourceViewCreation);
    DIGITOR_STAGE(GpuSourceShaderResourceViewCreation);
    DIGITOR_STAGE(LutShaderResourceViewCreation);
    DIGITOR_STAGE(FramebufferCreation); DIGITOR_STAGE(FramebufferAttachment);
    DIGITOR_STAGE(FramebufferValidation);
    DIGITOR_STAGE(UnorderedAccessViewCreation); DIGITOR_STAGE(SourceUpload);
    DIGITOR_STAGE(UniformLookup); DIGITOR_STAGE(ResourceBinding); DIGITOR_STAGE(DrawSetup);
    DIGITOR_STAGE(DescriptorUpdate); DIGITOR_STAGE(SourceUploadRecording);
    DIGITOR_STAGE(LutUploadRecording); DIGITOR_STAGE(ParameterUploadRecording);
    DIGITOR_STAGE(CommandPoolCreation); DIGITOR_STAGE(CommandAllocatorResetOrCreation);
    DIGITOR_STAGE(CommandQueueCreation);
    DIGITOR_STAGE(CommandBufferOrListAllocation); DIGITOR_STAGE(CommandBufferOrListBeginReset);
    DIGITOR_STAGE(CommandRecording); DIGITOR_STAGE(DispatchOrDraw);
    DIGITOR_STAGE(ComputeEncoderCreation); DIGITOR_STAGE(BlitEncoderCreation);
    DIGITOR_STAGE(SourceTextureBinding); DIGITOR_STAGE(OutputTextureBinding);
    DIGITOR_STAGE(BufferBinding); DIGITOR_STAGE(DispatchSetup);
    DIGITOR_STAGE(EncoderCompletion);
    DIGITOR_STAGE(SourceTransition); DIGITOR_STAGE(ConsumerDestinationTransition);
    DIGITOR_STAGE(ValidationTransition);
    DIGITOR_STAGE(Blit); DIGITOR_STAGE(ConsumerCopySubmission); DIGITOR_STAGE(Flush);
    DIGITOR_STAGE(CommandBufferOrListClose); DIGITOR_STAGE(QueueSubmission);
    DIGITOR_STAGE(FenceSignal); DIGITOR_STAGE(FenceCreation);
    DIGITOR_STAGE(EventCreation); DIGITOR_STAGE(EventSetup);
    DIGITOR_STAGE(SynchronizationWait); DIGITOR_STAGE(SynchronizationVerification);
    DIGITOR_STAGE(CommandStatusVerification);
    DIGITOR_STAGE(ProcessedFrameCreation); DIGITOR_STAGE(PreviewAcquisition);
    DIGITOR_STAGE(PreviewPresentation); DIGITOR_STAGE(ValidationReadbackResourceCreation);
    DIGITOR_STAGE(ValidationReadbackCopy); DIGITOR_STAGE(ValidationReadbackMap);
    DIGITOR_STAGE(CpuReadbackCopy);
    DIGITOR_STAGE(ValidationCpuCopy);
    DIGITOR_STAGE(DeterministicOutOfMemory); DIGITOR_STAGE(DeviceLost);
    DIGITOR_STAGE(ReflectionValidation);
#undef DIGITOR_STAGE
  }
  return "Unknown";
}
void note_cpu_color_reference() noexcept { ++cpu_reference_count; }
std::uint64_t cpu_color_reference_count() noexcept { return cpu_reference_count.load(); }
void reset_cpu_color_reference_count() noexcept { cpu_reference_count.store(0); }
void note_cpu_curve_reference() noexcept { ++cpu_curve_count; }
std::uint64_t cpu_curve_reference_count() noexcept { return cpu_curve_count.load(); }
void IRenderBackend::begin_grade_provenance(
    DigitorRendererBackend backend, bool gpu, const char *device,
    const char *compiler, const char *shader, const char *pipeline) noexcept {
  provenance_ = {};
  provenance_.selected_backend = backend;
  provenance_.gpu_execution = gpu;
  provenance_.native_device_identity = device ? device : "";
  provenance_.compiler_identity = compiler ? compiler : "";
  provenance_.compiled_shader_identity = shader ? shader : "";
  provenance_.native_pipeline_identity = pipeline ? pipeline : "";
  provenance_.cpu_color_reference_invocations = cpu_color_reference_count();
}
DigitorResult IRenderBackend::injected_failure(GpuFailurePoint point) noexcept {
  if (failure_point.load() != point)
    return DIGITOR_RESULT_OK;
  failure_point.store(GpuFailurePoint::None);
  provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
  provenance_.native_error_code = -static_cast<std::int64_t>(point);
  provenance_.device_lost = point == GpuFailurePoint::DeviceLost;
  return point == GpuFailurePoint::DeterministicOutOfMemory ? DIGITOR_RESULT_OUT_OF_MEMORY
                                               : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}
DigitorResult IRenderBackend::inject_at(GpuFailurePoint point, const char* operation) noexcept {
  if (failure_point.load() != point) return DIGITOR_RESULT_OK;
  provenance_.requested_failure_point = point;
  provenance_.actual_stage_reached = point;
  provenance_.failure_stage = gpu_failure_point_name(point);
  provenance_.failure_operation = operation ? operation : "";
  provenance_.failure_backend = provenance_.native_device_identity;
  provenance_.output_cleared = !provenance_.output_written;
  const auto result = injected_failure(point);
  provenance_.failure_result = result;
  return result;
}
bool gpu_validation_requested() noexcept {
  const auto value = environment_variable("DIGITOR_GPU_VALIDATION");
  return value && value->front() != '0';
}
DigitorResult IRenderBackend::create_texture(const DigitorTextureDesc &,
                                             void **out) noexcept {
  if (out)
    *out = nullptr;
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::create_buffer(const DigitorBufferDesc &,
                                            void **out) noexcept {
  if (out)
    *out = nullptr;
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::create_sampler(const DigitorSamplerDesc &,
                                             void **out) noexcept {
  if (out)
    *out = nullptr;
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::map_buffer(void *, uint64_t, uint64_t,
                                         void **out) noexcept {
  if (out)
    *out = nullptr;
  return DIGITOR_RESULT_UNSUPPORTED;
}
void IRenderBackend::unmap_buffer(void *) noexcept {}
void IRenderBackend::destroy_texture(void *) noexcept {}
void IRenderBackend::destroy_buffer(void *) noexcept {}
void IRenderBackend::destroy_sampler(void *) noexcept {}
DigitorResult IRenderBackend::render_rgba8(uint32_t, uint32_t,
                                           std::span<const uint8_t>,
                                           std::vector<uint8_t> &) noexcept {
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::grade_rgba32f(std::span<const Color>,
                                            std::span<Color>,
                                            const ColorGrade &) noexcept {
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::curves_rgba32f(std::span<const Color> source,
                                             std::span<Color> destination,
                                             const CompiledRgbCurves& curves) noexcept {
  const auto before = cpu_curve_reference_count();
  const auto result = execute_curves_rgba32f(source, destination, curves);
  provenance_.cpu_curve_invocations = cpu_curve_reference_count() - before;
  if (provenance_.readback_performed)
    provenance_.preview_source = PreviewSource::cpu_validation;
  return result;
}
DigitorResult IRenderBackend::execute_curves_rgba32f(std::span<const Color>, std::span<Color>,
                                                     const CompiledRgbCurves&) noexcept {
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::process_curves_gpu(
    std::span<const Color> source, std::uint32_t width, std::uint32_t height,
    std::int64_t timestamp, const CompiledRgbCurves& curves,
    ProcessedGpuFramePtr& out) noexcept {
  out.reset();
  const auto before = cpu_curve_reference_count();
  const auto result = execute_process_curves_gpu(source, width, height, timestamp,
                                                 curves, out);
  provenance_.cpu_curve_invocations = cpu_curve_reference_count() - before;
  if (result != DIGITOR_RESULT_OK || !out ||
      provenance_.cpu_curve_invocations != 0 ||
      provenance_.curve_fallback_invocations != 0 ||
      provenance_.readback_performed) {
    out.reset();
    return result == DIGITOR_RESULT_OK ? DIGITOR_RESULT_INTERNAL_ERROR : result;
  }
  out->bind_context_lifetime(context_lifetime_);
  provenance_.preview_source = PreviewSource::gpu;
  return DIGITOR_RESULT_OK;
}
DigitorResult IRenderBackend::present_gpu_frame(const ProcessedGpuFramePtr& frame) noexcept {
  provenance_.direct_preview_consumed = false;
  if (!frame) return DIGITOR_RESULT_INVALID_ARGUMENT;
  const auto result = execute_present_gpu_frame(frame);
  provenance_.direct_preview_consumed = result == DIGITOR_RESULT_OK;
  return result;
}
DigitorResult IRenderBackend::create_preview_consumer(const ProcessedGpuFramePtr& frame,
    std::shared_ptr<PreviewConsumerDestination>& out) noexcept {
  out.reset();
  if (!frame || !frame->ready() || frame->backend() != info().backend)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  return execute_create_preview_consumer(frame, out);
}
DigitorResult IRenderBackend::process_primary_wheels_gpu(std::span<const Color>s,std::uint32_t w,std::uint32_t h,std::int64_t ts,const PrimaryWheelsParameters&p,ProcessedGpuFramePtr&out)noexcept{
 out.reset();const auto before=primary_wheels_reference_count();
 const auto result=execute_process_primary_wheels_gpu(s,w,h,ts,p,out);
 provenance_.cpu_primary_wheels_invocations=primary_wheels_reference_count()-before;
 if(result!=DIGITOR_RESULT_OK||!out||provenance_.cpu_primary_wheels_invocations||provenance_.primary_wheels_fallback_invocations||provenance_.normal_preview_readback_count){out.reset();return result==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:result;}
 out->bind_context_lifetime(context_lifetime_);provenance_.preview_source=PreviewSource::gpu;return DIGITOR_RESULT_OK;
}
DigitorResult IRenderBackend::execute_process_primary_wheels_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&out)noexcept{out.reset();return DIGITOR_RESULT_UNSUPPORTED;}
GpuSourceResource IRenderBackend::gpu_source(const ProcessedGpuFramePtr& frame)const noexcept{
 GpuSourceResource source;if(!frame||frame->backend()!=info().backend||!frame->context_live())return source;
 const auto&m=frame->metadata();source.backend=frame->backend();source.context_identity=context_identity_;source.width=m.width;source.height=m.height;source.format=m.format;source.color_metadata_identity=m.color_metadata;source.source_identity=std::to_string(frame->identity());source.readiness=frame->ready()?GpuReadiness::Ready:GpuReadiness::Pending;source.frame=frame;return source;
}
DigitorResult IRenderBackend::process_curves_gpu(const GpuSourceResource&s,std::int64_t ts,const CompiledRgbCurves&c,ProcessedGpuFramePtr&out)noexcept{
 out.reset();if(!s.usable_by(info().backend,context_identity_))return DIGITOR_RESULT_INVALID_ARGUMENT;const auto before=cpu_curve_reference_count();auto result=execute_process_curves_gpu(s,ts,c,out);provenance_.cpu_curve_invocations=cpu_curve_reference_count()-before;if(result!=DIGITOR_RESULT_OK||!out||provenance_.cpu_curve_invocations||provenance_.curve_fallback_invocations||provenance_.readback_performed){out.reset();return result==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:result;}out->bind_context_lifetime(context_lifetime_);return DIGITOR_RESULT_OK;
}
DigitorResult IRenderBackend::process_primary_wheels_gpu(const GpuSourceResource&s,std::int64_t ts,const PrimaryWheelsParameters&p,ProcessedGpuFramePtr&out)noexcept{
 out.reset();if(!s.usable_by(info().backend,context_identity_))return DIGITOR_RESULT_INVALID_ARGUMENT;const auto before=primary_wheels_reference_count();auto result=execute_process_primary_wheels_gpu(s,ts,p,out);provenance_.cpu_primary_wheels_invocations=primary_wheels_reference_count()-before;if(result!=DIGITOR_RESULT_OK||!out||provenance_.cpu_primary_wheels_invocations||provenance_.primary_wheels_fallback_invocations||provenance_.normal_preview_readback_count){out.reset();return result==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:result;}out->bind_context_lifetime(context_lifetime_);return DIGITOR_RESULT_OK;
}
DigitorResult IRenderBackend::execute_process_curves_gpu(const GpuSourceResource&,std::int64_t,const CompiledRgbCurves&,ProcessedGpuFramePtr&out)noexcept{out.reset();return DIGITOR_RESULT_UNSUPPORTED;}
DigitorResult IRenderBackend::execute_process_primary_wheels_gpu(const GpuSourceResource&,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&out)noexcept{out.reset();return DIGITOR_RESULT_UNSUPPORTED;}
DigitorResult IRenderBackend::validation_readback_primary_wheels(const ProcessedGpuFramePtr&frame,std::span<Color>out)noexcept{if(!frame||!frame->validation_readback_supported())return DIGITOR_RESULT_UNSUPPORTED;const auto result=execute_validation_readback_primary_wheels(frame,out);provenance_.validation_readback_completed=result==DIGITOR_RESULT_OK;return result;}
DigitorResult IRenderBackend::execute_validation_readback_primary_wheels(const ProcessedGpuFramePtr&,std::span<Color>)noexcept{return DIGITOR_RESULT_UNSUPPORTED;}
DigitorResult IRenderBackend::execute_process_curves_gpu(
    std::span<const Color>, std::uint32_t, std::uint32_t, std::int64_t,
    const CompiledRgbCurves&, ProcessedGpuFramePtr& out) noexcept {
  out.reset(); return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::execute_present_gpu_frame(const ProcessedGpuFramePtr&) noexcept {
  return DIGITOR_RESULT_UNSUPPORTED;
}
DigitorResult IRenderBackend::execute_create_preview_consumer(
    const ProcessedGpuFramePtr&, std::shared_ptr<PreviewConsumerDestination>& out) noexcept {
  out.reset(); return DIGITOR_RESULT_UNSUPPORTED;
}
namespace {

class DeviceBackend final : public IRenderBackend {
public:
  explicit DeviceBackend(DigitorRendererInfo info) : info_(info) {}
  bool initialize(bool) override { return true; }
  void shutdown() noexcept override {}
  DigitorRendererInfo info() const noexcept override { return info_; }

private:
  DigitorRendererInfo info_{};
};

[[maybe_unused]] DigitorRendererInfo make_info(DigitorRendererBackend backend,
                                               const char *backend_name,
                                               const char *device_name,
                                               bool compute, bool fp16) {
  DigitorRendererInfo info{};
  info.backend = backend;
  copy_bounded(info.backend_name, backend_name != nullptr
                                      ? std::string_view(backend_name)
                                      : std::string_view{});
  copy_bounded(info.device_name, device_name != nullptr
                                     ? std::string_view(device_name)
                                     : std::string_view{});
  info.is_gpu = 1;
  info.supports_compute = compute;
  info.supports_fp16 = fp16;
  info.supports_fp32 = 1;
  return info;
}

[[maybe_unused]] std::optional<DigitorRendererInfo> discover(DigitorRendererBackend backend) {
#if defined(_WIN32)
  if (backend == DIGITOR_RENDERER_VULKAN) {
#if defined(DIGITOR_WINDOWS_VULKAN)
    HMODULE library = LoadLibraryA("vulkan-1.dll");
    if (!library)
      return std::nullopt;
    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(library, "vkGetInstanceProcAddr"));
    auto create = get_proc ? reinterpret_cast<PFN_vkCreateInstance>(
                                 get_proc(nullptr, "vkCreateInstance"))
                           : nullptr;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          nullptr,
                          "DigitorEngine",
                          1,
                          "DigitorEngine",
                          VK_MAKE_VERSION(0, 2, 0),
                          VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                            nullptr,
                            0,
                            &app,
                            0,
                            nullptr,
                            0,
                            nullptr};
    VkInstance instance{};
    if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) {
      FreeLibrary(library);
      return std::nullopt;
    }
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get_proc(instance, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get_proc(instance, "vkGetPhysicalDeviceProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(
        get_proc(instance, "vkDestroyInstance"));
    uint32_t count = 0;
    std::optional<DigitorRendererInfo> result;
    if (enumerate && properties &&
        enumerate(instance, &count, nullptr) == VK_SUCCESS && count) {
      std::vector<VkPhysicalDevice> devices(count);
      if (enumerate(instance, &count, devices.data()) == VK_SUCCESS) {
        VkPhysicalDeviceProperties p{};
        properties(devices[0], &p);
        result = make_info(backend, "Vulkan", p.deviceName, true, false);
      }
    }
    if (destroy)
      destroy(instance, nullptr);
    FreeLibrary(library);
    return result;
#else
    // Vulkan remains optional at build time; the D3D12 policy fallback still
    // works without an SDK.
    return std::nullopt;
#endif
  }
  if (backend == DIGITOR_RENDERER_D3D12) {
    IDXGIFactory6 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
      return std::nullopt;
    std::optional<DigitorRendererInfo> result;
    for (UINT index = 0;; ++index) {
      IDXGIAdapter1 *adapter = nullptr;
      if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
        break;
      DXGI_ADAPTER_DESC1 description{};
      adapter->GetDesc1(&description);
      ID3D12Device *device = nullptr;
      if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
          SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&device)))) {
        char name[128] = "Direct3D 12 Adapter";
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name,
                            static_cast<int>(sizeof(name)), nullptr, nullptr);
        result = make_info(backend, "Direct3D 12", name, true, true);
        device->Release();
        adapter->Release();
        break;
      }
      adapter->Release();
    }
    factory->Release();
    return result;
  }
#elif defined(__ANDROID__)
  if (backend == DIGITOR_RENDERER_VULKAN) {
    void *library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!library)
      return std::nullopt;
    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(library, "vkGetInstanceProcAddr"));
    if (!get_proc) {
      dlclose(library);
      return std::nullopt;
    }
    auto create = reinterpret_cast<PFN_vkCreateInstance>(
        get_proc(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          nullptr,
                          "DigitorEngine",
                          1,
                          "DigitorEngine",
                          VK_MAKE_VERSION(0, 2, 0),
                          VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                            nullptr,
                            0,
                            &app,
                            0,
                            nullptr,
                            0,
                            nullptr};
    VkInstance instance{};
    if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) {
      dlclose(library);
      return std::nullopt;
    }
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get_proc(instance, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get_proc(instance, "vkGetPhysicalDeviceProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(
        get_proc(instance, "vkDestroyInstance"));
    uint32_t count = 0;
    std::optional<DigitorRendererInfo> result;
    if (enumerate && properties &&
        enumerate(instance, &count, nullptr) == VK_SUCCESS && count) {
      std::vector<VkPhysicalDevice> devices(count);
      if (enumerate(instance, &count, devices.data()) == VK_SUCCESS) {
        VkPhysicalDeviceProperties p{};
        properties(devices[0], &p);
        result = make_info(backend, "Vulkan", p.deviceName, true, false);
      }
    }
    if (destroy)
      destroy(instance, nullptr);
    dlclose(library);
    return result;
  }
  if (backend == DIGITOR_RENDERER_OPENGL_ES) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
      return std::nullopt;
    const char *vendor = eglQueryString(display, EGL_VENDOR);
    auto result =
        make_info(backend, "OpenGL ES", vendor ? vendor : "Android GLES device",
                  false, false);
    eglTerminate(display);
    return result;
  }
#elif defined(__APPLE__)
  if (backend == DIGITOR_RENDERER_METAL) {
    using CreateDevice = id (*)();
    auto create = reinterpret_cast<CreateDevice>(
        dlsym(RTLD_DEFAULT, "MTLCreateSystemDefaultDevice"));
    id device = create ? create() : nullptr;
    if (!device)
      return std::nullopt;
    using SendId = id (*)(id, SEL);
    using SendCString = const char *(*)(id, SEL);
    id name = reinterpret_cast<SendId>(objc_msgSend)(device,
                                                     sel_registerName("name"));
    const char *utf8 = name ? reinterpret_cast<SendCString>(objc_msgSend)(
                                  name, sel_registerName("UTF8String"))
                            : nullptr;
    return make_info(backend, "Metal", utf8 ? utf8 : "Apple GPU", true, true);
  }
#endif
  (void)backend;
  return std::nullopt;
}

std::vector<DigitorRendererBackend> platform_order(HostPlatform platform) {
  switch (platform) {
  case HostPlatform::Windows:
    return {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_D3D12};
  case HostPlatform::Android:
    return {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_OPENGL_ES};
  case HostPlatform::IOS:
  case HostPlatform::MacOS:
    return {DIGITOR_RENDERER_METAL};
  default:
    return {};
  }
}
} // namespace

std::unique_ptr<IRenderBackend>
select_gpu_backend(HostPlatform platform, DigitorRendererBackend preferred,
                   const BackendFactory &factory) {
  auto order = platform_order(platform);
  if (preferred != DIGITOR_RENDERER_AUTO && preferred != DIGITOR_RENDERER_CPU) {
    order.erase(std::remove(order.begin(), order.end(), preferred),
                order.end());
    order.insert(order.begin(), preferred);
  } else if (preferred == DIGITOR_RENDERER_CPU) {
    return nullptr;
  }
  for (auto candidate : order)
    if (auto backend = factory(candidate))
      return backend;
  return nullptr;
}

std::unique_ptr<IRenderBackend>
create_gpu_backend(DigitorRendererBackend preferred) {
  return select_gpu_backend(
      current_platform(), preferred,
      [](DigitorRendererBackend backend) -> std::unique_ptr<IRenderBackend> {
        // Adapter discovery alone is not a rendering backend. Returning a
        // probe-only DeviceBackend here used to let initialization claim a GPU
        // while every resource and rendering operation was unsupported.
        return create_native_backend(backend);
      });
}
} // namespace digitor
