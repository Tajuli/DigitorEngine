#include "digitor/effects.hpp"
#include "digitor/lut.hpp"
#include "digitor/node_graph.hpp"
#include <cassert>
#include <cmath>
#include <sstream>
void test_v2(){using namespace digitor;
 NodeGraph g;int calls=0;auto a=g.add_node("source",[&](const NodeContext&,const std::vector<NodeValue>&){++calls;return NodeValue{{.25f,.5f,.75f,1}};},"source shader");auto b=g.add_node("copy",[](const NodeContext&,const std::vector<NodeValue>&v){return v.at(0);});g.connect(a,b);assert(g.evaluate(b,{7,1,1,{}})[0].g==.5f);g.evaluate(b,{7,1,1,{}});assert(calls==1);std::stringstream serialized;g.serialize(serialized);auto restored=NodeGraph::deserialize(serialized,[](const std::string&){return [](const NodeContext&,const std::vector<NodeValue>&v){return v.empty()?NodeValue{{1,0,0,1}}:v[0];};});assert(restored.size()==2&&restored.evaluate(b,{})[0].r==1);
 Lut1D one({{0,0,0,1},{1,1,1,1}});Color input{.25f,.5f,.75f,1},cpu{},gpu{};apply_lut_cpu(&input,&cpu,1,one);CommandBuffer cb;CommandEncoder enc(cb);apply_lut_gpu(enc,&input,&gpu,1,one);enc.finish();CommandQueue q;q.submit(cb);assert(std::abs(cpu.b-gpu.b)<1e-7f);
 std::stringstream cube("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");auto three=Lut3D::load_cube(cube);auto c=three.sample(input,LutInterpolation::tetrahedral);assert(std::abs(c.r-input.r)<1e-6f&&std::abs(c.b-input.b)<1e-6f);
 Color image[3]{{1,0,0,1},{0,1,0,1},{0,0,1,1}},effect[3];CommandBuffer eb;CommandEncoder ee(eb);apply_effect_gpu(ee,image,effect,3,1,{EffectType::chromatic_aberration,1,1,0,42});ee.finish();q.submit(eb);assert(effect[1].r==1&&effect[1].g==1&&effect[1].b==1);
}
