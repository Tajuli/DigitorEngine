#include "digitor/node_graph_c_api_ext.h"

#include "digitor/correction.hpp"
#include "digitor/production_node_graph.hpp"

#include <cstdint>
#include <new>

/* DigitorNodeGraph is an opaque C handle whose concrete layout is owned by the
 * core C API. This definition is intentionally identical to digitor_c_api.cpp.
 * Every operation first validates the handle through the stable public API so
 * stale/foreign handles are rejected before the implementation is touched. */
struct DigitorNodeGraph { digitor::ProductionNodeGraph impl; };

extern "C" DigitorResult digitor_node_graph_add_correction(
    DigitorNodeGraph* graph,
    const DigitorCorrectionControls* controls) {
    if (!graph || !controls) return DIGITOR_RESULT_INVALID_ARGUMENT;

    std::uint64_t required = 0;
    const auto validation =
        digitor_node_graph_recipe_identity(graph, nullptr, 0, &required);
    if (validation != DIGITOR_RESULT_OK) return validation;

    try {
        digitor::CorrectionSettings settings{};
        settings.exposure = controls->exposure;
        settings.contrast = controls->contrast;
        settings.saturation = controls->saturation;
        settings.temperature = controls->temperature;
        settings.tint = controls->tint;
        settings.highlights = controls->highlights;
        settings.shadows = controls->shadows;
        settings.hue = controls->hue;
        settings.color_boost = controls->color_boost;
        graph->impl.add_operation_to_selected(
            digitor::make_correction_operation(
                digitor::CorrectionParameters::create(settings)));
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}
