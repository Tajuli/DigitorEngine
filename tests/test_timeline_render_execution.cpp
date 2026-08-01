#include "digitor/timeline_render_execution.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  using namespace digitor;
  try {
    TimelineProjectModel project;
    project.fps = 30;
    TimelineTrackModel base{"v1", "Base", TimelineTrackType::video};
    base.clips.push_back({"base", TimelineClipType::video, 0, 5'000'000, 1'000'000});
    TimelineTrackModel overlay{"v2", "Overlay", TimelineTrackType::video};
    overlay.clips.push_back({"overlay", TimelineClipType::overlay, 1'000'000, 2'000'000, 0});
    TimelineTrackModel audio{"a1", "Audio", TimelineTrackType::audio};
    TimelineClipModel music{"music", TimelineClipType::audio, 0, 5'000'000, 500'000};
    music.volume = 0.8;
    audio.clips.push_back(music);
    project.tracks = {base, overlay, audio};

    ClipExecutionOverrides overlay_fx;
    overlay_fx.opacity = 0.75;
    overlay_fx.grade_revision = 9;
    overlay_fx.transition_in = {TransitionType::cross_dissolve, 1'000'000, 500'000};
    overlay_fx.opacity_curve.set_keyframe({1'000'000, 0.25, KeyframeInterpolation::linear});
    overlay_fx.opacity_curve.set_keyframe({2'000'000, 0.75, KeyframeInterpolation::linear});

    ClipExecutionOverrides music_fx;
    music_fx.volume = 0.5;
    music_fx.pan = -0.25;

    TimelineRenderExecutor executor(project, {{"overlay", overlay_fx}, {"music", music_fx}});
    const auto preview = executor.build_plan(TimelineExecutionMode::preview, 1'250'000,
                                             1920, 1080, 12, 34);
    const auto export_plan = executor.build_plan(TimelineExecutionMode::export_render,
                                                 1'250'000, 1920, 1080, 12, 34);

    require(preview.video_layers.size() == 2, "two video layers expected");
    require(preview.audio_layers.size() == 1, "one audio layer expected");
    require(preview.video_layers[0].clip_id == "base", "base layer order");
    require(preview.video_layers[1].clip_id == "overlay", "overlay layer order");
    require(preview.video_layers[0].source_time_us == 2'250'000, "base source mapping");
    require(preview.video_layers[1].source_time_us == 250'000, "overlay source mapping");
    require(preview.video_layers[1].transition_weight > 0.0 &&
            preview.video_layers[1].transition_weight < 1.0,
            "transition weight expected");
    require(preview.video_layers[1].cache_key.grade_revision == 9,
            "grade revision in cache key");
    require(std::abs(preview.audio_layers[0].gain - 0.4) < 0.0001,
            "clip and override gains multiply");
    require(std::abs(preview.audio_layers[0].pan + 0.25) < 0.0001,
            "audio pan expected");
    require(preview.identity == export_plan.identity,
            "preview and export identities must match");
    require(executor.preview_export_equivalent(1'250'000, 1920, 1080, 12, 34),
            "preview/export equivalence API");

    const auto later = executor.build_plan(TimelineExecutionMode::preview, 4'000'000,
                                           1920, 1080, 12, 34);
    require(later.video_layers.size() == 1, "overlay must expire");
    require(later.video_layers.front().clip_id == "base", "base remains active");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "timeline render execution test failed: " << error.what() << '\n';
    return 1;
  }
}
