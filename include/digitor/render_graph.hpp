#pragma once
#include "digitor/commands.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace digitor {
using GraphResource=uint32_t;
struct ResourceUse {GraphResource resource;ResourceState state;};
struct RenderPass {std::string name;std::vector<ResourceUse> reads,writes;std::function<void(CommandEncoder&)> execute;};
struct TransientResource {uint64_t size{};uint32_t first_pass{},last_pass{},alias_slot{};};
class RenderGraph {public: GraphResource create_transient(uint64_t);uint32_t add_pass(RenderPass);void compile();void execute(CommandQueue&);const std::vector<uint32_t>& order()const{return order_;}const std::vector<PipelineBarrier>& barriers()const{return barriers_;}const TransientResource& transient(GraphResource r)const{return resources_.at(r-1);}private:std::vector<TransientResource>resources_;std::vector<RenderPass>passes_;std::vector<uint32_t>order_;std::vector<PipelineBarrier>barriers_;};
using FrameGraph=RenderGraph;
}
