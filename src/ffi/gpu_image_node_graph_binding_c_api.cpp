#include "digitor/gpu_image_session_c_api.h"

#include <mutex>
#include <unordered_map>

namespace {
struct NodeGraphBinding {
    DigitorNodeGraph* graph{};
    uint64_t revision{};
};

std::mutex g_bindings_mutex;
std::unordered_map<DigitorGpuImageSession*, NodeGraphBinding> g_bindings;
}  // namespace

extern "C" {

DigitorResult digitor_gpu_image_session_bind_node_graph(
    DigitorGpuImageSession* session, DigitorNodeGraph* graph,
    uint64_t graph_revision) {
    if (!session || !graph || graph_revision == 0) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const auto revision_result =
        digitor_gpu_image_session_set_graph_revision(session, graph_revision);
    if (revision_result != DIGITOR_RESULT_OK) return revision_result;
    try {
        std::lock_guard lock(g_bindings_mutex);
        g_bindings[session] = NodeGraphBinding{graph, graph_revision};
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
}

DigitorResult digitor_gpu_image_session_clear_node_graph(
    DigitorGpuImageSession* session, uint64_t graph_revision) {
    if (!session || graph_revision == 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto revision_result =
        digitor_gpu_image_session_set_graph_revision(session, graph_revision);
    if (revision_result != DIGITOR_RESULT_OK) return revision_result;
    try {
        std::lock_guard lock(g_bindings_mutex);
        g_bindings.erase(session);
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_gpu_image_session_get_node_graph(
    DigitorGpuImageSession* session, DigitorNodeGraph** out_graph,
    uint64_t* out_graph_revision) {
    if (!session || !out_graph) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_graph = nullptr;
    if (out_graph_revision) *out_graph_revision = 0;
    try {
        std::lock_guard lock(g_bindings_mutex);
        const auto found = g_bindings.find(session);
        if (found == g_bindings.end()) return DIGITOR_RESULT_NOT_INITIALIZED;
        *out_graph = found->second.graph;
        if (out_graph_revision) *out_graph_revision = found->second.revision;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

}  // extern "C"
