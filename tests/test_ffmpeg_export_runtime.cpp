#include "digitor/ffmpeg_export_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  const auto root = std::filesystem::temp_directory_path() / "digitor_ffmpeg_runtime_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  const auto input = root / "input.mp4";
  std::ofstream(input, std::ios::binary) << "source";

  std::vector<std::string> captured;
  digitor::FfmpegExportRuntime runtime({}, [&](const std::vector<std::string>& args) {
    captured = args;
    std::ofstream(args.back(), std::ios::binary) << "proxy-bytes";
    return 0;
  });

  digitor::TranscodeRequest request;
  request.input_path = input;
  request.output_path = root / "output.mp4";
  request.duration_us = 2'000'000;
  request.profile.width = 1280;
  request.profile.height = 720;
  request.profile.video_bitrate = 3'000'000;

  digitor::ExportCheckpoint checkpoint;
  checkpoint.project_id = "project";
  checkpoint.output_path = request.output_path.string();
  checkpoint.duration_us = request.duration_us;
  digitor::ResumableExportSession session(checkpoint, root / "checkpoint.txt");

  const auto result = runtime.transcode(request, digitor::EncoderBackend::software, &session);
  assert(result.success);
  assert(std::filesystem::exists(request.output_path));
  assert(session.snapshot().state == digitor::ExportState::completed);
  assert(!captured.empty());
  assert(digitor::ffmpeg_video_encoder(digitor::EncoderBackend::software,
                                       digitor::ExportCodec::h264) == "libx264");
  assert(digitor::ffmpeg_video_encoder(digitor::EncoderBackend::nvenc,
                                       digitor::ExportCodec::hevc) == "hevc_nvenc");

  digitor::PersistentArtifactCache cache(root / "cache", 1024 * 1024);
  digitor::ProxyRequest proxy;
  proxy.clip_id = "clip";
  proxy.source_path = input.string();
  proxy.proxy_path = (root / "proxy.mp4").string();
  auto first = runtime.generate_proxy(proxy, 7, &cache);
  assert(first.success);
  std::filesystem::remove(proxy.proxy_path, ec);
  auto second = runtime.generate_proxy(proxy, 7, &cache);
  assert(second.success);
  assert(second.diagnostic == "proxy cache hit");

  runtime.cancel();
  const auto cancelled = runtime.transcode(request, digitor::EncoderBackend::software);
  assert(cancelled.cancelled);

  std::filesystem::remove_all(root, ec);
  return 0;
}
