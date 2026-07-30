#include "digitor/correction.hpp"
#include "digitor/production_node_graph.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

void test_correction() {
  using namespace digitor;

  const auto identity = CorrectionParameters::create();
  assert(identity->is_identity());
  const Color source{.2f, .4f, .6f, .75f};
  const auto unchanged = apply_correction_reference(source, *identity);
  assert(unchanged.r == source.r && unchanged.g == source.g &&
         unchanged.b == source.b && unchanged.a == source.a);

  CorrectionSettings settings;
  settings.exposure = .5f;
  settings.contrast = .25f;
  settings.saturation = .2f;
  settings.temperature = .1f;
  settings.tint = -.1f;
  settings.highlights = .3f;
  settings.shadows = -.2f;
  settings.hue = .25f;
  settings.color_boost = .4f;
  const auto correction = CorrectionParameters::create(settings);
  assert(!correction->is_identity());
  assert(correction->identity().find("correction:") == 0);

  std::vector<Color> input(4, source), expected(4);
  apply_correction_reference(input, expected, *correction);
  assert(expected[0].a == source.a);
  assert(std::abs(expected[0].r - source.r) > 1.0e-5f ||
         std::abs(expected[0].g - source.g) > 1.0e-5f ||
         std::abs(expected[0].b - source.b) > 1.0e-5f);

  ProductionNodeGraph graph;
  const auto node = graph.add_serial_after(graph.input_node(), "Correction");
  graph.select_node(node);
  graph.add_operation_to_selected(make_correction_operation(correction));
  const auto output = graph.render(input, 2, 2);
  assert(output.size() == expected.size());
  for (std::size_t i = 0; i < output.size(); ++i) {
    assert(std::abs(output[i].r - expected[i].r) < 1.0e-7f);
    assert(std::abs(output[i].g - expected[i].g) < 1.0e-7f);
    assert(std::abs(output[i].b - expected[i].b) < 1.0e-7f);
    assert(output[i].a == expected[i].a);
  }
  assert(graph.node(node).operations.size() == 1);
  assert(graph.node(node).operations.front().kind == NodeOperationKind::correction);

  bool rejected = false;
  auto invalid = settings;
  invalid.exposure = std::numeric_limits<float>::quiet_NaN();
  try {
    (void)CorrectionParameters::create(invalid);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
}
