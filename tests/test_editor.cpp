#include "digitor/media.hpp"
#include "digitor/playback.hpp"
#include "digitor/renderer.hpp"
#include "digitor/timeline.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

void test_editor(){
  digitor::FrameCache<digitor::VideoFrame> cache(1);auto a=std::make_shared<digitor::VideoFrame>();cache.put(1,a);assert(cache.get(1)==a);cache.put(2,std::make_shared<digitor::VideoFrame>());assert(!cache.get(1));
  digitor::Timeline t;auto track=t.add_track("V1");auto x=t.add_clip(track,"a",0,10);auto y=t.add_clip(track,"b",10,10);assert(t.roll(x,y,2));assert(t.find(x)->duration==12&&t.find(y)->source_in==2);assert(t.slip(y,2));assert(t.set_keyframe(x,0,0)&&t.set_keyframe(x,10,10));assert(*t.value_at(x,5)==5);assert(t.undo()&&t.redo());assert(t.schedule(3).size()==1&&t.schedule(3)[0].source_frame==3);assert(t.gaps(track,25).back().end()==25);digitor::Timeline nested;nested.add_track("inside");nested.add_clip(0,"nested source",0,5);auto nested_id=t.add_nested(track,nested,20);assert(t.nested(nested_id)&&t.schedule(22).size()==1);
  int builds=0;digitor::SharedRenderer shared([&](auto&g,const auto&r,auto&out){++builds;auto target=g.create_transient(4);g.add_pass({"shared",{},{{target,digitor::ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.resize(r.width*r.height);});}});});digitor::PreviewRenderer preview(shared);assert(preview.frame(3,2,2)->pixels.size()==4);assert(preview.frame(3,2,2)->pixels.size()==4&&builds==1);

  digitor::ByteBudgetLruCache<digitor::VideoFrame> budget_cache(2*sizeof(digitor::Color),[](const digitor::VideoFrame& frame){return frame.pixels.size()*sizeof(digitor::Color);});
  digitor::PlaybackCacheKey key_a{"source","graph",1,1,1,1,DIGITOR_RENDERER_CPU};
  digitor::PlaybackCacheKey key_b{"source","graph",2,1,1,1,DIGITOR_RENDERER_CPU};
  digitor::PlaybackCacheKey key_c{"source","graph",3,1,1,1,DIGITOR_RENDERER_CPU};
  auto frame_a=std::make_shared<digitor::VideoFrame>();frame_a->pixels.resize(1);
  auto frame_b=std::make_shared<digitor::VideoFrame>();frame_b->pixels.resize(1);
  auto frame_c=std::make_shared<digitor::VideoFrame>();frame_c->pixels.resize(1);
  budget_cache.put(key_a,frame_a);budget_cache.put(key_b,frame_b);assert(budget_cache.size()==2);assert(budget_cache.get(key_a)==frame_a);budget_cache.put(key_c,frame_c);assert(!budget_cache.get(key_b));assert(budget_cache.get(key_a)==frame_a&&budget_cache.get(key_c)==frame_c);

  std::mutex completion_mutex;std::condition_variable completion_cv;digitor::PlaybackFrameResult completed;bool ready=false;int decodes=0;
  digitor::PlaybackSchedulerOptions options;options.prefetch_frames=2;options.decoded_cache_bytes=16*sizeof(digitor::Color);options.gpu_cache_bytes=16*sizeof(digitor::Color);
  digitor::PlaybackScheduler scheduler([&](digitor::FrameNumber frame){++decodes;auto decoded=std::make_shared<digitor::VideoFrame>();decoded->number=frame;decoded->width=1;decoded->height=1;decoded->pixels.resize(1);return decoded;},{},options);
  scheduler.set_context("source","graph",1,1,DIGITOR_RENDERER_CPU);
  scheduler.set_callbacks([&](const digitor::PlaybackFrameResult& result){std::lock_guard<std::mutex> lock(completion_mutex);completed=result;ready=true;completion_cv.notify_all();});
  scheduler.start();const auto first=scheduler.request(5);
  {std::unique_lock<std::mutex> lock(completion_mutex);assert(completion_cv.wait_for(lock,std::chrono::seconds(2),[&]{return ready;}));}
  assert(completed.sequence==first&&completed.frame==5&&completed.decoded&&completed.decoded->number==5);
  {std::lock_guard<std::mutex> lock(completion_mutex);ready=false;}
  const auto second=scheduler.request(5);
  {std::unique_lock<std::mutex> lock(completion_mutex);assert(completion_cv.wait_for(lock,std::chrono::seconds(2),[&]{return ready;}));}
  assert(completed.sequence==second&&completed.from_decoded_cache);
  const auto old_epoch=scheduler.epoch();scheduler.seek(1000,{});assert(scheduler.epoch()==old_epoch+1);assert(scheduler.decoded_cache_size()==0);scheduler.stop();assert(decodes>=1);
}
