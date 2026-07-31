#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/digitor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace digitor {

class WindowsSoftwareReferenceDecoder final {
public:
  explicit WindowsSoftwareReferenceDecoder(std::string media_path);
  ~WindowsSoftwareReferenceDecoder();
  [[nodiscard]] DigitorResult open(std::string* diagnostic=nullptr) noexcept;
  [[nodiscard]] DigitorResult frame_rgba32f(std::uint32_t frame_index,
                                            std::vector<float>& out) noexcept;
private:
  struct Impl; std::unique_ptr<Impl> impl_;
};

class WindowsD3D12ValidationReadback final {
public:
  explicit WindowsD3D12ValidationReadback(void* d3d12_device);
  ~WindowsD3D12ValidationReadback();
  [[nodiscard]] DigitorResult read(const ProcessedGpuFramePtr&,
                                   std::vector<float>& out) noexcept;
private:
  struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace digitor
