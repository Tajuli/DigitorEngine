#include "digitor/rgb_curves.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
int main(){using namespace digitor;using clock=std::chrono::steady_clock;for(auto pixels:{std::size_t(1920)*1080,std::size_t(3840)*2160})for(auto size:{256u,1024u,4096u}){RgbCurvesParameters p;p.lut_size=size;p.master.points={{0,0},{.1f,.03f},{.3f,.22f},{.55f,.68f},{.8f,.92f},{1,1}};CompiledRgbCurves::clear_cache();auto c0=clock::now();auto curves=CompiledRgbCurves::compile(p);auto c1=clock::now();auto warm=CompiledRgbCurves::compile(p);auto c2=clock::now();std::vector<Color>in(pixels),out(pixels);std::mt19937 rng(470);std::uniform_real_distribution<float>d(-.25f,1.5f);for(auto&v:in)v={d(rng),d(rng),d(rng),d(rng)};auto e0=clock::now();curves->apply(in,out);auto e1=clock::now();std::cout<<pixels<<" pixels, LUT "<<size<<": cold coefficient+LUT "<<std::chrono::duration<double,std::milli>(c1-c0).count()<<" ms, warm lookup "<<std::chrono::duration<double,std::micro>(c2-c1).count()<<" us, CPU reference "<<std::chrono::duration<double,std::milli>(e1-e0).count()<<" ms, cache_hit="<<(curves==warm)<<'\n';}}
