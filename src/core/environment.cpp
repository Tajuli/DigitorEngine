#include "core/environment.hpp"

#include <cstdlib>

namespace digitor {

std::optional<std::string> environment_variable(const char* name) noexcept {
    if (name == nullptr || name[0] == '\0') return std::nullopt;
    try {
        const std::string owned_name(name);
#if defined(_WIN32)
        char* buffer = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&buffer, &size, owned_name.c_str()) != 0) {
            std::free(buffer);
            return std::nullopt;
        }
        if (buffer == nullptr || size <= 1 || buffer[0] == '\0') {
            std::free(buffer);
            return std::nullopt;
        }
        std::string value(buffer);
        std::free(buffer);
        return value;
#else
        const char* value = std::getenv(owned_name.c_str());
        if (value == nullptr || value[0] == '\0') return std::nullopt;
        return std::string(value);
#endif
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace digitor
