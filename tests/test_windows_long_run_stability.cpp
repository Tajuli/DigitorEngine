#if !defined(_WIN32)
#error This qualification is Windows-only
#endif

#define main digitor_preview_export_parity_once
#include "test_real_media_preview_export_parity.cpp"
#undef main

#include "digitor/media.hpp"
#include "gpu/execution_provenance.hpp"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::uint64_t working_set_bytes() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                            sizeof(counters))) return 0;
  return static_cast<std::uint64_t>(counters.WorkingSetSize);
}

int fail_stability(const std::string& message) {
  std::cerr << "WINDOWS_LONG_RUN_STABILITY=FAIL diagnostic=\"" << message << "\"\n";
  return 1;
}
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return fail_stability("expected fixture path and optional iterations");
  const int iterations = argc == 3 ? std::max(4, std::atoi(argv[2])) : 120;

  digitor::DecoderOptions options;
  options.hardware = digitor::HardwareDecode::cpu;
  options.allow_cpu_fallback = true;
  options.cache_capacity = 16;
  auto decoder = digitor::open_video_decoder(argv[1], options);
  if (!decoder) return fail_stability("decoder creation failed");

  const auto memory_start = working_set_bytes();
  std::uint64_t memory_peak = memory_start;
  std::uint64_t decoded_frames = 0;
  std::uint64_t seek_cycles = 0;
  std::uint64_t successful_cycles = 0;

  char* parity_argv[] = {argv[0], argv[1], nullptr};
  for (int i = 0; i < iterations; ++i) {
    if ((i % 10) == 0) {
      decoder->seek(static_cast<std::int64_t>((i % 4) * 500000));
      ++seek_cycles;
    }
    const auto frame = decoder->decode(static_cast<digitor::FrameNumber>(i % 90));
    if (!frame || frame->pixels.empty()) return fail_stability("decode/seek cycle failed");
    ++decoded_frames;

    if (i == iterations / 2) {
      digitor::set_gpu_failure_point(digitor::GpuFailurePoint::DeviceLost);
      const int injected = digitor_preview_export_parity_once(2, parity_argv);
      digitor::set_gpu_failure_point(digitor::GpuFailurePoint::None);
      if (injected == 0) return fail_stability("simulated device-loss did not fail closed");
      const int recovered = digitor_preview_export_parity_once(2, parity_argv);
      if (recovered != 0) return fail_stability("backend recreation after simulated device-loss failed");
      ++successful_cycles;
    } else {
      if (digitor_preview_export_parity_once(2, parity_argv) != 0)
        return fail_stability("preview/export stress cycle failed");
      ++successful_cycles;
    }
    memory_peak = std::max(memory_peak, working_set_bytes());
  }

  const auto memory_end = working_set_bytes();
  const auto memory_growth = memory_end > memory_start ? memory_end - memory_start : 0;
  constexpr std::uint64_t max_growth = 256ull * 1024ull * 1024ull;
  const bool bounded = memory_growth <= max_growth;

  std::cout << "LONG_RUN_METRICS iterations=" << iterations
            << " decoded_frames=" << decoded_frames
            << " seek_cycles=" << seek_cycles
            << " successful_cycles=" << successful_cycles
            << " memory_start_bytes=" << memory_start
            << " memory_peak_bytes=" << memory_peak
            << " memory_end_bytes=" << memory_end
            << " memory_growth_bytes=" << memory_growth
            << " memory_growth_limit_bytes=" << max_growth << '\n';
  std::cout << "SIMULATED_DEVICE_LOSS_FAIL_CLOSED=PASS\n";
  std::cout << "BACKEND_RECREATION_RECOVERY=PASS\n";
  std::cout << "REPEATED_SEEK_DECODE=PASS\n";
  std::cout << "MEMORY_GROWTH_BOUNDED=" << (bounded ? "PASS" : "FAIL") << '\n';
  if (!bounded) return fail_stability("working-set growth exceeded qualification limit");
  std::cout << "WINDOWS_LONG_RUN_STABILITY=PASS\n";
  return 0;
}
