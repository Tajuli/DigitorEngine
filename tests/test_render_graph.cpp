#include "digitor/render_graph.hpp"
#include "digitor/shader.hpp"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<class Function> void expect_logic_error(Function function) {
    bool threw = false;
    try { function(); } catch (const std::logic_error&) { threw = true; }
    assert(threw);
}

digitor::RenderPass pass(std::string name, std::vector<digitor::ResourceUse> reads,
                         std::vector<digitor::ResourceUse> writes, std::vector<int>* replay = nullptr,
                         int marker = 0, bool side_effect = false) {
    return {std::move(name), std::move(reads), std::move(writes),
            [replay, marker](digitor::CommandEncoder& encoder) {
                encoder.dispatch([replay, marker] { if (replay) replay->push_back(marker); });
            }, side_effect};
}
}

void test_render_graph() {
    using namespace digitor;
    std::vector<int> replay;
    RenderGraph graph;
    const auto input = graph.import_resource({GraphResourceType::texture, 256, 8, 8, 1, false,
                                               ResourceState::shader_read, "input"});
    const auto temporary = graph.create_resource({GraphResourceType::storage_buffer, 256, 0, 0, 0,
                                                   true, ResourceState::undefined, "temporary"});
    const auto output = graph.create_resource({GraphResourceType::readback, 256, 0, 0, 0,
                                                true, ResourceState::undefined, "output"});
    const auto unused = graph.create_transient(64);
    graph.add_pass(pass("prepare", {{input, ResourceState::shader_read}},
                        {{temporary, ResourceState::shader_write}}, &replay, 1));
    graph.add_pass(pass("consume", {{temporary, ResourceState::shader_read}},
                        {{output, ResourceState::copy_destination}}, &replay, 2));
    const auto dead = graph.add_pass(pass("culled", {}, {{unused, ResourceState::shader_write}},
                                          &replay, 9));
    graph.export_resource(output);
    graph.compile();
    assert((graph.order() == std::vector<GraphPass>{0, 1}));
    assert(graph.is_culled(dead));
    assert(graph.schedule().size() == 2 && graph.schedule()[1].dependencies[0] == 0);
    assert(graph.barriers().size() == 2 && !graph.barriers()[0].transitions.empty());
    const auto hash = graph.hash();
    const auto description = graph.replay_description();
    graph.compile();
    assert(graph.hash() == hash && graph.replay_description() == description);
    CommandQueue queue;
    graph.execute(queue);
    assert((replay == std::vector<int>{1, 2}));

    // Compatible, non-overlapping allocations reuse the same physical slot.
    RenderGraph reuse;
    const auto first = reuse.create_transient(128);
    const auto second = reuse.create_transient(64);
    reuse.add_pass(pass("first", {}, {{first, ResourceState::shader_write}}, nullptr, 0, true));
    reuse.add_pass(pass("second", {}, {{second, ResourceState::shader_write}}, nullptr, 0, true));
    reuse.add_dependency(0, 1);
    reuse.compile();
    assert(reuse.transient(first).alias_slot == reuse.transient(second).alias_slot);

    // The descriptor math remains valid at several practical resolutions.
    for (const auto [width, height] : std::vector<std::pair<std::uint32_t, std::uint32_t>>{
             {1, 1}, {1920, 1080}, {3840, 2160}}) {
        RenderGraph sized;
        const auto texture = sized.create_resource({GraphResourceType::render_target,
            static_cast<std::uint64_t>(width) * height * 4, width, height});
        sized.add_pass(pass("render", {}, {{texture, ResourceState::shader_write}}, nullptr, 0, true));
        sized.compile();
        assert(sized.resource_desc(texture).width == width && sized.transient(texture).size >= width);
    }

    RenderGraph cycle;
    const auto a = cycle.create_transient(4), b = cycle.create_transient(4);
    cycle.add_pass(pass("a", {}, {{a, ResourceState::shader_write}}, nullptr, 0, true));
    cycle.add_pass(pass("b", {}, {{b, ResourceState::shader_write}}, nullptr, 0, true));
    cycle.add_dependency(0, 1); cycle.add_dependency(1, 0);
    expect_logic_error([&] { cycle.compile(); });

    RenderGraph duplicate;
    const auto duplicate_resource = duplicate.create_transient(4);
    duplicate.add_pass(pass("duplicate", {}, {{duplicate_resource, ResourceState::shader_write},
        {duplicate_resource, ResourceState::shader_write}}, nullptr, 0, true));
    expect_logic_error([&] { duplicate.compile(); });

    RenderGraph before_write;
    const auto unwritten = before_write.create_transient(4);
    before_write.add_pass(pass("bad-read", {{unwritten, ResourceState::shader_read}}, {}, nullptr, 0, true));
    expect_logic_error([&] { before_write.compile(); });

    RenderGraph released;
    const auto released_resource = released.create_transient(4);
    released.add_pass(pass("write", {}, {{released_resource, ResourceState::shader_write}}, nullptr, 0, true));
    released.add_pass(pass("late-read", {{released_resource, ResourceState::shader_read}}, {}, nullptr, 0, true));
    released.release_resource(released_resource, 0);
    expect_logic_error([&] { released.compile(); });

    // Stress a long dependency chain and prove stable ordering/hash.
    RenderGraph stress;
    GraphResource previous = 0;
    for (int index = 0; index < 512; ++index) {
        const auto next = stress.create_transient(16);
        std::vector<ResourceUse> reads;
        if (previous) reads.push_back({previous, ResourceState::shader_read});
        stress.add_pass(pass("p" + std::to_string(index), std::move(reads),
                             {{next, ResourceState::shader_write}}, nullptr, 0, index == 511));
        previous = next;
    }
    stress.compile();
    assert(stress.order().size() == 512);
    const auto stress_hash = stress.hash(); stress.compile(); assert(stress.hash() == stress_hash);

    CpuKernelRegistry kernels;
    int kernel_calls = 0;
    kernels.register_kernel("copy", [&](auto, auto) { ++kernel_calls; });
    std::array<std::byte, 1> bytes{};
    assert(kernels.execute("copy", bytes, bytes) && kernel_calls == 1);
    assert(!kernels.execute("unregistered", bytes, bytes));
}
