#pragma once

#include "digitor/digitor.h"

namespace digitor {

class RenderContext final {
public:
    explicit RenderContext(DigitorRendererBackend backend) noexcept
        : backend_(backend) {}

    [[nodiscard]] DigitorRendererBackend backend() const noexcept {
        return backend_;
    }

private:
    DigitorRendererBackend backend_;
};

}  // namespace digitor
