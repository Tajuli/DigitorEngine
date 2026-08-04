#pragma once

#include "digitor/native_text_render_runtime.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace digitor {

enum class NativeTextBackend : std::uint32_t {
  vulkan,
  d3d12,
  metal,
  gles,
};

enum class NativeTextCommandType : std::uint32_t {
  upload_atlas,
  transition_atlas_for_sampling,
  bind_pipeline,
  bind_atlas,
  bind_vertex_buffer,
  bind_index_buffer,
  set_viewport,
  draw_indexed,
};

struct NativeTextBackendHandles {
  std::uint64_t device{};
  std::uint64_t queue{};
  std::uint64_t command_buffer{};
  std::uint64_t pipeline{};
  std::uint64_t atlas_texture{};
  std::uint64_t atlas_view{};
  std::uint64_t vertex_buffer{};
  std::uint64_t index_buffer{};
};

struct NativeTextCommand {
  NativeTextCommandType type{};
  std::uint64_t resource{};
  std::uint32_t value0{};
  std::uint32_t value1{};
  std::uint32_t value2{};
};

struct NativeTextBackendRequest {
  NativeTextBackend backend{NativeTextBackend::vulkan};
  NativeTextBackendHandles handles;
  AtlasUpload upload;
  TextDrawPacket packet;
  std::uint32_t target_width{};
  std::uint32_t target_height{};
  bool preview{};
};

struct NativeTextBackendResult {
  TextBackendStatus status{TextBackendStatus::unavailable};
  std::vector<NativeTextCommand> commands;
  std::uint64_t digest{};
  std::string diagnostic;
};

using NativeTextCommandSubmitter =
    std::function<bool(NativeTextBackend, const NativeTextBackendHandles&,
                       const std::vector<NativeTextCommand>&)>;

class NativeTextBackendBridge {
 public:
  explicit NativeTextBackendBridge(NativeTextCommandSubmitter submitter);

  NativeTextBackendResult submit(const NativeTextBackendRequest& request);
  void invalidate() noexcept;
  std::uint32_t uploaded_generation(NativeTextBackend backend) const noexcept;

 private:
  NativeTextCommandSubmitter submitter_;
  std::uint32_t uploaded_generations_[4]{};
};

const char* native_text_backend_name(NativeTextBackend backend) noexcept;

}  // namespace digitor
