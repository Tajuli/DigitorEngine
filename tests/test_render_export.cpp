#include "digitor/renderer.hpp"
#include "digitor/digitor.h"
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <mutex>
void test_render_export(){
 digitor::VideoFrame a;a.width=2;a.height=1;a.pixels={{0,0,0,1},{1,1,1,1}};auto b=a;auto v=digitor::validate_pixels(a,b);assert(v.passed&&v.differing_pixels==0&&std::isinf(v.psnr)&&v.ssim==1);
 b.pixels[0].r=.1f;v=digitor::validate_pixels(a,b,20,.9);assert(v.differing_pixels==1&&v.psnr>20&&v.ssim>.9);
 int builds=0;digitor::SharedRenderer renderer([&](auto&g,const auto&r,auto&out){++builds;auto id=g.create_transient(4);g.add_pass({"one-graph",{},{{id,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.assign(size_t(r.width)*r.height,{.25f,.5f,.75f,1});});}});});assert(digitor::validate_preview_export(renderer,{0,2,2,{}}).passed);assert(builds==1);
 DigitorSdkSession*s=nullptr;assert(digitor_sdk_create(&s)==DIGITOR_RESULT_OK);std::mutex m;std::condition_variable cv;bool done=false;auto callback=[](DigitorResult r,void*u){assert(r==DIGITOR_RESULT_OK);auto*p=static_cast<std::pair<std::condition_variable*,bool*>*>(u);*p->second=true;p->first->notify_one();};std::pair<std::condition_variable*,bool*> state{&cv,&done};assert(digitor_sdk_preview_async(s,1,4,4,callback,&state)==DIGITOR_RESULT_OK);{std::unique_lock lock(m);cv.wait(lock,[&]{return done;});}DigitorNativeTexture texture{};assert(digitor_sdk_get_native_texture(s,&texture)==DIGITOR_RESULT_OK&&texture.width==4&&texture.pixels);assert(digitor_sdk_destroy(s)==DIGITOR_RESULT_OK);
}
