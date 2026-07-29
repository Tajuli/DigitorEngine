#include "platform/platform.hpp"

namespace digitor {

HostPlatform current_platform() noexcept {
#if defined(_WIN32)
    return HostPlatform::Windows;
#elif defined(__ANDROID__)
    return HostPlatform::Android;
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        return HostPlatform::IOS;
    #else
        return HostPlatform::MacOS;
    #endif
#elif defined(__linux__)
    return HostPlatform::Linux;
#else
    return HostPlatform::Unknown;
#endif
}

}  // namespace digitor
