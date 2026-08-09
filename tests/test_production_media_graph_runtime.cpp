#include "digitor/production_media_graph_runtime.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace digitor;

int main() {
  ProductionNodeGraph graph;

  ProductionMediaGraphRuntime runtime(
      nullptr,
      graph,
      [](const ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_OK;
      });

  assert(runtime.graph_identity() == graph.recipe_identity());
  auto telemetry = runtime.telemetry();
  assert(telemetry.graph_identity == graph.recipe_identity());
  assert(telemetry.preview_frames == 0);
  assert(telemetry.export_frames == 0);
  assert(telemetry.cpu_readbacks == 0);
  assert(!telemetry.export_running);
  assert(!telemetry.cancelled);

  HardwareEncodeConfig software;
  software.backend = EncoderBackend::software;
  std::string diagnostic;
  const std::vector<FrameNumber> frames{0};
  assert(runtime.export_frames(frames, software, &diagnostic) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  // Encoder callbacks are deliberately absent, so the runtime must reject the
  // export before any decode, graph execution, readback or software encode.
  assert(!diagnostic.empty());
  assert(runtime.telemetry().cpu_readbacks == 0);

  runtime.cancel();
  telemetry = runtime.telemetry();
  assert(telemetry.cancelled);
  assert(!telemetry.export_running);
  assert(telemetry.cpu_readbacks == 0);

  ProcessedGpuFramePtr output;
  diagnostic.clear();
  assert(runtime.preview(0, &output, &diagnostic) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(!output);
  assert(!diagnostic.empty());

  return 0;
}
