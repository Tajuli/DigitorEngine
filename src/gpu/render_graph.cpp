#include "digitor/render_graph.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace digitor {
namespace {
constexpr auto invalid_pass = std::numeric_limits<std::uint32_t>::max();

std::uint32_t checked_index(std::size_t value, const char* collection) {
    if (value > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error(std::string(collection) + " exceeds 32-bit graph limits");
    return static_cast<std::uint32_t>(value);
}
} // namespace

GraphResource RenderGraph::create_transient(uint64_t size) {
    if (!size) throw std::invalid_argument("zero-sized transient");
    if (resources_.size() >= std::numeric_limits<GraphResource>::max()) return 0;
    resources_.push_back({size, invalid_pass, 0, 0});
    return static_cast<GraphResource>(resources_.size());
}

uint32_t RenderGraph::add_pass(RenderPass pass) {
    if (pass.name.empty() || !pass.execute) throw std::invalid_argument("invalid pass");
    if (passes_.size() >= invalid_pass) return invalid_pass;
    const auto index = static_cast<std::uint32_t>(passes_.size());
    passes_.push_back(std::move(pass));
    return index;
}

void RenderGraph::compile() {
    order_.clear();
    barriers_.clear();
    const auto pass_count = checked_index(passes_.size(), "pass count");
    const auto resource_count = checked_index(resources_.size(), "resource count");
    std::vector<std::vector<uint32_t>> edges(passes_.size());
    std::vector<uint32_t> degree(passes_.size());
    std::unordered_map<GraphResource, uint32_t> writer;
    for (uint32_t i = 0; i < pass_count; ++i) {
        for (auto use : passes_[i].reads) {
            if (!use.resource || use.resource > resource_count) throw std::out_of_range("resource");
            if (writer.contains(use.resource)) { edges[writer[use.resource]].push_back(i); ++degree[i]; }
        }
        for (auto use : passes_[i].writes) {
            if (!use.resource || use.resource > resource_count) throw std::out_of_range("resource");
            if (writer.contains(use.resource)) { edges[writer[use.resource]].push_back(i); ++degree[i]; }
            writer[use.resource] = i;
        }
        for (auto use : passes_[i].reads) {
            auto& resource = resources_[use.resource - 1];
            resource.first_pass = std::min(resource.first_pass, i);
            resource.last_pass = i;
        }
        for (auto use : passes_[i].writes) {
            auto& resource = resources_[use.resource - 1];
            resource.first_pass = std::min(resource.first_pass, i);
            resource.last_pass = i;
        }
    }
    for (uint32_t i = 0; i < pass_count; ++i) if (!degree[i]) order_.push_back(i);
    for (std::size_t q = 0; q < order_.size(); ++q)
        for (auto destination : edges[order_[q]])
            if (--degree[destination] == 0) order_.push_back(destination);
    if (order_.size() != passes_.size()) throw std::logic_error("render graph cycle");

    for (uint32_t i = 0; i < resource_count; ++i) {
        uint32_t slot = 0;
        for (;; ++slot) {
            bool clash = false;
            for (uint32_t j = 0; j < i; ++j)
                if (resources_[j].alias_slot == slot &&
                    !(resources_[j].last_pass < resources_[i].first_pass ||
                      resources_[i].last_pass < resources_[j].first_pass)) {
                    clash = true;
                    break;
                }
            if (!clash) break;
        }
        resources_[i].alias_slot = slot;
    }
    std::unordered_map<GraphResource, ResourceState> states;
    for (auto pass_index : order_) {
        PipelineBarrier barrier;
        auto uses = passes_[pass_index].reads;
        uses.insert(uses.end(), passes_[pass_index].writes.begin(), passes_[pass_index].writes.end());
        for (auto use : uses) {
            const auto before = states.contains(use.resource) ? states[use.resource] : ResourceState::undefined;
            if (before != use.state) {
                barrier.transitions.push_back({use.resource, before, use.state});
                states[use.resource] = use.state;
            }
        }
        barriers_.push_back(std::move(barrier));
    }
}

void RenderGraph::execute(CommandQueue& queue) {
    if (order_.size() != passes_.size()) compile();
    CommandBuffer buffer;
    CommandEncoder encoder(buffer);
    for (std::size_t i = 0; i < order_.size(); ++i) {
        if (!barriers_[i].transitions.empty()) encoder.barrier(barriers_[i]);
        passes_[order_[i]].execute(encoder);
    }
    encoder.finish();
    queue.submit(buffer);
}
} // namespace digitor
