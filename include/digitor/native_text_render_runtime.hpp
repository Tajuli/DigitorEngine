#pragma once

#include "digitor/native_text_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace digitor {

enum class TextRenderConsumer : std::uint32_t { preview, export_frame };
enum class TextBackendStatus : std::uint32_t { ready, unavailable, upload_failed, draw_failed, stale_packet };

struct AtlasUpload {
  std::uint32_t generation{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t page{};
  std::vector<std::uint8_t> coverage;
};

struct TextRenderRequest {
  TextDrawPacket packet;
  AtlasUpload atlas;
  TextRenderConsumer consumer{TextRenderConsumer::preview};
  std::uint32_t target_width{};
  std::uint32_t target_height{};
  bool require_gpu{true};
};

struct TextRenderResult {
  TextBackendStatus status{TextBackendStatus::unavailable};
  std::uint64_t digest{};
  std::uint32_t submitted_vertices{};
  std::uint32_t submitted_indices{};
  bool atlas_uploaded{};
};

using AtlasUploadCallback = std::function<bool(const AtlasUpload&)>;
using TextDrawCallback = std::function<bool(const TextDrawPacket&, std::uint32_t, std::uint32_t)>;

class NativeTextRenderRuntime {
 public:
  NativeTextRenderRuntime(AtlasUploadCallback upload, TextDrawCallback draw);
  TextRenderResult render(const TextRenderRequest& request);
  std::uint32_t uploaded_generation() const noexcept;
  void invalidate() noexcept;

 private:
  AtlasUploadCallback upload_;
  TextDrawCallback draw_;
  std::uint32_t uploaded_generation_{};
};

std::uint64_t digest_text_packet(const TextDrawPacket& packet,
                                 std::uint32_t target_width,
                                 std::uint32_t target_height) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorTextRenderSummary {
  std::uint32_t status;
  std::uint32_t submitted_vertices;
  std::uint32_t submitted_indices;
  std::uint32_t atlas_uploaded;
  std::uint64_t digest;
};

std::uint64_t digitor_text_packet_digest(const digitor::TextVertex* vertices,
                                         std::size_t vertex_count,
                                         const std::uint32_t* indices,
                                         std::size_t index_count,
                                         std::uint32_t atlas_generation,
                                         std::uint32_t target_width,
                                         std::uint32_t target_height);

}
