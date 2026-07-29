#pragma once

namespace digitor {

enum class HostPlatform {
    Windows,
    Android,
    IOS,
    MacOS,
    Linux,
    Unknown
};

[[nodiscard]] HostPlatform current_platform() noexcept;

}  // namespace digitor
