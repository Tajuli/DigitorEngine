#ifndef DIGITOR_NODE_GRAPH_C_API_EXT_H
#define DIGITOR_NODE_GRAPH_C_API_EXT_H

#include "digitor/digitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorCorrectionControls {
    float exposure;
    float contrast;
    float saturation;
    float temperature;
    float tint;
    float highlights;
    float shadows;
    float hue;
    float color_boost;
} DigitorCorrectionControls;

/* Adds the production Correction operation to the currently selected node.
 * This is additive to the stable digitor.h ABI and uses the same
 * ProductionNodeGraph operation used by preview and export. */
DIGITOR_API DigitorResult digitor_node_graph_add_correction(
    DigitorNodeGraph* graph,
    const DigitorCorrectionControls* controls
);

#ifdef __cplusplus
}
#endif

#endif
