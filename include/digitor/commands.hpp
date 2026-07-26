#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace digitor {
enum class ResourceState { undefined, common, copy_source, copy_destination, shader_read, shader_write };
struct ResourceTransition { uint64_t resource; ResourceState before; ResourceState after; };
struct PipelineBarrier { std::vector<ResourceTransition> transitions; };
class Fence { public: uint64_t value() const; void signal(uint64_t); void wait(uint64_t); private: mutable std::mutex m_; std::condition_variable cv_; uint64_t value_{}; };
class Semaphore { public: void signal(); void wait(); private: std::mutex m_; std::condition_variable cv_; uint64_t signals_{}; };
class CommandBuffer { public: enum class State { initial, recording, executable, submitted, complete }; State state() const; private: friend class CommandEncoder; friend class CommandQueue; State state_{State::initial}; std::vector<std::function<void()>> commands_; };
class CommandEncoder { public: explicit CommandEncoder(CommandBuffer&); void barrier(PipelineBarrier); void dispatch(std::function<void()>); void finish(); private: CommandBuffer* buffer_; };
class CommandQueue { public: void submit(CommandBuffer&, Fence* = nullptr, uint64_t = 0, Semaphore* wait = nullptr, Semaphore* signal = nullptr); };
struct FrameContext { FrameContext(uint32_t value) : index(value) {} uint32_t index{}; CommandBuffer commands; Fence fence; uint64_t serial{}; };
class TripleBuffer { public: static constexpr uint32_t frame_count=3; FrameContext& begin_frame(); void end_frame(CommandQueue&); private: FrameContext frames_[frame_count]{{0},{1},{2}}; uint64_t serial_{}; FrameContext* active_{}; };
}
