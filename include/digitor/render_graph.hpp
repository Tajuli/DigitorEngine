#pragma once

#include "digitor/commands.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace digitor {

using GraphResource = std::uint32_t;
using GraphPass = std::uint32_t;
constexpr GraphPass invalid_graph_pass = std::numeric_limits<GraphPass>::max();

enum class GraphResourceType {
    texture,
    buffer,
    uniform_buffer,
    storage_buffer,
    render_target,
    readback
};

struct GraphResourceDesc {
    GraphResourceType type{GraphResourceType::buffer};
    std::uint64_t size{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t format{};
    bool transient{true};
    ResourceState initial_state{ResourceState::undefined};
    std::string name;
};

struct ResourceUse {
    GraphResource resource{};
    ResourceState state{ResourceState::undefined};
};

struct RenderPass {
    std::string name;
    std::vector<ResourceUse> reads;
    std::vector<ResourceUse> writes;
    std::function<void(CommandEncoder&)> execute;
    // Conservative by default for compatibility: explicitly pure passes can be culled.
    bool side_effect{true};
};

struct TransientResource {
    std::uint64_t size{};
    GraphPass first_pass{invalid_graph_pass};
    GraphPass last_pass{};
    std::uint32_t alias_slot{};
    GraphResourceType type{GraphResourceType::buffer};
    bool transient{true};
};

struct ScheduledPass {
    GraphPass pass{};
    std::vector<GraphPass> dependencies;
    PipelineBarrier barrier;
    // Reserved queue classification makes the schedule async-ready while execution stays serial.
    std::uint32_t queue_index{};
};

class RenderGraph {
public:
    GraphResource create_resource(GraphResourceDesc);
    GraphResource import_resource(GraphResourceDesc);
    GraphResource create_transient(std::uint64_t);
    GraphPass add_pass(RenderPass);
    void add_dependency(GraphPass before, GraphPass after);
    void export_resource(GraphResource);
    void release_resource(GraphResource, GraphPass after_pass);

    void compile();
    void execute(CommandQueue&);

    [[nodiscard]] const std::vector<GraphPass>& order() const noexcept { return order_; }
    [[nodiscard]] const std::vector<PipelineBarrier>& barriers() const noexcept { return barriers_; }
    [[nodiscard]] const std::vector<ScheduledPass>& schedule() const noexcept { return schedule_; }
    [[nodiscard]] const TransientResource& transient(GraphResource resource) const;
    [[nodiscard]] const GraphResourceDesc& resource_desc(GraphResource resource) const;
    [[nodiscard]] bool is_culled(GraphPass pass) const;
    [[nodiscard]] std::uint64_t hash() const noexcept { return hash_; }
    [[nodiscard]] std::string replay_description() const;

private:
    struct ResourceRecord {
        GraphResourceDesc desc;
        TransientResource lifetime;
        GraphPass release_after{invalid_graph_pass};
        bool exported{};
    };
    std::vector<ResourceRecord> resources_;
    std::vector<RenderPass> passes_;
    std::vector<std::pair<GraphPass, GraphPass>> explicit_edges_;
    std::vector<GraphPass> order_;
    std::vector<PipelineBarrier> barriers_;
    std::vector<ScheduledPass> schedule_;
    std::vector<bool> culled_;
    std::uint64_t hash_{};
    bool compiled_{};
};

using FrameGraph = RenderGraph;
} // namespace digitor
