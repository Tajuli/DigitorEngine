#include "digitor/renderer.hpp"
#include "digitor/digitor.h"
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
void test_render_export(){
 digitor::VideoFrame a;a.width=2;a.height=1;a.pixels={{0,0,0,1},{1,1,1,1}};auto b=a;auto v=digitor::validate_pixels(a,b);assert(v.passed&&v.differing_pixels==0&&std::isinf(v.psnr)&&v.ssim==1);
 b.pixels[0].r=.1f;v=digitor::validate_pixels(a,b,20,.9);assert(v.differing_pixels==1&&v.psnr>20&&v.ssim>.9);
 int builds=0;digitor::SharedRenderer renderer([&](auto&g,const auto&r,auto&out){++builds;auto id=g.create_transient(4);g.add_pass({"one-graph",{},{{id,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.assign(size_t(r.width)*r.height,{.25f,.5f,.75f,1});});}});});assert(digitor::validate_preview_export(renderer,{0,2,2,{}}).passed);assert(builds==1);
 DigitorSdkSession*s=nullptr;assert(digitor_sdk_create(&s)==DIGITOR_RESULT_OK);std::mutex m;std::condition_variable cv;std::atomic_bool done=false;auto callback=[](DigitorResult r,void*u){assert(r==DIGITOR_RESULT_OK);auto*p=static_cast<std::pair<std::condition_variable*,std::atomic_bool*>*>(u);p->second->store(true);p->first->notify_one();};std::pair<std::condition_variable*,std::atomic_bool*> state{&cv,&done};assert(digitor_sdk_preview_async(s,1,4,4,callback,&state)==DIGITOR_RESULT_OK);{std::unique_lock lock(m);cv.wait(lock,[&]{return done.load();});}DigitorNativeTexture texture{};assert(digitor_sdk_get_native_texture(s,&texture)==DIGITOR_RESULT_OK&&texture.width==4&&texture.pixels);assert(digitor_sdk_destroy(s)==DIGITOR_RESULT_OK);

 // A C++ callback supplied through the C ABI must not be able to terminate the
 // process by unwinding out of an SDK worker thread.
 DigitorSdkSession* throwing=nullptr;assert(digitor_sdk_create(&throwing)==DIGITOR_RESULT_OK);
 auto throws=[](DigitorResult,void*){throw std::runtime_error("callback failure");};
 assert(digitor_sdk_seek_async(throwing,2,throws,nullptr)==DIGITOR_RESULT_OK);
 assert(digitor_sdk_destroy(throwing)==DIGITOR_RESULT_OK);

 DigitorSdkSession* reentrant=nullptr;assert(digitor_sdk_create(&reentrant)==DIGITOR_RESULT_OK);
 struct Reentry { DigitorSdkSession* session; std::atomic_bool done; } reentry{reentrant,false};
 auto reenter=[](DigitorResult result,void* data){auto& state=*static_cast<Reentry*>(data);
   assert(result==DIGITOR_RESULT_OK);
   assert(digitor_sdk_set_color(state.session,{0,1,1})==DIGITOR_RESULT_OK);
   assert(digitor_sdk_destroy(state.session)==DIGITOR_RESULT_RESOURCE_IN_USE);
   state.done.store(true);
 };
 assert(digitor_sdk_seek_async(reentrant,3,reenter,&reentry)==DIGITOR_RESULT_OK);
 while(!reentry.done.load())std::this_thread::yield();
 assert(digitor_sdk_destroy(reentrant)==DIGITOR_RESULT_OK);

 // Destruction and entry through another API may race, but the session object
 // must stay pinned until every in-flight API call has left it.
 DigitorSdkSession* raced=nullptr;assert(digitor_sdk_create(&raced)==DIGITOR_RESULT_OK);
 std::atomic_bool start{false};
 std::thread caller([&]{while(!start.load()){} for(int i=0;i<10000;++i){
   const auto result=digitor_sdk_set_color(raced,{0,1,1});
   assert(result==DIGITOR_RESULT_OK||result==DIGITOR_RESULT_INVALID_ARGUMENT);
 }});
 start.store(true);assert(digitor_sdk_destroy(raced)==DIGITOR_RESULT_OK);caller.join();
}
