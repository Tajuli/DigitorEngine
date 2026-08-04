#include "digitor/native_text_backend_bridge.hpp"

#include <array>
#include <sstream>

namespace digitor {
namespace {

std::size_t backend_index(NativeTextBackend backend) noexcept {
  return static_cast<std::size_t>(backend);
}

bool valid_handles(const NativeTextBackendHandles& handles) noexcept {
  return handles.device != 0u && handles.queue != 0u && handles.command_buffer != 0u &&
         handles.pipeline != 0u && handles.atlas_texture != 0u && handles.atlas_view != 0u &&
         handles.vertex_buffer != 0u && handles.index_buffer != 0u;
}

NativeTextCommand make_command(NativeTextCommandType type, std::uint64_t resource = 0u,
                               std::uint32_t value0 = 0u, std::uint32_t value1 = 0u,
                               std::uint32_t value2 = 0u) {
  NativeTextCommand command;
  command.type = type;
  command.resource = resource;
  command.value0 = value0;
  command.value1 = value1;
  command.value2 = value2;
  return command;
}

}  // namespace

NativeTextBackendBridge::NativeTextBackendBridge(NativeTextCommandSubmitter submitter)
    : submitter_(std::move(submitter)) {}

NativeTextBackendResult NativeTextBackendBridge::submit(const NativeTextBackendRequest& request) {
  NativeTextBackendResult result;
  result.digest = digest_text_packet(request.packet, request.target_width, request.target_height);

  if (!submitter_) {
    result.status = TextBackendStatus::unavailable;
    result.diagnostic = "native submitter unavailable";
    return result;
  }
  if (!valid_handles(request.handles)) {
    result.status = TextBackendStatus::unavailable;
    result.diagnostic = "required native backend handle is missing";
    return result;
  }
  if (!request.packet.gpu_ready || request.packet.vertices.empty() || request.packet.indices.empty()) {
    result.status = TextBackendStatus::draw_failed;
    result.diagnostic = "text packet is not GPU ready";
    return result;
  }
  if (request.packet.atlas_generation != request.upload.generation) {
    result.status = TextBackendStatus::stale_packet;
    result.diagnostic = "text packet and atlas generations differ";
    return result;
  }
  if (request.upload.width == 0u || request.upload.height == 0u || request.target_width == 0u ||
      request.target_height == 0u) {
    result.status = TextBackendStatus::upload_failed;
    result.diagnostic = "atlas or render target dimensions are zero";
    return result;
  }

  const auto index = backend_index(request.backend);
  if (uploaded_generations_[index] != request.upload.generation) {
    result.commands.push_back(make_command(NativeTextCommandType::upload_atlas,
                                           request.handles.atlas_texture, request.upload.width,
                                           request.upload.height, request.upload.generation));
    result.commands.push_back(make_command(NativeTextCommandType::transition_atlas_for_sampling,
                                           request.handles.atlas_texture));
  }

  result.commands.push_back(
      make_command(NativeTextCommandType::bind_pipeline, request.handles.pipeline));
  result.commands.push_back(
      make_command(NativeTextCommandType::bind_atlas, request.handles.atlas_view));
  result.commands.push_back(
      make_command(NativeTextCommandType::bind_vertex_buffer, request.handles.vertex_buffer,
                   static_cast<std::uint32_t>(request.packet.vertices.size())));
  result.commands.push_back(
      make_command(NativeTextCommandType::bind_index_buffer, request.handles.index_buffer,
                   static_cast<std::uint32_t>(request.packet.indices.size())));
  result.commands.push_back(make_command(NativeTextCommandType::set_viewport, 0u,
                                         request.target_width, request.target_height));
  result.commands.push_back(make_command(
      NativeTextCommandType::draw_indexed, request.handles.command_buffer,
      static_cast<std::uint32_t>(request.packet.indices.size()), 1u, request.preview ? 1u : 0u));

  if (!submitter_(request.backend, request.handles, result.commands)) {
    result.status = TextBackendStatus::draw_failed;
    result.diagnostic = "native backend rejected the command list";
    return result;
  }

  uploaded_generations_[index] = request.upload.generation;
  result.status = TextBackendStatus::ready;
  std::ostringstream diagnostic;
  diagnostic << native_text_backend_name(request.backend) << " commands=" << result.commands.size();
  result.diagnostic = diagnostic.str();
  return result;
}

void NativeTextBackendBridge::invalidate() noexcept {
  for (auto& generation : uploaded_generations_) {
    generation = 0u;
  }
}

std::uint32_t NativeTextBackendBridge::uploaded_generation(NativeTextBackend backend) const noexcept {
  return uploaded_generations_[backend_index(backend)];
}

const char* native_text_backend_name(NativeTextBackend backend) noexcept {
  switch (backend) {
    case NativeTextBackend::vulkan:
      return "vulkan";
    case NativeTextBackend::d3d12:
      return "d3d12";
    case NativeTextBackend::metal:
      return "metal";
    case NativeTextBackend::gles:
      return "gles";
  }
  return "unknown";
}

}  // namespace digitor
