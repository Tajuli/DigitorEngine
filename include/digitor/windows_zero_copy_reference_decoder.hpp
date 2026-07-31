#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace digitor {

struct WindowsReferenceFrame {
  std::int64_t timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<float> linear_rgba;
};

class WindowsZeroCopyReferenceDecoder final {
public:
  explicit WindowsZeroCopyReferenceDecoder(const std::string& media_path);
  ~WindowsZeroCopyReferenceDecoder();
  WindowsZeroCopyReferenceDecoder(const WindowsZeroCopyReferenceDecoder&) = delete;
  WindowsZeroCopyReferenceDecoder& operator=(const WindowsZeroCopyReferenceDecoder&) = delete;

  [[nodiscard]] DigitorResult frame(std::uint64_t index,
                                    WindowsReferenceFrame& out) noexcept;
  [[nodiscard]] const std::string& diagnostic() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
