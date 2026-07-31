#include "digitor/windows_zero_copy_frame_broker.hpp"
#include <list>
#include <mutex>
#include <unordered_map>

namespace digitor {
struct WindowsZeroCopyFrameBroker::Impl {
  struct Entry { ProcessedGpuFramePtr frame; std::list<std::int64_t>::iterator lru; };
  WindowsZeroCopyDecodeCallback decode;
  std::size_t capacity{8};
  mutable std::mutex mutex;
  std::unordered_map<std::int64_t,Entry> frames;
  std::list<std::int64_t> order;
  WindowsZeroCopyBrokerStats stats;

  void touch(std::unordered_map<std::int64_t,Entry>::iterator it){
    order.erase(it->second.lru);order.push_front(it->first);it->second.lru=order.begin();
  }
  void trim(){while(frames.size()>capacity){const auto key=order.back();order.pop_back();frames.erase(key);}stats.live_frames=frames.size();}
};
WindowsZeroCopyFrameBroker::WindowsZeroCopyFrameBroker(WindowsZeroCopyDecodeCallback d,std::size_t c):impl_(std::make_unique<Impl>()){
  impl_->decode=std::move(d);impl_->capacity=c?c:1;
}
WindowsZeroCopyFrameBroker::~WindowsZeroCopyFrameBroker()=default;
DigitorResult WindowsZeroCopyFrameBroker::acquire(std::int64_t ts,ProcessedGpuFramePtr& out) noexcept {
  out.reset();try{
    {std::scoped_lock lock(impl_->mutex);++impl_->stats.decode_requests;auto it=impl_->frames.find(ts);if(it!=impl_->frames.end()){
      if(!it->second.frame||!it->second.frame->ready()||it->second.frame->metadata().timestamp!=ts){++impl_->stats.identity_violations;return DIGITOR_RESULT_INTERNAL_ERROR;}
      ++impl_->stats.cache_hits;impl_->touch(it);out=it->second.frame;return DIGITOR_RESULT_OK;}
      ++impl_->stats.cache_misses;}
    ProcessedGpuFramePtr decoded;const auto result=impl_->decode?impl_->decode(ts,decoded):DIGITOR_RESULT_NOT_INITIALIZED;
    if(result!=DIGITOR_RESULT_OK||!decoded)return result==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:result;
    if(decoded->backend()!=DIGITOR_RENDERER_D3D12||decoded->metadata().timestamp!=ts||!decoded->ready()){
      std::scoped_lock lock(impl_->mutex);++impl_->stats.identity_violations;return DIGITOR_RESULT_INTERNAL_ERROR;}
    std::scoped_lock lock(impl_->mutex);impl_->order.push_front(ts);impl_->frames[ts]={decoded,impl_->order.begin()};impl_->trim();out=std::move(decoded);return DIGITOR_RESULT_OK;
  }catch(...){out.reset();return DIGITOR_RESULT_INTERNAL_ERROR;}
}
DigitorResult WindowsZeroCopyFrameBroker::deliver_preview(std::int64_t ts,const WindowsZeroCopyFrameConsumer& c) noexcept {ProcessedGpuFramePtr f;auto r=acquire(ts,f);if(r!=DIGITOR_RESULT_OK)return r;r=c?c(f):DIGITOR_RESULT_NOT_INITIALIZED;if(r==DIGITOR_RESULT_OK){std::scoped_lock lock(impl_->mutex);++impl_->stats.preview_deliveries;}return r;}
DigitorResult WindowsZeroCopyFrameBroker::deliver_export(std::int64_t ts,const WindowsZeroCopyFrameConsumer& c) noexcept {ProcessedGpuFramePtr f;auto r=acquire(ts,f);if(r!=DIGITOR_RESULT_OK)return r;r=c?c(f):DIGITOR_RESULT_NOT_INITIALIZED;if(r==DIGITOR_RESULT_OK){std::scoped_lock lock(impl_->mutex);++impl_->stats.export_deliveries;}return r;}
void WindowsZeroCopyFrameBroker::clear() noexcept {std::scoped_lock lock(impl_->mutex);impl_->frames.clear();impl_->order.clear();impl_->stats.live_frames=0;}
WindowsZeroCopyBrokerStats WindowsZeroCopyFrameBroker::stats() const {std::scoped_lock lock(impl_->mutex);return impl_->stats;}
} // namespace digitor
