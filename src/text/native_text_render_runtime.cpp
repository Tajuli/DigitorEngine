#include "digitor/native_text_render_runtime.hpp"

#include <utility>

namespace digitor {
namespace {

std::uint64_t fnv1a_append(std::uint64_t hash, const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

template <typename T>
std::uint64_t append_value(std::uint64_t hash, const T& value) noexcept {
  return fnv1a_append(hash, &value, sizeof(T));
}

}  // namespace

NativeTextRenderRuntime::NativeTextRenderRuntime(AtlasUploadCallback upload,
                                                 TextDrawCallback draw)
    : upload_(std::move(upload)), draw_(std::move(draw)) {}

TextRenderResult NativeTextRenderRuntime::render(const TextRenderRequest& request) {
  TextRenderResult result;
  result.digest = digest_text_packet(request.packet, request.target_width, request.target_height);

  if (request.require_gpu && (!upload_ || !draw_)) {
    result.status = TextBackendStatus::unavailable;
    return result;
  }
  if (!request.packet.gpu_ready || request.packet.vertices.empty() || request.packet.indices.empty()) {
    result.status = TextBackendStatus::draw_failed;
    return result;
  }
  if (request.packet.atlas_generation != request.atlas.generation) {
    result.status = TextBackendStatus::stale_packet;
    return result;
  }
  if (request.atlas.width == 0u || request.atlas.height == 0u || request.target_width == 0u ||
      request.target_height == 0u) {
    result.status = TextBackendStatus::upload_failed;
    return result;
  }

  if (uploaded_generation_ != request.atlas.generation) {
    if (!upload_ || !upload_(request.atlas)) {
      result.status = TextBackendStatus::upload_failed;
      return result;
    }
    uploaded_generation_ = request.atlas.generation;
    result.atlas_uploaded = true;
  }

  if (!draw_ || !draw_(request.packet, request.target_width, request.target_height)) {
    result.status = TextBackendStatus::draw_failed;
    return result;
  }

  result.status = TextBackendStatus::ready;
  result.submitted_vertices = static_cast<std::uint32_t>(request.packet.vertices.size());
  result.submitted_indices = static_cast<std::uint32_t>(request.packet.indices.size());
  return result;
}

std::uint32_t NativeTextRenderRuntime::uploaded_generation() const noexcept {
  return uploaded_generation_;
}

void NativeTextRenderRuntime::invalidate() noexcept { uploaded_generation_ = 0u; }

std::uint64_t digest_text_packet(const TextDrawPacket& packet,
                                 std::uint32_t target_width,
                                 std::uint32_t target_height) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_value(hash, packet.atlas_generation);
  hash = append_value(hash, target_width);
  hash = append_value(hash, target_height);
  if (!packet.vertices.empty()) {
    hash = fnv1a_append(hash, packet.vertices.data(), packet.vertices.size() * sizeof(TextVertex));
  }
  if (!packet.indices.empty()) {
    hash = fnv1a_append(hash, packet.indices.data(), packet.indices.size() * sizeof(std::uint32_t));
  }
  return hash;
}

}  // namespace digitor

extern "C" std::uint64_t digitor_text_packet_digest(const digitor::TextVertex* vertices,
                                                      std::size_t vertex_count,
                                                      const std::uint32_t* indices,
                                                      std::size_t index_count,
                                                      std::uint32_t atlas_generation,
                                                      std::uint32_t target_width,
                                                      std::uint32_t target_height) {
  if ((vertex_count != 0u && vertices == nullptr) || (index_count != 0u && indices == nullptr)) {
    return 0u;
  }
  digitor::TextDrawPacket packet;
  packet.atlas_generation = atlas_generation;
  packet.gpu_ready = vertex_count != 0u && index_count != 0u;
  if (vertex_count != 0u) {
    packet.vertices.assign(vertices, vertices + vertex_count);
  }
  if (index_count != 0u) {
    packet.indices.assign(indices, indices + index_count);
  }
  return digitor::digest_text_packet(packet, target_width, target_height);
}
