#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#include <string>

namespace digitor {

[[nodiscard]] D3D11_TEXTURE2D_DESC
normalized_d3d11va_interop_desc(const D3D11_TEXTURE2D_DESC &source) noexcept;

[[nodiscard]] std::string format_d3d11_texture_creation_failure(
    HRESULT result, const D3D11_TEXTURE2D_DESC &source,
    const D3D11_TEXTURE2D_DESC &destination, D3D_FEATURE_LEVEL feature_level,
    HRESULT format_support_result, UINT format_support,
    const std::string &debug_message = {});

} // namespace digitor
#endif
