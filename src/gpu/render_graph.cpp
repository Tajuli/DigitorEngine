#include "digitor/render_graph.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace digitor {
namespace {
std::uint64_t append_hash(std::uint64_t hash, std::string_view value) {
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void validate_use(const ResourceUse& use, std::size_t count) {
    if (!use.resource || use.resource > count) throw std::out_of_range("render graph resource");
    if (use.state == ResourceState::undefined) throw std::logic_error("resource use has undefined state");
}
} // namespace

GraphResource RenderGraph::create_resource(GraphResourceDesc desc) {
    if (!desc.size) {
        if (!desc.width || !desc.height) throw std::invalid_argument("zero-sized graph resource");
        desc.size = static_cast<std::uint64_t>(desc.width) * desc.height;
    }
    if (resources_.size() >= std::numeric_limits<GraphResource>::max()) return 0;
    TransientResource lifetime{desc.size, invalid_graph_pass, 0, 0, desc.type, desc.transient};
    resources_.push_back({std::move(desc), lifetime, invalid_graph_pass, false});
    compiled_ = false;
    return static_cast<GraphResource>(resources_.size());
}

GraphResource RenderGraph::import_resource(GraphResourceDesc desc) {
    desc.transient = false;
    if (desc.initial_state == ResourceState::undefined) desc.initial_state = ResourceState::common;
    return create_resource(std::move(desc));
}

GraphResource RenderGraph::create_transient(std::uint64_t size) {
    GraphResourceDesc description;
    description.size = size;
    return create_resource(std::move(description));
}

GraphPass RenderGraph::add_pass(RenderPass pass) {
    if (pass.name.empty() || !pass.execute) throw std::invalid_argument("invalid render pass");
    if (passes_.size() >= invalid_graph_pass) return invalid_graph_pass;
    passes_.push_back(std::move(pass));
    compiled_ = false;
    return static_cast<GraphPass>(passes_.size() - 1);
}

void RenderGraph::add_dependency(GraphPass before, GraphPass after) {
    if (before >= passes_.size() || after >= passes_.size() || before == after)
        throw std::invalid_argument("invalid render pass dependency");
    explicit_edges_.emplace_back(before, after);
    compiled_ = false;
}

void RenderGraph::export_resource(GraphResource resource) {
    if (!resource || resource > resources_.size()) throw std::out_of_range("render graph resource");
    resources_[resource - 1].exported = true;
    compiled_ = false;
}

void RenderGraph::release_resource(GraphResource resource, GraphPass after_pass) {
    if (!resource || resource > resources_.size() || after_pass >= passes_.size())
        throw std::out_of_range("resource release");
    resources_[resource - 1].release_after = after_pass;
    compiled_ = false;
}

void RenderGraph::compile() {
    const auto pass_count = passes_.size();
    order_.clear(); barriers_.clear(); schedule_.clear();
    culled_.assign(pass_count, false);
    for (auto& resource : resources_) {
        resource.lifetime.first_pass = invalid_graph_pass;
        resource.lifetime.last_pass = 0;
        resource.lifetime.alias_slot = 0;
    }

    std::vector<std::vector<GraphPass>> edges(pass_count), reverse(pass_count);
    auto add_edge = [&](GraphPass from, GraphPass to) {
        if (from == to) return;
        auto& list = edges[from];
        if (std::find(list.begin(), list.end(), to) == list.end()) {
            list.push_back(to); reverse[to].push_back(from);
        }
    };
    for (const auto& [from, to] : explicit_edges_) add_edge(from, to);

    std::vector<GraphPass> last_writer(resources_.size(), invalid_graph_pass);
    std::vector<std::vector<GraphPass>> readers(resources_.size());
    for (GraphPass pass = 0; pass < pass_count; ++pass) {
        std::unordered_set<GraphResource> read_set, write_set;
        for (const auto& use : passes_[pass].reads) {
            validate_use(use, resources_.size());
            if (!read_set.insert(use.resource).second) throw std::logic_error("duplicate resource read");
            const auto index = use.resource - 1;
            if (last_writer[index] != invalid_graph_pass) add_edge(last_writer[index], pass);
            else if (resources_[index].desc.transient)
                throw std::logic_error("transient resource read before write");
            readers[index].push_back(pass);
        }
        for (const auto& use : passes_[pass].writes) {
            validate_use(use, resources_.size());
            if (!write_set.insert(use.resource).second || read_set.contains(use.resource))
                throw std::logic_error("duplicate resource write");
            const auto index = use.resource - 1;
            if (last_writer[index] != invalid_graph_pass) add_edge(last_writer[index], pass);
            for (const auto reader : readers[index]) add_edge(reader, pass);
            readers[index].clear();
            last_writer[index] = pass;
        }
    }

    // Retain externally observable passes and recursively retain their producers.
    std::vector<bool> live(pass_count, false);
    std::vector<GraphPass> stack;
    for (GraphPass pass = 0; pass < pass_count; ++pass) if (passes_[pass].side_effect) stack.push_back(pass);
    for (std::size_t resource = 0; resource < resources_.size(); ++resource)
        if (resources_[resource].exported && last_writer[resource] != invalid_graph_pass)
            stack.push_back(last_writer[resource]);
    while (!stack.empty()) {
        const auto pass = stack.back(); stack.pop_back();
        if (live[pass]) continue;
        live[pass] = true;
        stack.insert(stack.end(), reverse[pass].begin(), reverse[pass].end());
    }
    for (GraphPass pass = 0; pass < pass_count; ++pass) culled_[pass] = !live[pass];

    std::vector<std::uint32_t> degree(pass_count);
    for (GraphPass from = 0; from < pass_count; ++from) if (live[from])
        for (const auto to : edges[from]) if (live[to]) ++degree[to];
    std::priority_queue<GraphPass, std::vector<GraphPass>, std::greater<>> ready;
    for (GraphPass pass = 0; pass < pass_count; ++pass) if (live[pass] && degree[pass] == 0) ready.push(pass);
    while (!ready.empty()) {
        const auto from = ready.top(); ready.pop(); order_.push_back(from);
        auto destinations = edges[from];
        std::sort(destinations.begin(), destinations.end());
        for (const auto to : destinations) if (live[to] && --degree[to] == 0) ready.push(to);
    }
    if (order_.size() != static_cast<std::size_t>(std::count(live.begin(), live.end(), true)))
        throw std::logic_error("render graph dependency cycle");

    std::vector<GraphPass> position(pass_count, invalid_graph_pass);
    for (GraphPass index = 0; index < order_.size(); ++index) position[order_[index]] = index;
    for (GraphPass index = 0; index < order_.size(); ++index) {
        const auto pass = order_[index];
        auto track = [&](const ResourceUse& use) {
            auto& record = resources_[use.resource - 1];
            if (record.release_after != invalid_graph_pass && pass > record.release_after)
                throw std::logic_error("resource used after release");
            record.lifetime.first_pass = std::min(record.lifetime.first_pass, index);
            record.lifetime.last_pass = std::max(record.lifetime.last_pass, index);
        };
        for (const auto& use : passes_[pass].reads) track(use);
        for (const auto& use : passes_[pass].writes) track(use);
    }

    // First-fit allocation is deterministic; only compatible transient resources alias.
    for (std::size_t index = 0; index < resources_.size(); ++index) {
        auto& current = resources_[index];
        if (!current.desc.transient || current.lifetime.first_pass == invalid_graph_pass) {
            current.lifetime.alias_slot = static_cast<std::uint32_t>(index); continue;
        }
        std::uint32_t slot = 0;
        for (;; ++slot) {
            bool clash = false;
            for (std::size_t prior = 0; prior < index; ++prior) {
                const auto& candidate = resources_[prior];
                const bool overlap = !(candidate.lifetime.last_pass < current.lifetime.first_pass ||
                                       current.lifetime.last_pass < candidate.lifetime.first_pass);
                if (candidate.desc.transient && candidate.lifetime.alias_slot == slot &&
                    (candidate.desc.type != current.desc.type || candidate.desc.size < current.desc.size || overlap)) {
                    clash = true; break;
                }
            }
            if (!clash) break;
        }
        current.lifetime.alias_slot = slot;
    }

    std::vector<ResourceState> states;
    states.reserve(resources_.size());
    for (const auto& resource : resources_) states.push_back(resource.desc.initial_state);
    for (const auto pass : order_) {
        PipelineBarrier barrier;
        auto transition = [&](const ResourceUse& use) {
            auto& before = states[use.resource - 1];
            if (before != use.state) {
                barrier.transitions.push_back({use.resource, before, use.state});
                before = use.state;
            }
        };
        for (const auto& use : passes_[pass].reads) transition(use);
        for (const auto& use : passes_[pass].writes) transition(use);
        barriers_.push_back(barrier);
        std::vector<GraphPass> dependencies;
        for (const auto dependency : reverse[pass]) if (live[dependency]) dependencies.push_back(dependency);
        std::sort(dependencies.begin(), dependencies.end());
        schedule_.push_back({pass, std::move(dependencies), std::move(barrier), 0});
    }

    hash_ = 1469598103934665603ull;
    for (const auto pass : order_) {
        hash_ = append_hash(hash_, passes_[pass].name);
        for (const auto& use : passes_[pass].reads)
            hash_ = append_hash(hash_, std::to_string(use.resource) + "r" + std::to_string(static_cast<int>(use.state)));
        for (const auto& use : passes_[pass].writes)
            hash_ = append_hash(hash_, std::to_string(use.resource) + "w" + std::to_string(static_cast<int>(use.state)));
    }
    compiled_ = true;
}

void RenderGraph::execute(CommandQueue& queue) {
    if (!compiled_) compile();
    CommandBuffer buffer;
    CommandEncoder encoder(buffer);
    for (std::size_t index = 0; index < order_.size(); ++index) {
        if (!barriers_[index].transitions.empty()) encoder.barrier(barriers_[index]);
        passes_[order_[index]].execute(encoder);
    }
    encoder.finish();
    queue.submit(buffer);
}

const TransientResource& RenderGraph::transient(GraphResource resource) const {
    if (!resource || resource > resources_.size()) throw std::out_of_range("render graph resource");
    return resources_[resource - 1].lifetime;
}

const GraphResourceDesc& RenderGraph::resource_desc(GraphResource resource) const {
    if (!resource || resource > resources_.size()) throw std::out_of_range("render graph resource");
    return resources_[resource - 1].desc;
}

bool RenderGraph::is_culled(GraphPass pass) const {
    if (pass >= passes_.size()) throw std::out_of_range("render graph pass");
    return compiled_ && culled_[pass];
}

std::string RenderGraph::replay_description() const {
    if (!compiled_) throw std::logic_error("render graph is not compiled");
    std::ostringstream output;
    output << "render-graph-v1 " << hash_ << '\n';
    for (std::size_t index = 0; index < schedule_.size(); ++index) {
        output << index << ' ' << passes_[schedule_[index].pass].name << " deps";
        for (const auto dependency : schedule_[index].dependencies) output << ' ' << dependency;
        output << " barriers " << schedule_[index].barrier.transitions.size() << '\n';
    }
    return output.str();
}
} // namespace digitor
