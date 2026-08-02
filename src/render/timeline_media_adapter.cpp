#include "digitor/timeline_media_adapter.hpp"

#include <utility>

namespace digitor {

TimelineMediaAdapter::TimelineMediaAdapter(std::vector<TimelineMediaSource> sources,
                                           MediaAdapterCallbacks callbacks,
                                           bool require_zero_copy)
    : callbacks_(std::move(callbacks)), require_zero_copy_(require_zero_copy) {
  for (auto& source : sources) {
    if (!source.clip_id.empty() && !source.original_path.empty()) {
      sources_.insert_or_assign(source.clip_id, std::move(source));
    }
  }
}

bool TimelineMediaAdapter::has_source(const std::string& clip_id) const noexcept {
  return sources_.contains(clip_id);
}

std::optional<std::string> TimelineMediaAdapter::selected_path(const std::string& clip_id,
                                                               TimelineExecutionMode mode,
                                                               bool allow_proxy) const {
  const auto it = sources_.find(clip_id);
  if (it == sources_.end()) return std::nullopt;
  const auto& source = it->second;
  const bool proxy = mode == TimelineExecutionMode::preview && allow_proxy &&
                     source.prefer_proxy_for_preview && !source.proxy_path.empty();
  return proxy ? source.proxy_path : source.original_path;
}

TimelineRenderCallbacks TimelineMediaAdapter::make_render_callbacks(TimelineExecutionMode mode,
                                                                     std::uint32_t width,
                                                                     std::uint32_t height,
                                                                     bool allow_proxy) const {
  TimelineRenderCallbacks out;
  out.decode_video = [this, mode, width, height, allow_proxy](const VideoExecutionLayer& layer,
                                                              bool runtime_allows_proxy) {
    if (!callbacks_.decode_video) return std::optional<RenderVideoFrame>{};
    const auto path = selected_path(layer.clip_id, mode, allow_proxy && runtime_allows_proxy);
    if (!path) return std::optional<RenderVideoFrame>{};
    const auto& source = sources_.at(layer.clip_id);
    const bool proxy = *path == source.proxy_path && !source.proxy_path.empty();
    auto frame = callbacks_.decode_video(MediaDecodeRequest{layer.clip_id, *path,
                                                             layer.source_time_us, width, height,
                                                             proxy, mode, require_zero_copy_});
    if (require_zero_copy_ && (!frame || !frame->gpu_resident())) return std::optional<RenderVideoFrame>{};
    return frame;
  };
  out.decode_audio = [this, mode, width, height](const AudioExecutionLayer& layer,
                                                 std::size_t frames) {
    if (!callbacks_.decode_audio) return std::optional<RenderAudioBlock>{};
    const auto path = selected_path(layer.clip_id, TimelineExecutionMode::export_render, false);
    if (!path) return std::optional<RenderAudioBlock>{};
    return callbacks_.decode_audio(MediaDecodeRequest{layer.clip_id, *path, layer.source_time_us,
                                                       width, height, false, mode, false}, frames);
  };
  out.apply_effects = [this](const VideoExecutionLayer& layer, RenderVideoFrame& frame) {
    if (require_zero_copy_ && !frame.gpu_resident()) return false;
    if (!callbacks_.apply_effects) return true;
    const bool applied = callbacks_.apply_effects(layer, frame);
    return applied && (!require_zero_copy_ || frame.gpu_resident());
  };
  out.composite = [this](const VideoExecutionLayer& layer,
                         const RenderVideoFrame& input,
                         RenderVideoFrame& output) {
    if (require_zero_copy_ && (!input.gpu_resident() || !output.gpu_resident())) return false;
    if (!callbacks_.composite) return false;
    const bool composited = callbacks_.composite(layer, input, output);
    return composited && (!require_zero_copy_ || output.gpu_resident());
  };
  out.create_gpu_target = callbacks_.create_gpu_target;
  out.cancelled = callbacks_.cancelled;
  return out;
}

bool TimelineMediaAdapter::deliver(const TimelineRenderResult& result,
                                   const TimelineExecutionPlan& plan) const {
  if (!result.success || result.cancelled) return false;
  if (require_zero_copy_ && (!result.gpu_resident || !result.video.gpu_resident())) return false;
  if (plan.mode == TimelineExecutionMode::preview) {
    return callbacks_.deliver_preview && callbacks_.deliver_preview(result.video, plan);
  }
  return callbacks_.deliver_export && callbacks_.deliver_export(result.video, plan);
}

}  // namespace digitor
