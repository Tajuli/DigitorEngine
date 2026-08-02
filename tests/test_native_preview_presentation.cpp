#include "digitor/native_preview_presentation.hpp"

#include <atomic>
#include <cassert>
#include <memory>

using namespace digitor;

namespace {
struct Host final : NativePreviewTextureHost {
  const void* context{};
  bool live{true};
  std::uint64_t last{};
  int calls{};
  bool attached() const noexcept override { return live; }
  DigitorRendererBackend backend() const noexcept override { return DIGITOR_RENDERER_VULKAN; }
  const void* device_identity() const noexcept override { return context; }
  DigitorResult present(const ProcessedGpuFramePtr&, std::uint64_t generation) noexcept override {
    ++calls;
    last = generation;
    return DIGITOR_RESULT_OK;
  }
};

ProcessedGpuFramePtr frame(const void* context, std::uint64_t identity,
                           const char* color = "display-srgb") {
  return std::make_shared<ProcessedGpuFrame>(
      context, DIGITOR_RENDERER_VULKAN,
      GpuFrameMetadata{8, 4, DIGITOR_PIXEL_FORMAT_RGBA8_UNORM,
                       GpuFrameAlpha::premultiplied, 10, color},
      identity, std::make_shared<int>(1),
      std::make_shared<std::atomic_bool>(true), false);
}
} // namespace

int main() {
  DigitorSdkSession* sdk{};
  assert(digitor_sdk_create(&sdk) == DIGITOR_RESULT_OK);
  DigitorNativePreviewCapabilities capabilities{};
  capabilities.struct_size = sizeof(capabilities);
  assert(digitor_sdk_query_native_preview(sdk, &capabilities) == DIGITOR_RESULT_OK);
  assert(!capabilities.native_gpu_preview_available && capabilities.cpu_fallback_only);
  assert(digitor_sdk_set_preview_mode(sdk, DIGITOR_PREVIEW_MODE_NATIVE_GPU_STRICT) ==
         DIGITOR_RESULT_OK);
  assert(digitor_sdk_preview_async(sdk, 0, 8, 4, nullptr, nullptr) ==
         DIGITOR_RESULT_BACKEND_UNAVAILABLE);
  DigitorNativeTexture legacy{};
  assert(digitor_sdk_get_native_texture(sdk, &legacy) == DIGITOR_RESULT_UNSUPPORTED);
  assert(digitor_sdk_destroy(sdk) == DIGITOR_RESULT_OK);

  int context{};
  auto host = std::make_shared<Host>();
  host->context = &context;
  NativePreviewPresentationSession presenter(host);

  assert(!presenter.submit(nullptr, 1));
  assert(!presenter.submit(frame(&context, 1, "scene-linear"), 1));
  assert(presenter.submit(frame(&context, 2), 2));
  assert(host->calls == 1 && host->last == 2);
  assert(presenter.submit(frame(&context, 3), 3));
  assert(presenter.submit(frame(&context, 4), 4));
  assert(host->calls == 1); // bounded queue replaces pending without presenting it
  presenter.consumed(2);
  assert(host->calls == 2 && host->last == 4);
  auto stats = presenter.telemetry();
  assert(stats.frames_delivered == 3 && stats.frames_displayed == 1);
  assert(stats.queue_replacements == 1 && stats.cpu_fallback_frames == 0);
  assert(stats.bytes_read_back == 0 && stats.queue_depth == 1);
  assert(!presenter.submit(frame(&context, 5), 4));
  presenter.cancel();
  assert(!presenter.submit(frame(&context, 6), 6));
}
