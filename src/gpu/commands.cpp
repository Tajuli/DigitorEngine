#include "digitor/commands.hpp"
#include <stdexcept>
namespace digitor {
uint64_t Fence::value() const { std::lock_guard lock(m_); return value_; }
void Fence::signal(uint64_t v) { { std::lock_guard lock(m_); if(v>value_) value_=v; } cv_.notify_all(); }
void Fence::wait(uint64_t v) { std::unique_lock lock(m_); cv_.wait(lock,[&]{return value_>=v;}); }
void Semaphore::signal(){ {std::lock_guard lock(m_); ++signals_;} cv_.notify_one(); }
void Semaphore::wait(){std::unique_lock lock(m_); cv_.wait(lock,[&]{return signals_>0;}); --signals_;}
CommandBuffer::State CommandBuffer::state() const{return state_;}
CommandEncoder::CommandEncoder(CommandBuffer& b):buffer_(&b){if(b.state_!=CommandBuffer::State::initial&&b.state_!=CommandBuffer::State::complete) throw std::logic_error("command buffer is busy"); b.commands_.clear();b.state_=CommandBuffer::State::recording;}
void CommandEncoder::barrier(PipelineBarrier b){if(!buffer_||buffer_->state_!=CommandBuffer::State::recording)throw std::logic_error("encoder is closed"); buffer_->commands_.push_back([b=std::move(b)]{for(const auto&t:b.transitions)if(t.resource==0||t.before==t.after)throw std::logic_error("invalid resource transition");});}
void CommandEncoder::dispatch(std::function<void()> f){if(!buffer_||!f)throw std::invalid_argument("invalid command");buffer_->commands_.push_back(std::move(f));}
void CommandEncoder::finish(){if(!buffer_)throw std::logic_error("encoder is closed");buffer_->state_=CommandBuffer::State::executable;buffer_=nullptr;}
void CommandQueue::submit(CommandBuffer& b,Fence*f,uint64_t v,Semaphore*w,Semaphore*s){if(b.state_!=CommandBuffer::State::executable)throw std::logic_error("command buffer is not executable");if(w)w->wait();b.state_=CommandBuffer::State::submitted;for(auto&c:b.commands_)c();b.state_=CommandBuffer::State::complete;if(s)s->signal();if(f)f->signal(v);}
FrameContext& TripleBuffer::begin_frame(){if(active_)throw std::logic_error("frame already active");auto&f=frames_[serial_%frame_count];if(f.serial)f.fence.wait(f.serial);f.serial=++serial_;active_=&f;return f;}
void TripleBuffer::end_frame(CommandQueue&q){if(!active_)throw std::logic_error("no active frame");q.submit(active_->commands,&active_->fence,active_->serial);active_=nullptr;}
}
