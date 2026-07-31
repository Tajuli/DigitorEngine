#pragma once
#include "digitor/gpu_frame.hpp"
#include "digitor/media.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace digitor {

struct PlaybackCacheKey {
  std::string source_identity;
  std::string graph_identity;
  FrameNumber frame{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t epoch{};
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  bool operator==(const PlaybackCacheKey&) const noexcept = default;
};

struct PlaybackCacheKeyHash {
  std::size_t operator()(const PlaybackCacheKey& key) const noexcept {
    auto combine=[](std::size_t& seed,std::size_t value){seed^=value+0x9e3779b97f4a7c15ull+(seed<<6)+(seed>>2);};
    std::size_t seed=std::hash<std::string>{}(key.source_identity);
    combine(seed,std::hash<std::string>{}(key.graph_identity));
    combine(seed,std::hash<FrameNumber>{}(key.frame));
    combine(seed,std::hash<std::uint32_t>{}(key.width));
    combine(seed,std::hash<std::uint32_t>{}(key.height));
    combine(seed,std::hash<std::uint64_t>{}(key.epoch));
    combine(seed,std::hash<unsigned>{}(static_cast<unsigned>(key.backend)));
    return seed;
  }
};

template<class T> class ByteBudgetLruCache {
public:
  using Value=std::shared_ptr<T>;
  using SizeFunction=std::function<std::size_t(const T&)>;
  explicit ByteBudgetLruCache(std::size_t budget_bytes,SizeFunction size_function)
      :budget_bytes_(budget_bytes),size_function_(std::move(size_function)){
    if(!size_function_)throw std::invalid_argument("cache size function is required");
  }
  void set_budget(std::size_t budget){std::lock_guard<std::mutex> lock(mutex_);budget_bytes_=budget;evict_locked();}
  [[nodiscard]] std::size_t budget()const noexcept{std::lock_guard<std::mutex> lock(mutex_);return budget_bytes_;}
  [[nodiscard]] std::size_t bytes()const noexcept{std::lock_guard<std::mutex> lock(mutex_);return bytes_;}
  [[nodiscard]] std::size_t size()const noexcept{std::lock_guard<std::mutex> lock(mutex_);return entries_.size();}
  void put(PlaybackCacheKey key,Value value){
    if(!value)return;
    const auto item_bytes=size_function_(*value);
    std::lock_guard<std::mutex> lock(mutex_);
    erase_locked(key);
    if(!budget_bytes_||item_bytes>budget_bytes_)return;
    entries_.push_front({std::move(key),std::move(value),item_bytes});
    index_[entries_.front().key]=entries_.begin();bytes_+=item_bytes;evict_locked();
  }
  [[nodiscard]] Value get(const PlaybackCacheKey& key){
    std::lock_guard<std::mutex> lock(mutex_);auto found=index_.find(key);if(found==index_.end())return {};
    entries_.splice(entries_.begin(),entries_,found->second);return found->second->value;
  }
  void erase(const PlaybackCacheKey& key){std::lock_guard<std::mutex> lock(mutex_);erase_locked(key);}
  void clear(){std::lock_guard<std::mutex> lock(mutex_);entries_.clear();index_.clear();bytes_=0;}
  void clear_before_epoch(std::uint64_t epoch){
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto it=entries_.begin();it!=entries_.end();){if(it->key.epoch<epoch){bytes_-=it->bytes;index_.erase(it->key);it=entries_.erase(it);}else ++it;}
  }
private:
  struct Entry{PlaybackCacheKey key;Value value;std::size_t bytes{};};
  using Iterator=typename std::list<Entry>::iterator;
  void erase_locked(const PlaybackCacheKey& key){auto found=index_.find(key);if(found==index_.end())return;bytes_-=found->second->bytes;entries_.erase(found->second);index_.erase(found);}
  void evict_locked(){while(bytes_>budget_bytes_&&!entries_.empty()){auto& entry=entries_.back();bytes_-=entry.bytes;index_.erase(entry.key);entries_.pop_back();}}
  mutable std::mutex mutex_;std::size_t budget_bytes_{},bytes_{};SizeFunction size_function_;
  std::list<Entry> entries_;std::unordered_map<PlaybackCacheKey,Iterator,PlaybackCacheKeyHash> index_;
};

struct PlaybackSchedulerOptions {
  std::size_t decoded_cache_bytes{256u*1024u*1024u};
  std::size_t gpu_cache_bytes{512u*1024u*1024u};
  std::uint32_t prefetch_frames{12};
};

struct PlaybackFrameResult {
  std::uint64_t sequence{};
  std::uint64_t epoch{};
  FrameNumber frame{};
  std::shared_ptr<VideoFrame> decoded;
  ProcessedGpuFramePtr gpu;
  bool from_decoded_cache{};
  bool from_gpu_cache{};
};

class PlaybackScheduler final {
public:
  using DecodeCallback=std::function<std::shared_ptr<VideoFrame>(FrameNumber)>;
  using ProcessCallback=std::function<ProcessedGpuFramePtr(const std::shared_ptr<VideoFrame>&)>;
  using SeekCallback=std::function<void(std::int64_t)>;
  using CompletionCallback=std::function<void(const PlaybackFrameResult&)>;
  using ErrorCallback=std::function<void(std::uint64_t,FrameNumber,std::exception_ptr)>;

  PlaybackScheduler(DecodeCallback decode,ProcessCallback process,PlaybackSchedulerOptions options={})
      :decode_(std::move(decode)),process_(std::move(process)),options_(options),
       decoded_cache_(options.decoded_cache_bytes,[](const VideoFrame& frame){return frame.pixels.size()*sizeof(Color);}),
       gpu_cache_(options.gpu_cache_bytes,[](const ProcessedGpuFrame& frame){const auto&m=frame.metadata();return static_cast<std::size_t>(m.width)*m.height*sizeof(Color);}){
    if(!decode_)throw std::invalid_argument("playback decode callback is required");
  }
  ~PlaybackScheduler(){stop();}
  PlaybackScheduler(const PlaybackScheduler&)=delete;PlaybackScheduler& operator=(const PlaybackScheduler&)=delete;

  void start(){std::lock_guard<std::mutex> lock(mutex_);if(worker_.joinable())return;stopping_=false;paused_=false;worker_=std::thread([this]{run();});}
  void stop()noexcept{{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;queue_.clear();}cv_.notify_all();if(worker_.joinable())worker_.join();}
  void pause()noexcept{paused_.store(true,std::memory_order_release);}
  void resume()noexcept{paused_.store(false,std::memory_order_release);cv_.notify_all();}
  [[nodiscard]] bool paused()const noexcept{return paused_.load(std::memory_order_acquire);}

  void set_callbacks(CompletionCallback completion,ErrorCallback error={}){std::lock_guard<std::mutex> lock(mutex_);completion_=std::move(completion);error_=std::move(error);}
  void set_context(std::string source,std::string graph,std::uint32_t width,std::uint32_t height,DigitorRendererBackend backend){
    std::lock_guard<std::mutex> lock(mutex_);source_identity_=std::move(source);graph_identity_=std::move(graph);width_=width;height_=height;backend_=backend;
  }
  [[nodiscard]] std::uint64_t request(FrameNumber frame){
    if(frame<0)throw std::invalid_argument("playback frame must be non-negative");
    const auto sequence=latest_sequence_.fetch_add(1,std::memory_order_acq_rel)+1;
    const auto epoch=epoch_.load(std::memory_order_acquire);
    {std::lock_guard<std::mutex> lock(mutex_);queue_.clear();queue_.push_back({sequence,epoch,frame,true});for(std::uint32_t i=1;i<=options_.prefetch_frames;++i)queue_.push_back({sequence,epoch,frame+static_cast<FrameNumber>(i),false});}
    cv_.notify_all();return sequence;
  }
  void cancel_before(std::uint64_t sequence)noexcept{cancelled_before_.store(sequence,std::memory_order_release);cv_.notify_all();}
  void seek(std::int64_t pts_us,SeekCallback seek_callback){
    if(pts_us<0)throw std::invalid_argument("seek timestamp must be non-negative");
    const auto next_epoch=epoch_.fetch_add(1,std::memory_order_acq_rel)+1;latest_sequence_.fetch_add(1,std::memory_order_acq_rel);
    {std::lock_guard<std::mutex> lock(mutex_);queue_.clear();}
    if(seek_callback)seek_callback(pts_us);
    decoded_cache_.clear_before_epoch(next_epoch);gpu_cache_.clear_before_epoch(next_epoch);cv_.notify_all();
  }
  void invalidate_graph(std::string graph_identity){
    {std::lock_guard<std::mutex> lock(mutex_);graph_identity_=std::move(graph_identity);queue_.clear();}
    epoch_.fetch_add(1,std::memory_order_acq_rel);decoded_cache_.clear();gpu_cache_.clear();cv_.notify_all();
  }
  [[nodiscard]] std::uint64_t epoch()const noexcept{return epoch_.load(std::memory_order_acquire);}
  [[nodiscard]] std::uint64_t latest_sequence()const noexcept{return latest_sequence_.load(std::memory_order_acquire);}
  [[nodiscard]] std::size_t decoded_cache_bytes()const noexcept{return decoded_cache_.bytes();}
  [[nodiscard]] std::size_t gpu_cache_bytes()const noexcept{return gpu_cache_.bytes();}
  [[nodiscard]] std::size_t decoded_cache_size()const noexcept{return decoded_cache_.size();}
  [[nodiscard]] std::size_t gpu_cache_size()const noexcept{return gpu_cache_.size();}

private:
  struct WorkItem{std::uint64_t sequence{},epoch{};FrameNumber frame{};bool deliver{};};
  [[nodiscard]] bool current(const WorkItem& item)const noexcept{return item.epoch==epoch_.load(std::memory_order_acquire)&&item.sequence==latest_sequence_.load(std::memory_order_acquire)&&item.sequence>=cancelled_before_.load(std::memory_order_acquire);}
  PlaybackCacheKey key_for(const WorkItem& item)const{
    std::lock_guard<std::mutex> lock(mutex_);return {source_identity_,graph_identity_,item.frame,width_,height_,item.epoch,backend_};
  }
  void run()noexcept{
    for(;;){
      WorkItem item;
      {std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[this]{return stopping_||(!paused_.load(std::memory_order_acquire)&&!queue_.empty());});if(stopping_)return;item=queue_.front();queue_.pop_front();}
      if(!current(item))continue;
      try{
        const auto key=key_for(item);PlaybackFrameResult result;result.sequence=item.sequence;result.epoch=item.epoch;result.frame=item.frame;
        result.gpu=gpu_cache_.get(key);result.from_gpu_cache=static_cast<bool>(result.gpu);
        result.decoded=decoded_cache_.get(key);result.from_decoded_cache=static_cast<bool>(result.decoded);
        if(!result.decoded){result.decoded=decode_(item.frame);if(!result.decoded)throw std::runtime_error("decoder returned no playback frame");decoded_cache_.put(key,result.decoded);}
        if(process_&&!result.gpu){result.gpu=process_(result.decoded);if(!result.gpu)throw std::runtime_error("GPU processing returned no playback frame");gpu_cache_.put(key,result.gpu);}
        if(item.deliver&&current(item)){CompletionCallback completion;{std::lock_guard<std::mutex> lock(mutex_);completion=completion_;}if(completion)completion(result);}
      }catch(...){if(item.deliver&&current(item)){ErrorCallback error;{std::lock_guard<std::mutex> lock(mutex_);error=error_;}if(error)error(item.sequence,item.frame,std::current_exception());}}
    }
  }

  DecodeCallback decode_;ProcessCallback process_;PlaybackSchedulerOptions options_;
  ByteBudgetLruCache<VideoFrame> decoded_cache_;ByteBudgetLruCache<ProcessedGpuFrame> gpu_cache_;
  mutable std::mutex mutex_;std::condition_variable cv_;std::deque<WorkItem> queue_;std::thread worker_;
  CompletionCallback completion_;ErrorCallback error_;std::string source_identity_,graph_identity_;std::uint32_t width_{},height_{};DigitorRendererBackend backend_{DIGITOR_RENDERER_CPU};
  std::atomic_uint64_t epoch_{1},latest_sequence_{0},cancelled_before_{0};std::atomic_bool paused_{false};bool stopping_{false};
};

} // namespace digitor
