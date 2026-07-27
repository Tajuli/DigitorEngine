#pragma once

#include <optional>
#include <string>

namespace digitor {

// Returns an owning copy. Empty names, missing variables, empty values, and
// allocation/runtime failures are represented uniformly as std::nullopt.
std::optional<std::string> environment_variable(const char* name) noexcept;

} // namespace digitor
