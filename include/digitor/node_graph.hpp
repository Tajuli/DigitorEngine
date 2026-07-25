#pragma once
#include "digitor/color.hpp"
#include <cstdint>
#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>
namespace digitor {
using NodeId=std::uint64_t;
using NodeValue=std::vector<Color>;
struct NodeContext { std::int64_t frame{}; std::uint32_t width{},height{}; std::unordered_map<std::string,double> values; };
using NodeProcessor=std::function<NodeValue(const NodeContext&,const std::vector<NodeValue>&)>;
struct Node {NodeId id{};std::string name;std::string shader;std::vector<NodeId> inputs;NodeProcessor process;};
class NodeGraph {
public:
 NodeId add_node(std::string name,NodeProcessor processor={},std::string shader={});
 void connect(NodeId source,NodeId destination); void remove_node(NodeId); void clear_cache();
 NodeValue evaluate(NodeId output,const NodeContext&); std::vector<NodeId> execution_order(NodeId output)const;
 void serialize(std::ostream&)const; static NodeGraph deserialize(std::istream&,const std::function<NodeProcessor(const std::string&)>& resolver={});
 std::size_t size()const noexcept{return nodes_.size();} std::size_t cache_size()const noexcept{return cache_.size();}
 const Node& node(NodeId id)const;
private:
 struct Key {NodeId node;std::int64_t frame;std::uint32_t width,height;bool operator==(const Key&)const=default;};
 struct Hash {std::size_t operator()(const Key&)const noexcept;};
 std::unordered_map<NodeId,Node>nodes_;std::unordered_map<Key,NodeValue,Hash>cache_;NodeId next_{1};
};
using ShaderGraph=NodeGraph;
}
