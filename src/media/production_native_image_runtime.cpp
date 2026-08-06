// Aggregated after native_image_runtime.cpp in the same translation unit.
// Keeping these definitions here preserves the existing target wiring and
// gives create_native_image_codec() a platform-specific, non-fallback factory.
namespace digitor {
namespace {
#if defined(_WIN32)
std::unique_ptr<NativeImageCodec> create_windows_wic_image_codec() noexcept {
  try { return std::make_unique<WicImageCodec>(); } catch (...) { return {}; }
}
#elif defined(__ANDROID__)
std::unique_ptr<NativeImageCodec> create_android_image_codec() noexcept {
  try { return std::make_unique<AndroidImageCodec>(); } catch (...) { return {}; }
}
#elif defined(__APPLE__)
// Apple ImageIO is compiled in the Objective-C++ platform translation unit.
// The common C++ target reports unavailable rather than selecting a CPU codec
// or another platform implementation.
std::unique_ptr<NativeImageCodec> create_apple_imageio_codec() noexcept {
  return {};
}
#endif
}  // namespace
}  // namespace digitor
